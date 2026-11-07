#include "hires_tee.h"

#include <base/str.h>

#include <engine/gfx/image_manipulation.h>
#include <engine/graphics.h>
#include <engine/image.h>
#include <engine/storage.h>

#include <generated/client_data.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

float CatmullRom(float t)
{
	t = std::abs(t);
	if(t < 1.0f)
		return 1.5f * t * t * t - 2.5f * t * t + 1.0f;
	if(t < 2.0f)
		return -0.5f * t * t * t + 2.5f * t * t - 4.0f * t + 2.0f;
	return 0.0f;
}

// RGBA8 -> premultiplied float, upscale by an integer factor, sharpen a little, back to RGBA8
void Upscale(const CImageInfo &Src, int Factor, CImageInfo &Dst)
{
	const int W = (int)Src.m_Width, H = (int)Src.m_Height, DW = W * Factor, DH = H * Factor;
	std::vector<float> vSrc((size_t)W * H * 4);
	for(size_t i = 0; i < (size_t)W * H; i++)
	{
		const float a = Src.m_pData[i * 4 + 3] / 255.0f;
		for(int c = 0; c < 3; c++)
			vSrc[i * 4 + c] = Src.m_pData[i * 4 + c] / 255.0f * a;
		vSrc[i * 4 + 3] = a;
	}
	// horizontal then vertical
	std::vector<float> vTmp((size_t)DW * H * 4), vOut((size_t)DW * DH * 4);
	for(int y = 0; y < H; y++)
		for(int x = 0; x < DW; x++)
		{
			const float sx = (x + 0.5f) / Factor - 0.5f;
			const int x0 = (int)std::floor(sx);
			float Acc[4] = {0, 0, 0, 0}, WSum = 0.0f;
			for(int k = -1; k <= 2; k++)
			{
				const int xi = std::clamp(x0 + k, 0, W - 1);
				const float w = CatmullRom(sx - (x0 + k));
				for(int c = 0; c < 4; c++)
					Acc[c] += vSrc[((size_t)y * W + xi) * 4 + c] * w;
				WSum += w;
			}
			for(int c = 0; c < 4; c++)
				vTmp[((size_t)y * DW + x) * 4 + c] = Acc[c] / WSum;
		}
	for(int y = 0; y < DH; y++)
	{
		const float sy = (y + 0.5f) / Factor - 0.5f;
		const int y0 = (int)std::floor(sy);
		for(int x = 0; x < DW; x++)
		{
			float Acc[4] = {0, 0, 0, 0}, WSum = 0.0f;
			for(int k = -1; k <= 2; k++)
			{
				const int yi = std::clamp(y0 + k, 0, H - 1);
				const float w = CatmullRom(sy - (y0 + k));
				for(int c = 0; c < 4; c++)
					Acc[c] += vTmp[((size_t)yi * DW + x) * 4 + c] * w;
				WSum += w;
			}
			for(int c = 0; c < 4; c++)
				vOut[((size_t)y * DW + x) * 4 + c] = Acc[c] / WSum;
		}
	}

	// gentle unsharp mask on the premultiplied color (3x3 blur as the low pass)
	std::vector<float> vSharp(vOut);
	const float Amount = 0.35f;
	for(int y = 1; y < DH - 1; y++)
		for(int x = 1; x < DW - 1; x++)
			for(int c = 0; c < 4; c++)
			{
				float Blur = 0.0f;
				for(int dy = -1; dy <= 1; dy++)
					for(int dx = -1; dx <= 1; dx++)
						Blur += vOut[((size_t)(y + dy) * DW + (x + dx)) * 4 + c];
				Blur /= 9.0f;
				const float v = vOut[((size_t)y * DW + x) * 4 + c];
				vSharp[((size_t)y * DW + x) * 4 + c] = v + (v - Blur) * Amount;
			}

	Dst.m_Width = DW;
	Dst.m_Height = DH;
	Dst.m_Format = CImageInfo::FORMAT_RGBA;
	Dst.Allocate();
	for(size_t i = 0; i < (size_t)DW * DH; i++)
	{
		const float a = std::clamp(vSharp[i * 4 + 3], 0.0f, 1.0f);
		for(int c = 0; c < 3; c++)
			Dst.m_pData[i * 4 + c] = (uint8_t)std::lround(std::clamp(a > 0.001f ? vSharp[i * 4 + c] / a : 0.0f, 0.0f, 1.0f) * 255.0f);
		Dst.m_pData[i * 4 + 3] = (uint8_t)std::lround(a * 255.0f);
	}
}

// the same grayscale + body weighting the game applies for custom colored skins
void MakeColorable(CImageInfo &Image)
{
	ConvertToGrayscale(Image);
	const CDataSprite &Body = g_pData->m_aSprites[SPRITE_TEE_BODY];
	const size_t BodyW = Body.m_W * (Image.m_Width / Body.m_pSet->m_Gridx);
	const size_t BodyH = Body.m_H * (Image.m_Height / Body.m_pSet->m_Gridy);
	const size_t Pitch = Image.m_Width * 4;
	int aFreq[256] = {0};
	for(size_t y = 0; y < BodyH; y++)
		for(size_t x = 0; x < BodyW; x++)
			if(Image.m_pData[y * Pitch + x * 4 + 3] > 128)
				aFreq[Image.m_pData[y * Pitch + x * 4]]++;
	uint8_t OrgWeight = 1;
	const uint8_t NewWeight = 192;
	for(int i = 1; i < 256; i++)
		if(aFreq[OrgWeight] < aFreq[i])
			OrgWeight = i;
	for(size_t y = 0; y < BodyH; y++)
		for(size_t x = 0; x < BodyW; x++)
		{
			uint8_t *p = &Image.m_pData[y * Pitch + x * 4];
			uint8_t v = p[0];
			if(v <= OrgWeight)
				v = (uint8_t)((v / (float)OrgWeight) * NewWeight);
			else
				v = (uint8_t)(((v - OrgWeight) / (float)(255 - OrgWeight)) * (255 - NewWeight) + NewWeight);
			p[0] = p[1] = p[2] = v;
		}
}

void LoadParts(IGraphics *pGraphics, const CImageInfo &Image, CSkin::CSkinTextures &Out)
{
	Out.m_Body = pGraphics->LoadSpriteTexture(Image, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_BODY]);
	Out.m_BodyOutline = pGraphics->LoadSpriteTexture(Image, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_BODY_OUTLINE]);
	Out.m_Feet = pGraphics->LoadSpriteTexture(Image, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_FOOT]);
	Out.m_FeetOutline = pGraphics->LoadSpriteTexture(Image, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_FOOT_OUTLINE]);
	Out.m_Hands = pGraphics->LoadSpriteTexture(Image, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_HAND]);
	Out.m_HandsOutline = pGraphics->LoadSpriteTexture(Image, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_HAND_OUTLINE]);
	for(size_t i = 0; i < std::size(Out.m_aEyes); i++)
		Out.m_aEyes[i] = pGraphics->LoadSpriteTexture(Image, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_EYE_NORMAL + i]);
}

} // namespace

void FogUpscaleSkin(const CImageInfo &Src, int Factor, CImageInfo &Dst) { Upscale(Src, Factor, Dst); }
void FogMakeColorable(CImageInfo &Image) { MakeColorable(Image); }

void CFogHiResTee::Unload(IGraphics *pGraphics)
{
	m_Original.Unload(pGraphics);
	m_Colorable.Unload(pGraphics);
	m_Original.Reset();
	m_Colorable.Reset();
	m_Name.clear();
	m_Failed = false;
}

bool CFogHiResTee::Get(IGraphics *pGraphics, const char *pSkinName, CSkin::CSkinTextures &Original, CSkin::CSkinTextures &Colorable)
{
	if(m_Name != pSkinName)
	{
		Unload(pGraphics);
		m_Name = pSkinName;

		CImageInfo Image;
		char aPath[256];
		str_format(aPath, sizeof(aPath), "skins/%s.png", pSkinName);
		bool Loaded = pGraphics->LoadPng(Image, aPath, IStorage::TYPE_ALL);
		if(!Loaded)
		{
			str_format(aPath, sizeof(aPath), "downloadedskins/%s.png", pSkinName);
			Loaded = pGraphics->LoadPng(Image, aPath, IStorage::TYPE_SAVE);
		}
		if(!Loaded || Image.m_Format != CImageInfo::FORMAT_RGBA || Image.m_Width < 8)
		{
			Image.Free();
			m_Failed = true;
			return false;
		}

		// aim for a body part of ~384 px, enough for a big tishka on a 1080p screen
		const int Factor = std::clamp((int)std::ceil(1024.0f / Image.m_Width), 1, 4);
		CImageInfo Big;
		Upscale(Image, Factor, Big);
		Image.Free();
		LoadParts(pGraphics, Big, m_Original);
		MakeColorable(Big);
		LoadParts(pGraphics, Big, m_Colorable);
		Big.Free();
	}
	if(m_Failed)
		return false;
	Original = m_Original;
	Colorable = m_Colorable;
	return true;
}
