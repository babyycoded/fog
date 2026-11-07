#include "painter.h"

#include <base/io.h>
#include <base/mem.h>
#include <base/math.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/storage.h>

#include <generated/client_data.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr int SKIN_W = 256, SKIN_H = 128;
constexpr size_t MAX_UNDO = 60;

float SmoothStep(float a, float b, float x)
{
	const float t = std::clamp((x - a) / std::max(b - a, 1e-5f), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

// box/bilinear resample of an RGBA image to 256x128 (skins come in any multiple of that)
void ResampleTo(const CImageInfo &Src, CImageInfo &Dst)
{
	Dst.m_Width = SKIN_W;
	Dst.m_Height = SKIN_H;
	Dst.m_Format = CImageInfo::FORMAT_RGBA;
	Dst.Allocate();
	const float sx = (float)Src.m_Width / SKIN_W, sy = (float)Src.m_Height / SKIN_H;
	for(int y = 0; y < SKIN_H; y++)
		for(int x = 0; x < SKIN_W; x++)
		{
			// average the source block (premultiplied so edges don't darken)
			const int x0 = (int)(x * sx), x1 = std::max(x0 + 1, (int)((x + 1) * sx));
			const int y0 = (int)(y * sy), y1 = std::max(y0 + 1, (int)((y + 1) * sy));
			float Acc[4] = {0, 0, 0, 0};
			int n = 0;
			for(int yy = y0; yy < y1 && yy < (int)Src.m_Height; yy++)
				for(int xx = x0; xx < x1 && xx < (int)Src.m_Width; xx++)
				{
					const uint8_t *p = &Src.m_pData[((size_t)yy * Src.m_Width + xx) * 4];
					const float a = p[3] / 255.0f;
					Acc[0] += p[0] * a;
					Acc[1] += p[1] * a;
					Acc[2] += p[2] * a;
					Acc[3] += a;
					n++;
				}
			uint8_t *d = &Dst.m_pData[((size_t)y * SKIN_W + x) * 4];
			const float a = n ? Acc[3] / n : 0.0f;
			for(int c = 0; c < 3; c++)
				d[c] = (uint8_t)std::clamp(Acc[3] > 0.0f ? Acc[c] / Acc[3] : 0.0f, 0.0f, 255.0f);
			d[3] = (uint8_t)std::lround(a * 255.0f);
		}
}

} // namespace

void CFogTeePainter::OnShutdown()
{
	m_Preview.Unload(Graphics());
	m_Preview.Reset();
	for(auto &Texture : m_aCanvas)
		Graphics()->UnloadTexture(&Texture);
	m_Work.Free();
}

CFogTeePainter::SRect CFogTeePainter::SpriteRect(int Sprite) const
{
	const CDataSprite &s = g_pData->m_aSprites[Sprite];
	const int gx = SKIN_W / s.m_pSet->m_Gridx, gy = SKIN_H / s.m_pSet->m_Gridy;
	return {s.m_X * gx, s.m_Y * gy, s.m_W * gx, s.m_H * gy};
}

CFogTeePainter::SRect CFogTeePainter::PartRect(int Part) const
{
	return SpriteRect(Part == PART_BODY ? SPRITE_TEE_BODY : SPRITE_TEE_FOOT);
}

int CFogTeePainter::Width(int Part) const { return PartRect(Part).w; }
int CFogTeePainter::Height(int Part) const { return PartRect(Part).h; }

bool CFogTeePainter::LoadSkinImage(const char *pSkinName, CImageInfo &Out)
{
	CImageInfo Image;
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "skins/%s.png", pSkinName);
	bool Loaded = Graphics()->LoadPng(Image, aPath, IStorage::TYPE_ALL);
	if(!Loaded)
	{
		str_format(aPath, sizeof(aPath), "downloadedskins/%s.png", pSkinName);
		Loaded = Graphics()->LoadPng(Image, aPath, IStorage::TYPE_SAVE);
	}
	if(!Loaded || Image.m_Format != CImageInfo::FORMAT_RGBA || Image.m_Width < 8)
	{
		Image.Free();
		return false;
	}
	ResampleTo(Image, Out);
	Image.Free();
	return true;
}

void CFogTeePainter::LoadTemplate()
{
	CImageInfo Default;
	if(!LoadSkinImage("default", Default))
		return;
	const SRect b = PartRect(PART_BODY), f = PartRect(PART_FEET);
	m_vBodyMask.assign((size_t)b.w * b.h, 0.0f);
	m_vBodyShade.assign((size_t)b.w * b.h, 1.0f);
	m_vFeetMask.assign((size_t)f.w * f.h, 0.0f);
	// the shade map: the default body's brightness relative to its main tone
	double Sum = 0.0;
	int Count = 0;
	for(int y = 0; y < b.h; y++)
		for(int x = 0; x < b.w; x++)
		{
			const uint8_t *p = &Default.m_pData[((size_t)(b.y + y) * SKIN_W + b.x + x) * 4];
			m_vBodyMask[(size_t)y * b.w + x] = p[3] / 255.0f;
			if(p[3] > 200)
			{
				Sum += (p[0] + p[1] + p[2]) / 3.0;
				Count++;
			}
		}
	const float Mean = Count ? (float)(Sum / Count) : 128.0f;
	for(int y = 0; y < b.h; y++)
		for(int x = 0; x < b.w; x++)
		{
			const uint8_t *p = &Default.m_pData[((size_t)(b.y + y) * SKIN_W + b.x + x) * 4];
			m_vBodyShade[(size_t)y * b.w + x] = std::clamp(((p[0] + p[1] + p[2]) / 3.0f) / Mean, 0.55f, 1.35f);
		}
	for(int y = 0; y < f.h; y++)
		for(int x = 0; x < f.w; x++)
			m_vFeetMask[(size_t)y * f.w + x] = Default.m_pData[((size_t)(f.y + y) * SKIN_W + f.x + x) * 4 + 3] / 255.0f;
	Default.Free();
}

void CFogTeePainter::Start(const char *pSkinName)
{
	if(m_Started)
		return;
	LoadTemplate();
	m_Color = ColorHSLA(0.58f, 0.18f, 0.78f).Pack(false);
	Import(pSkinName && pSkinName[0] ? pSkinName : "default");
	m_vUndo.clear();
	m_vRedo.clear();
	m_Started = true;
}

void CFogTeePainter::Import(const char *pSkinName)
{
	CImageInfo Image;
	if(!LoadSkinImage(pSkinName, Image) && !LoadSkinImage("default", Image))
		return;
	if(m_Started)
		PushUndo();
	m_Work.Free();
	m_Work = std::move(Image);
	m_Dirty = true;
}

void CFogTeePainter::NewBlank()
{
	if(!m_Work.m_pData)
		return;
	PushUndo();
	const ColorRGBA Base(0.82f, 0.84f, 0.87f, 1.0f);
	for(int Part = 0; Part < NUM_PARTS; Part++)
	{
		const SRect r = PartRect(Part);
		const std::vector<float> &vMask = Part == PART_BODY ? m_vBodyMask : m_vFeetMask;
		for(int y = 0; y < r.h; y++)
			for(int x = 0; x < r.w; x++)
			{
				uint8_t *p = &m_Work.m_pData[((size_t)(r.y + y) * SKIN_W + r.x + x) * 4];
				const float m = vMask.empty() ? 0.0f : vMask[(size_t)y * r.w + x];
				p[0] = (uint8_t)(Base.r * 255);
				p[1] = (uint8_t)(Base.g * 255);
				p[2] = (uint8_t)(Base.b * 255);
				p[3] = (uint8_t)std::lround(m * 255.0f);
			}
	}
	m_Dirty = true;
}

void CFogTeePainter::PushUndo()
{
	if(!m_Work.m_pData)
		return;
	m_vUndo.emplace_back(m_Work.m_pData, m_Work.m_pData + m_Work.DataSize());
	if(m_vUndo.size() > MAX_UNDO)
		m_vUndo.erase(m_vUndo.begin());
	m_vRedo.clear();
}

void CFogTeePainter::Undo()
{
	if(m_vUndo.empty() || !m_Work.m_pData)
		return;
	m_vRedo.emplace_back(m_Work.m_pData, m_Work.m_pData + m_Work.DataSize());
	std::copy(m_vUndo.back().begin(), m_vUndo.back().end(), m_Work.m_pData);
	m_vUndo.pop_back();
	m_Dirty = true;
}

void CFogTeePainter::Redo()
{
	if(m_vRedo.empty() || !m_Work.m_pData)
		return;
	m_vUndo.emplace_back(m_Work.m_pData, m_Work.m_pData + m_Work.DataSize());
	std::copy(m_vRedo.back().begin(), m_vRedo.back().end(), m_Work.m_pData);
	m_vRedo.pop_back();
	m_Dirty = true;
}

void CFogTeePainter::ClearPart()
{
	if(!m_Work.m_pData)
		return;
	PushUndo();
	const SRect r = PartRect(m_Part);
	for(int y = 0; y < r.h; y++)
		for(int x = 0; x < r.w; x++)
			m_Work.m_pData[((size_t)(r.y + y) * SKIN_W + r.x + x) * 4 + 3] = 0;
	m_Dirty = true;
}

void CFogTeePainter::BeginStroke()
{
	if(!m_Work.m_pData)
		return;
	PushUndo();
	const SRect r = PartRect(m_Part);
	m_vStrokeCover.assign((size_t)r.w * r.h, 0.0f);
	m_InStroke = true;
}

void CFogTeePainter::EndStroke()
{
	m_InStroke = false;
}

void CFogTeePainter::StampOne(vec2 At)
{
	const SRect r = PartRect(m_Part);
	const std::vector<float> &vMask = m_Part == PART_BODY ? m_vBodyMask : m_vFeetMask;
	const std::vector<uint8_t> &vBase = m_vUndo.back(); // the picture before this stroke
	const float Radius = std::max(m_Size * 0.5f, 0.5f);
	const float Hard = m_Hardness / 100.0f;
	const float Opacity = m_Opacity / 100.0f;
	const ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(m_Color));
	const int x0 = std::max(0, (int)std::floor(At.x - Radius - 1)), x1 = std::min(r.w - 1, (int)std::ceil(At.x + Radius + 1));
	const int y0 = std::max(0, (int)std::floor(At.y - Radius - 1)), y1 = std::min(r.h - 1, (int)std::ceil(At.y + Radius + 1));
	for(int y = y0; y <= y1; y++)
		for(int x = x0; x <= x1; x++)
		{
			const float d = std::sqrt((x + 0.5f - At.x) * (x + 0.5f - At.x) + (y + 0.5f - At.y) * (y + 0.5f - At.y));
			// a 1 px brush is a hard pixel; bigger ones fade out by the hardness
			float a = Radius <= 0.75f ? (d <= 0.71f ? 1.0f : 0.0f) : 1.0f - SmoothStep(Radius * Hard, Radius + 0.5f, d);
			if(a <= 0.0f)
				continue;
			const size_t m = (size_t)y * r.w + x;
			if(m_LockShape && !vMask.empty())
				a *= std::clamp(vMask[m] * 1.4f, 0.0f, 1.0f);
			a *= Opacity;
			if(a <= m_vStrokeCover[m])
				continue;
			m_vStrokeCover[m] = a;
			const size_t i = ((size_t)(r.y + y) * SKIN_W + r.x + x) * 4;
			const uint8_t *b = &vBase[i];
			uint8_t *p = &m_Work.m_pData[i];
			const float ba = b[3] / 255.0f;
			if(m_Tool == TOOL_ERASER)
			{
				p[0] = b[0];
				p[1] = b[1];
				p[2] = b[2];
				p[3] = (uint8_t)std::lround(ba * (1.0f - a) * 255.0f);
				continue;
			}
			// source over, un-premultiplied
			const float oa = a + ba * (1.0f - a);
			const float Src[3] = {Color.r, Color.g, Color.b};
			for(int c = 0; c < 3; c++)
			{
				const float v = oa > 0.0f ? (Src[c] * a + b[c] / 255.0f * ba * (1.0f - a)) / oa : 0.0f;
				p[c] = (uint8_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f);
			}
			p[3] = (uint8_t)std::lround(oa * 255.0f);
		}
}

void CFogTeePainter::Stamp(vec2 At)
{
	StampOne(At);
	if(m_Mirror)
		StampOne(vec2(Width(m_Part) - At.x, At.y));
}

void CFogTeePainter::Stroke(vec2 From, vec2 To)
{
	if(!m_InStroke || !m_Work.m_pData || m_vUndo.empty())
		return;
	const float Step = std::max(m_Size * 0.2f, 0.4f);
	const float Len = distance(From, To);
	const int Steps = std::max(1, (int)std::ceil(Len / Step));
	for(int s = 0; s <= Steps; s++)
		Stamp(mix(From, To, s / (float)Steps));
	m_Dirty = true;
}

void CFogTeePainter::Fill(vec2 At)
{
	if(!m_Work.m_pData)
		return;
	const SRect r = PartRect(m_Part);
	const int sx = (int)At.x, sy = (int)At.y;
	if(sx < 0 || sy < 0 || sx >= r.w || sy >= r.h)
		return;
	PushUndo();
	const std::vector<float> &vMask = m_Part == PART_BODY ? m_vBodyMask : m_vFeetMask;
	const ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(m_Color));
	const auto Px = [&](int x, int y) { return &m_Work.m_pData[((size_t)(r.y + y) * SKIN_W + r.x + x) * 4]; };
	uint8_t aSeed[4];
	mem_copy(aSeed, Px(sx, sy), 4);
	const auto Similar = [&](const uint8_t *p) {
		if(aSeed[3] < 20 && p[3] < 20)
			return true;
		return std::abs(p[0] - aSeed[0]) + std::abs(p[1] - aSeed[1]) + std::abs(p[2] - aSeed[2]) < 60 && std::abs(p[3] - aSeed[3]) < 60;
	};
	std::vector<uint8_t> vSeen((size_t)r.w * r.h, 0);
	std::vector<std::pair<int, int>> vStack = {{sx, sy}};
	const float Opacity = m_Opacity / 100.0f;
	while(!vStack.empty())
	{
		auto [x, y] = vStack.back();
		vStack.pop_back();
		if(x < 0 || y < 0 || x >= r.w || y >= r.h)
			continue;
		const size_t m = (size_t)y * r.w + x;
		if(vSeen[m] || !Similar(Px(x, y)))
			continue;
		vSeen[m] = 1;
		float a = Opacity;
		if(m_LockShape && !vMask.empty())
			a *= std::clamp(vMask[m] * 1.4f, 0.0f, 1.0f);
		if(a > 0.0f)
		{
			uint8_t *p = Px(x, y);
			const float ba = p[3] / 255.0f, oa = a + ba * (1.0f - a);
			const float Src[3] = {Color.r, Color.g, Color.b};
			for(int c = 0; c < 3; c++)
				p[c] = (uint8_t)std::lround(std::clamp(oa > 0.0f ? (Src[c] * a + p[c] / 255.0f * ba * (1.0f - a)) / oa : 0.0f, 0.0f, 1.0f) * 255.0f);
			p[3] = (uint8_t)std::lround(oa * 255.0f);
		}
		vStack.push_back({x + 1, y});
		vStack.push_back({x - 1, y});
		vStack.push_back({x, y + 1});
		vStack.push_back({x, y - 1});
	}
	m_Dirty = true;
}

void CFogTeePainter::Pick(vec2 At)
{
	if(!m_Work.m_pData)
		return;
	const SRect r = PartRect(m_Part);
	const int x = std::clamp((int)At.x, 0, r.w - 1), y = std::clamp((int)At.y, 0, r.h - 1);
	const uint8_t *p = &m_Work.m_pData[((size_t)(r.y + y) * SKIN_W + r.x + x) * 4];
	if(p[3] < 10)
		return;
	m_Color = color_cast<ColorHSLA>(ColorRGBA(p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, 1.0f)).Pack(false);
}

// the finished skin: shading, outlines grown from the shapes, hands in the body's colour
CImageInfo CFogTeePainter::Compose() const
{
	CImageInfo Out = m_Work.DeepCopy();
	const SRect b = PartRect(PART_BODY);
	float Avg[3] = {0, 0, 0}, AvgW = 0.0f;
	for(int y = 0; y < b.h; y++)
		for(int x = 0; x < b.w; x++)
		{
			uint8_t *p = &Out.m_pData[((size_t)(b.y + y) * SKIN_W + b.x + x) * 4];
			const float w = p[3] / 255.0f;
			for(int c = 0; c < 3; c++)
				Avg[c] += p[c] * w;
			AvgW += w;
			if(m_Shading && !m_vBodyShade.empty())
			{
				const float s = m_vBodyShade[(size_t)y * b.w + x];
				for(int c = 0; c < 3; c++)
					p[c] = (uint8_t)std::clamp(p[c] * s, 0.0f, 255.0f);
			}
		}

	// outlines: a dark silhouette a few pixels bigger than the painted shape
	const auto GrowOutline = [&](int FillSprite, int OutlineSprite) {
		const SRect f = SpriteRect(FillSprite), o = SpriteRect(OutlineSprite);
		const float R = (float)m_Outline;
		const int Ri = (int)std::ceil(R);
		for(int y = 0; y < o.h; y++)
			for(int x = 0; x < o.w; x++)
			{
				float Best = 0.0f;
				for(int dy = -Ri; dy <= Ri && Best < 1.0f; dy++)
					for(int dx = -Ri; dx <= Ri; dx++)
					{
						const int sx = x + dx, sy = y + dy;
						if(sx < 0 || sy < 0 || sx >= f.w || sy >= f.h)
							continue;
						const float d = std::sqrt((float)(dx * dx + dy * dy));
						if(d > R + 0.5f)
							continue;
						const float a = m_Work.m_pData[((size_t)(f.y + sy) * SKIN_W + f.x + sx) * 4 + 3] / 255.0f * std::clamp(R + 0.5f - d, 0.0f, 1.0f);
						Best = std::max(Best, a);
					}
				uint8_t *p = &Out.m_pData[((size_t)(o.y + y) * SKIN_W + o.x + x) * 4];
				p[0] = 18;
				p[1] = 19;
				p[2] = 22;
				p[3] = (uint8_t)std::lround(Best * 255.0f);
			}
	};
	if(m_Outline > 0)
	{
		GrowOutline(SPRITE_TEE_BODY, SPRITE_TEE_BODY_OUTLINE);
		GrowOutline(SPRITE_TEE_FOOT, SPRITE_TEE_FOOT_OUTLINE);
	}

	// hands: keep their shape and shading, take the body's average colour
	if(AvgW > 0.0f)
	{
		const SRect h = SpriteRect(SPRITE_TEE_HAND);
		for(int y = 0; y < h.h; y++)
			for(int x = 0; x < h.w; x++)
			{
				uint8_t *p = &Out.m_pData[((size_t)(h.y + y) * SKIN_W + h.x + x) * 4];
				const float g = (p[0] + p[1] + p[2]) / 3.0f / 200.0f;
				for(int c = 0; c < 3; c++)
					p[c] = (uint8_t)std::clamp(Avg[c] / AvgW * g, 0.0f, 255.0f);
			}
	}
	return Out;
}

void CFogTeePainter::Rebuild()
{
	CImageInfo Out = Compose();
	m_Preview.Unload(Graphics());
	m_Preview.m_Body = Graphics()->LoadSpriteTexture(Out, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_BODY]);
	m_Preview.m_BodyOutline = Graphics()->LoadSpriteTexture(Out, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_BODY_OUTLINE]);
	m_Preview.m_Feet = Graphics()->LoadSpriteTexture(Out, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_FOOT]);
	m_Preview.m_FeetOutline = Graphics()->LoadSpriteTexture(Out, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_FOOT_OUTLINE]);
	m_Preview.m_Hands = Graphics()->LoadSpriteTexture(Out, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_HAND]);
	m_Preview.m_HandsOutline = Graphics()->LoadSpriteTexture(Out, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_HAND_OUTLINE]);
	for(size_t i = 0; i < std::size(m_Preview.m_aEyes); i++)
		m_Preview.m_aEyes[i] = Graphics()->LoadSpriteTexture(Out, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_EYE_NORMAL + i]);

	// canvases: outline under fill, blown up with nearest neighbour so pixels stay crisp
	constexpr int ZOOM = 4;
	for(int Part = 0; Part < NUM_PARTS; Part++)
	{
		const SRect f = PartRect(Part), o = SpriteRect(Part == PART_BODY ? SPRITE_TEE_BODY_OUTLINE : SPRITE_TEE_FOOT_OUTLINE);
		CImageInfo Canvas;
		Canvas.m_Width = f.w * ZOOM;
		Canvas.m_Height = f.h * ZOOM;
		Canvas.m_Format = CImageInfo::FORMAT_RGBA;
		Canvas.Allocate();
		for(int y = 0; y < f.h; y++)
			for(int x = 0; x < f.w; x++)
			{
				const uint8_t *pf = &Out.m_pData[((size_t)(f.y + y) * SKIN_W + f.x + x) * 4];
				const uint8_t *po = &Out.m_pData[((size_t)(o.y + y) * SKIN_W + o.x + x) * 4];
				const float fa = pf[3] / 255.0f, oa = po[3] / 255.0f;
				const float a = fa + oa * (1.0f - fa);
				uint8_t Px[4];
				for(int c = 0; c < 3; c++)
					Px[c] = (uint8_t)std::clamp(a > 0.0f ? (pf[c] * fa + po[c] * oa * (1.0f - fa)) / a : 0.0f, 0.0f, 255.0f);
				Px[3] = (uint8_t)std::lround(a * 255.0f);
				for(int zy = 0; zy < ZOOM; zy++)
					for(int zx = 0; zx < ZOOM; zx++)
						mem_copy(&Canvas.m_pData[(((size_t)y * ZOOM + zy) * Canvas.m_Width + x * ZOOM + zx) * 4], Px, 4);
			}
		Graphics()->UnloadTexture(&m_aCanvas[Part]);
		m_aCanvas[Part] = Graphics()->LoadTextureRawMove(Canvas, 0, "fog_painter_canvas");
	}
	Out.Free();
}

const CSkin::CSkinTextures &CFogTeePainter::PreviewTextures()
{
	const float Now = Client()->GlobalTime();
	if(m_Work.m_pData && m_Dirty && (m_LastRebuild < 0.0f || Now - m_LastRebuild > 1.0f / 30.0f || Now < m_LastRebuild))
	{
		m_Dirty = false;
		m_LastRebuild = Now;
		Rebuild();
	}
	return m_Preview;
}

IGraphics::CTextureHandle CFogTeePainter::CanvasTexture(int Part)
{
	PreviewTextures();
	return m_aCanvas[Part];
}

bool CFogTeePainter::Save(const char *pName, char *pError, int ErrorSize)
{
	if(!m_Work.m_pData)
		return false;
	if(!pName[0] || !str_valid_filename(pName))
	{
		str_copy(pError, "Use a name without / \\ : * ? \" < > |", ErrorSize);
		return false;
	}
	Storage()->CreateFolder("skins", IStorage::TYPE_SAVE);
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "skins/%s.png", pName);
	CImageInfo Out = Compose();
	const bool Ok = CImageLoader::SavePng(Storage()->OpenFile(aPath, IOFLAG_WRITE, IStorage::TYPE_SAVE), aPath, Out);
	Out.Free();
	if(!Ok)
		str_copy(pError, "Could not write the file", ErrorSize);
	return Ok;
}
