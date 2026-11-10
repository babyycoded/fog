#include "glow_atlas.h"

#include <engine/image.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <unordered_map>

namespace {

constexpr int DOWNSAMPLE = 2; // the glow is soft anyway, half resolution keeps it cheap
constexpr int MAX_ATLAS_WIDTH = 1024;
constexpr int GUTTER = 4; // empty texels between cells, so filtering and mipmaps never bleed

void GaussianBlur(std::vector<float> &vData, int Width, int Height, float Sigma)
{
	if(Sigma < 0.3f)
		return;
	const int Radius = (int)std::ceil(Sigma * 3.0f);
	std::vector<float> vKernel(Radius * 2 + 1);
	float Sum = 0.0f;
	for(int i = -Radius; i <= Radius; i++)
		Sum += vKernel[i + Radius] = std::exp(-(i * i) / (2.0f * Sigma * Sigma));
	for(float &k : vKernel)
		k /= Sum;

	std::vector<float> vTmp(vData.size());
	for(int y = 0; y < Height; y++)
		for(int x = 0; x < Width; x++)
		{
			float Acc = 0.0f;
			for(int k = -Radius; k <= Radius; k++)
			{
				const int sx = x + k;
				if(sx >= 0 && sx < Width)
					Acc += vData[(size_t)y * Width + sx] * vKernel[k + Radius];
			}
			vTmp[(size_t)y * Width + x] = Acc;
		}
	for(int y = 0; y < Height; y++)
		for(int x = 0; x < Width; x++)
		{
			float Acc = 0.0f;
			for(int k = -Radius; k <= Radius; k++)
			{
				const int sy = y + k;
				if(sy >= 0 && sy < Height)
					Acc += vTmp[(size_t)sy * Width + x] * vKernel[k + Radius];
			}
			vData[(size_t)y * Width + x] = Acc;
		}
}

struct SCell
{
	std::vector<float> m_vData;
	int m_Width = 0;
	int m_Height = 0;
	int m_X = 0;
	int m_Y = 0;
	float m_TexelsPerUnit = 1.0f; // in the downsampled cell
};

// Lines share most of their letters, so blurred cells are kept across builds. The key is the
// exact glyph mask plus the blur parameters, so a hit gives the very same floats a fresh blur
// would; nothing depends on where the glyph sits in the text atlas.
struct SCachedCell
{
	std::vector<uint8_t> m_vMask;
	int m_MaskW;
	int m_MaskH;
	uint32_t m_SigmaBits;
	uint32_t m_TexelsBits;
	SCell m_Cell;
};

constexpr size_t MAX_CACHED_CELLS = 512;
std::unordered_map<uint64_t, std::vector<SCachedCell>> gs_CellCache;
size_t gs_NumCachedCells = 0;

uint32_t FloatBits(float Value)
{
	uint32_t Bits;
	std::memcpy(&Bits, &Value, sizeof(Bits));
	return Bits;
}

uint64_t HashMask(const std::vector<uint8_t> &vMask, int MaskW, int MaskH, uint32_t SigmaBits, uint32_t TexelsBits)
{
	uint64_t Hash = 1469598103934665603ull;
	auto Mix = [&](uint64_t Value) {
		Hash ^= Value;
		Hash *= 1099511628211ull;
	};
	Mix((uint64_t)MaskW);
	Mix((uint64_t)MaskH);
	Mix(SigmaBits);
	Mix(TexelsBits);
	for(uint8_t Byte : vMask)
		Mix(Byte);
	return Hash;
}

const SCell *FindCachedCell(uint64_t Hash, const std::vector<uint8_t> &vMask, int MaskW, int MaskH, uint32_t SigmaBits, uint32_t TexelsBits)
{
	const auto Found = gs_CellCache.find(Hash);
	if(Found == gs_CellCache.end())
		return nullptr;
	for(const SCachedCell &Cached : Found->second)
		if(Cached.m_MaskW == MaskW && Cached.m_MaskH == MaskH && Cached.m_SigmaBits == SigmaBits && Cached.m_TexelsBits == TexelsBits && Cached.m_vMask == vMask)
			return &Cached.m_Cell;
	return nullptr;
}

} // namespace

void CFogGlowAtlas::Unload(IGraphics *pGraphics)
{
	if(m_Texture.IsValid())
		pGraphics->UnloadTexture(&m_Texture);
	m_vEntries.clear();
}

void CFogGlowAtlas::Build(IGraphics *pGraphics, ITextRender *pTextRender, const std::vector<STextGlyph> &vGlyphs, float BlurRadius)
{
	Unload(pGraphics);
	m_vEntries.assign(vGlyphs.size(), SEntry());

	// the same letter repeats a lot in a line: blur each atlas glyph once
	std::map<std::tuple<int, int, int, int>, int> CellOfGlyph;
	std::vector<SCell> vCells;
	std::vector<int> vGlyphCell(vGlyphs.size(), -1);

	std::vector<uint8_t> vMask;
	for(size_t i = 0; i < vGlyphs.size(); i++)
	{
		const STextGlyph &Glyph = vGlyphs[i];
		const auto Key = std::make_tuple((int)Glyph.m_aUVs[0], (int)Glyph.m_aUVs[1], (int)Glyph.m_aUVs[2], (int)Glyph.m_aUVs[3]);
		const auto Found = CellOfGlyph.find(Key);
		if(Found != CellOfGlyph.end())
		{
			vGlyphCell[i] = Found->second;
			continue;
		}

		int MaskW, MaskH;
		if(Glyph.m_Size.x <= 0.0f || !pTextRender->GetGlyphMask(Glyph, vMask, MaskW, MaskH))
			continue;

		const float TexelsPerUnit = MaskW / Glyph.m_Size.x / DOWNSAMPLE;
		const float Sigma = BlurRadius * TexelsPerUnit;
		const int Pad = (int)std::ceil(Sigma * 2.6f) + 1;
		const int SmallW = (MaskW + DOWNSAMPLE - 1) / DOWNSAMPLE;
		const int SmallH = (MaskH + DOWNSAMPLE - 1) / DOWNSAMPLE;

		const uint32_t SigmaBits = FloatBits(Sigma);
		const uint32_t TexelsBits = FloatBits(TexelsPerUnit);
		const uint64_t Hash = HashMask(vMask, MaskW, MaskH, SigmaBits, TexelsBits);
		SCell Cell;
		if(const SCell *pCached = FindCachedCell(Hash, vMask, MaskW, MaskH, SigmaBits, TexelsBits))
			Cell = *pCached;
		else
		{
			Cell.m_Width = SmallW + Pad * 2;
			Cell.m_Height = SmallH + Pad * 2;
			Cell.m_TexelsPerUnit = TexelsPerUnit;
			Cell.m_vData.assign((size_t)Cell.m_Width * Cell.m_Height, 0.0f);
			for(int y = 0; y < MaskH; y++)
				for(int x = 0; x < MaskW; x++)
					Cell.m_vData[(size_t)(y / DOWNSAMPLE + Pad) * Cell.m_Width + (x / DOWNSAMPLE + Pad)] += vMask[(size_t)y * MaskW + x] / (255.0f * DOWNSAMPLE * DOWNSAMPLE);
			GaussianBlur(Cell.m_vData, Cell.m_Width, Cell.m_Height, Sigma);

			if(gs_NumCachedCells >= MAX_CACHED_CELLS)
			{
				gs_CellCache.clear();
				gs_NumCachedCells = 0;
			}
			gs_CellCache[Hash].push_back({vMask, MaskW, MaskH, SigmaBits, TexelsBits, Cell});
			gs_NumCachedCells++;
		}

		CellOfGlyph[Key] = (int)vCells.size();
		vGlyphCell[i] = (int)vCells.size();
		vCells.push_back(std::move(Cell));
	}
	if(vCells.empty())
		return;

	// shelf packing
	int AtlasW = 0, AtlasH = 0, CursorX = GUTTER, CursorY = GUTTER, ShelfH = 0;
	for(SCell &Cell : vCells)
	{
		if(CursorX + Cell.m_Width + GUTTER > MAX_ATLAS_WIDTH)
		{
			CursorX = GUTTER;
			CursorY += ShelfH + GUTTER;
			ShelfH = 0;
		}
		Cell.m_X = CursorX;
		Cell.m_Y = CursorY;
		CursorX += Cell.m_Width + GUTTER;
		ShelfH = std::max(ShelfH, Cell.m_Height);
		AtlasW = std::max(AtlasW, CursorX);
	}
	AtlasH = CursorY + ShelfH + GUTTER;

	// one shared scale so all letters glow equally bright; a slight gamma keeps the tail long
	float Peak = 0.0f;
	for(const SCell &Cell : vCells)
		for(float v : Cell.m_vData)
			Peak = std::max(Peak, v);
	if(Peak <= 0.0f)
		return;

	CImageInfo Image;
	Image.m_Width = AtlasW;
	Image.m_Height = AtlasH;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	std::vector<uint8_t> vPixels((size_t)AtlasW * AtlasH * 4, 0);
	for(size_t p = 0; p < vPixels.size(); p += 4)
		vPixels[p] = vPixels[p + 1] = vPixels[p + 2] = 255;
	for(const SCell &Cell : vCells)
		for(int y = 0; y < Cell.m_Height; y++)
			for(int x = 0; x < Cell.m_Width; x++)
			{
				const float v = std::pow(std::clamp(Cell.m_vData[(size_t)y * Cell.m_Width + x] / Peak, 0.0f, 1.0f), 0.8f);
				vPixels[((size_t)(Cell.m_Y + y) * AtlasW + (Cell.m_X + x)) * 4 + 3] = (uint8_t)std::lround(v * 255.0f);
			}
	Image.m_pData = vPixels.data();
	m_Texture = pGraphics->LoadTextureRaw(Image, 0, "fog_glow_atlas");
	Image.m_pData = nullptr; // owned by the vector

	for(size_t i = 0; i < vGlyphs.size(); i++)
	{
		if(vGlyphCell[i] < 0)
			continue;
		const SCell &Cell = vCells[vGlyphCell[i]];
		SEntry &Entry = m_vEntries[i];
		Entry.m_aUVs[0] = (Cell.m_X + 0.5f) / AtlasW;
		Entry.m_aUVs[1] = (Cell.m_Y + 0.5f) / AtlasH;
		Entry.m_aUVs[2] = (Cell.m_X + Cell.m_Width - 0.5f) / AtlasW;
		Entry.m_aUVs[3] = (Cell.m_Y + Cell.m_Height - 0.5f) / AtlasH;
		Entry.m_Size = vec2(Cell.m_Width - 1.0f, Cell.m_Height - 1.0f) / Cell.m_TexelsPerUnit;
		Entry.m_Valid = true;
	}
}
