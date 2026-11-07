#include "tee_style.h"

#include "hires_tee.h"
#include "look.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <generated/client_data.h>

#include <game/client/gameclient.h>
#include <game/client/render.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr float PI2 = 6.28318530718f;
// the body weighting maps the skin's main shade to 192, so this is "full colour"
constexpr float BASE_SHADE = 192.0f / 255.0f;

struct SRgb
{
	float r, g, b;
};

SRgb Mix(const SRgb &a, const SRgb &b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
SRgb FromConfig(unsigned Packed)
{
	const ColorRGBA c = color_cast<ColorRGBA>(ColorHSLA(Packed));
	return {c.r, c.g, c.b};
}
SRgb Hsl(float h, float s, float l)
{
	const ColorRGBA c = color_cast<ColorRGBA>(ColorHSLA(h - std::floor(h), s, l, 1.0f));
	return {c.r, c.g, c.b};
}
float Frac(float x) { return x - std::floor(x); }
float SmoothStep(float a, float b, float x)
{
	const float t = std::clamp((x - a) / std::max(b - a, 1e-5f), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}
float Hash(float x, float y)
{
	const float h = std::sin(x * 127.1f + y * 311.7f) * 43758.5453f;
	return h - std::floor(h);
}
float Noise(float x, float y)
{
	const float ix = std::floor(x), iy = std::floor(y), fx = x - ix, fy = y - iy;
	const float ux = fx * fx * (3.0f - 2.0f * fx), uy = fy * fy * (3.0f - 2.0f * fy);
	const float a = Hash(ix, iy), b = Hash(ix + 1, iy), c = Hash(ix, iy + 1), d = Hash(ix + 1, iy + 1);
	return a + (b - a) * ux + (c - a) * uy + (a - b - c + d) * ux * uy;
}
float Fbm(float x, float y)
{
	float Sum = 0.0f, Amp = 0.5f;
	for(int i = 0; i < 4; i++)
	{
		Sum += Noise(x, y) * Amp;
		x = x * 2.03f + 1.7f;
		y = y * 2.03f - 3.1f;
		Amp *= 0.5f;
	}
	return Sum;
}
// soft periodic band: 1 in the middle of each period, 0 between
float Band(float x, float Soft) { return SmoothStep(0.5f - Soft, 0.5f + Soft, 0.5f + 0.5f * std::cos(PI2 * x)); }

struct SSpriteRect
{
	int x, y, w, h;
};
SSpriteRect SpriteRect(const CImageInfo &Image, int Sprite)
{
	const CDataSprite &s = g_pData->m_aSprites[Sprite];
	const int gx = (int)Image.m_Width / s.m_pSet->m_Gridx, gy = (int)Image.m_Height / s.m_pSet->m_Gridy;
	return {s.m_X * gx, s.m_Y * gy, s.m_W * gx, s.m_H * gy};
}

int ListPatternCallback(const char *pName, int IsDir, int StorageType, void *pUser)
{
	if(IsDir || !str_endswith(pName, ".png"))
		return 0;
	auto *pList = static_cast<std::vector<std::string> *>(pUser);
	std::string Name(pName, str_length(pName) - 4);
	if(std::find(pList->begin(), pList->end(), Name) == pList->end())
		pList->push_back(Name);
	return 0;
}

} // namespace

unsigned CFogTeeStyle::InvertPackedHsl(unsigned Packed)
{
	ColorRGBA Rgb = color_cast<ColorRGBA>(ColorHSLA(Packed).UnclampLighting(ColorHSLA::DARKEST_LGT));
	Rgb = ColorRGBA(1.0f - Rgb.r, 1.0f - Rgb.g, 1.0f - Rgb.b, 1.0f);
	return color_cast<ColorHSLA>(Rgb).Pack(ColorHSLA::DARKEST_LGT, false);
}

bool CFogTeeStyle::Enabled(int Slot) const
{
	if(!g_Config.m_FogTeeStyle)
		return false;
	return Slot == SLOT_PLAYER || g_Config.m_FogDummySync != 0;
}

void CFogTeeStyle::UnloadSlot(SSlot &Slot)
{
	Slot.m_Textures.Unload(Graphics());
	Slot.m_Textures.Reset();
	Slot.m_Base.Free();
	Slot.m_Gray.Free();
	Slot.m_Work.Free();
	Slot.m_Skin.clear();
	Slot.m_Failed = false;
	Slot.m_LastBake = -1.0f;
}

void CFogTeeStyle::Invalidate()
{
	for(auto &Slot : m_aSlots)
		UnloadSlot(Slot);
	for(auto &[Id, Slot] : m_Remote)
		UnloadSlot(Slot);
	m_Remote.clear();
}

void CFogTeeStyle::OnShutdown()
{
	for(auto &[Id, Slot] : m_Remote)
		UnloadSlot(Slot);
	m_Remote.clear();
	for(auto &Slot : m_aSlots)
		UnloadSlot(Slot);
	m_Pattern.Free();
	m_PatternName.clear();
}

bool CFogTeeStyle::LoadSlot(SSlot &Slot, const char *pSkinName)
{
	if(Slot.m_Skin == pSkinName)
		return !Slot.m_Failed;
	UnloadSlot(Slot);
	Slot.m_Skin = pSkinName;

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
		Slot.m_Failed = true;
		return false;
	}
	// a body part of about 192 px: sharp in the big studio preview, still cheap to re-colour
	const int Factor = std::clamp((int)std::lround(512.0f / Image.m_Width), 1, 4);
	FogUpscaleSkin(Image, Factor, Slot.m_Base);
	Image.Free();
	Slot.m_Gray = Slot.m_Base.DeepCopy();
	FogMakeColorable(Slot.m_Gray);
	Slot.m_Work = Slot.m_Base.DeepCopy();

	CSkin::CSkinTextures &t = Slot.m_Textures;
	t.m_BodyOutline = Graphics()->LoadSpriteTexture(Slot.m_Base, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_BODY_OUTLINE]);
	t.m_FeetOutline = Graphics()->LoadSpriteTexture(Slot.m_Base, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_FOOT_OUTLINE]);
	t.m_HandsOutline = Graphics()->LoadSpriteTexture(Slot.m_Base, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_HAND_OUTLINE]);
	for(size_t i = 0; i < std::size(t.m_aEyes); i++)
		t.m_aEyes[i] = Graphics()->LoadSpriteTexture(Slot.m_Base, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_EYE_NORMAL + i]);
	return true;
}

void CFogTeeStyle::ScanPatterns()
{
	m_vPatternFiles.clear();
	Storage()->CreateFolder("fog", IStorage::TYPE_SAVE);
	Storage()->CreateFolder("fog/patterns", IStorage::TYPE_SAVE);
	Storage()->ListDirectory(IStorage::TYPE_ALL, "fog/patterns", ListPatternCallback, &m_vPatternFiles);
	std::sort(m_vPatternFiles.begin(), m_vPatternFiles.end());
	m_PatternsScanned = true;
}

void CFogTeeStyle::LoadPattern()
{
	if(m_PatternName == g_Config.m_FogTeePatternFile)
		return;
	m_PatternName = g_Config.m_FogTeePatternFile;
	m_Pattern.Free();
	if(m_PatternName.empty())
		return;
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "fog/patterns/%s.png", m_PatternName.c_str());
	CImageInfo Image;
	if(Graphics()->LoadPng(Image, aPath, IStorage::TYPE_ALL))
	{
		if(Image.m_Format == CImageInfo::FORMAT_RGBA)
			m_Pattern = std::move(Image);
		else
			Image.Free();
	}
}

void CFogTeeStyle::Bake(SSlot &Slot, const CTeeRenderInfo &Game, const SFogLook &L, float Time)
{
	const bool Invert = L.m_Invert;
	const int Mode = L.m_ColorMode;
	const float Speed = L.m_ColorSpeed / 100.0f;
	const float T = Time * Speed;
	SRgb C1 = FromConfig(L.m_Color1), C2 = FromConfig(L.m_Color2), PatColor = FromConfig(L.m_PatternColor);
	const ColorHSLA Hsl1(L.m_Color1);
	const auto Inv = [&](SRgb c) { return Invert ? SRgb{1.0f - c.r, 1.0f - c.g, 1.0f - c.b} : c; };
	C1 = Inv(C1);
	C2 = Inv(C2);
	PatColor = Inv(PatColor);

	// colour of the body at (u, v) in [0, 1]; Valid = false means "keep the skin's own pixel"
	const bool GameColours = Mode == 0;
	const SRgb GameBody = Inv({Game.m_ColorBody.r, Game.m_ColorBody.g, Game.m_ColorBody.b});
	const SRgb GameFeet = Inv({Game.m_ColorFeet.r, Game.m_ColorFeet.g, Game.m_ColorFeet.b});
	const auto BodyColor = [&](float u, float v) -> SRgb {
		switch(Mode)
		{
		case 1: return Mix(C1, C2, 0.5f - 0.5f * std::cos(T * PI2 * 0.25f));
		case 2: return Mix(C1, C2, 0.5f + 0.5f * std::sin((u * 0.6f + v) * PI2 - T * 2.5f));
		case 3: return Inv(Hsl(T * 0.08f, std::max(Hsl1.s, 0.45f), std::clamp(Hsl1.l, 0.35f, 0.8f)));
		case 4: return Inv(Hsl(T * 0.06f + v * 0.35f + 0.08f * std::sin(u * 6.0f + T), std::max(Hsl1.s, 0.45f), std::clamp(Hsl1.l, 0.35f, 0.8f)));
		case 5: return Mix(C1, C2, std::pow(0.5f + 0.5f * std::sin(T * 3.0f), 6.0f));
		default: return GameBody;
		}
	};

	// pattern
	const int Pattern = L.m_Pattern;
	if(Pattern == 9)
		LoadPattern();
	const bool HasImage = Pattern == 9 && m_Pattern.m_pData;
	const float Opacity = Pattern == 0 || (Pattern == 9 && !HasImage) ? 0.0f : L.m_PatternOpacity / 100.0f;
	const float Scale = 6.0f / (L.m_PatternScale / 100.0f);
	const float Angle = L.m_PatternAngle / 360.0f * PI2;
	const float Ca = std::cos(Angle), Sa = std::sin(Angle);
	const float Offset = Time * L.m_PatternSpeed / 100.0f * 0.5f;
	const float Soft = 0.02f + L.m_PatternSoft / 100.0f * 0.45f;
	const auto PatternAt = [&](float u, float v, SRgb &Color) -> float {
		const float px = ((u - 0.5f) * Ca - (v - 0.5f) * Sa) * Scale;
		const float py = ((u - 0.5f) * Sa + (v - 0.5f) * Ca) * Scale;
		Color = PatColor;
		switch(Pattern)
		{
		case 1: return Band(px + Offset, Soft);
		case 2:
		{
			const float cx = Frac(px + Offset) - 0.5f, cy = Frac(py) - 0.5f;
			const float d = std::sqrt(cx * cx + cy * cy);
			return 1.0f - SmoothStep(0.26f - Soft * 0.4f, 0.26f + Soft * 0.4f, d);
		}
		case 3: return Band(px + 0.22f * std::sin(py * PI2 * 0.7f + Time * 1.3f) + Offset, Soft);
		case 4: return SmoothStep(-Soft, Soft, std::cos(PI2 * (px + Offset)) * std::cos(PI2 * py));
		case 5: return Band(px + std::abs(Frac(py) - 0.5f) + Offset, Soft);
		case 6:
		{
			const float qx = px * 1.4f + Offset, qy = py * 1.4f;
			const float ix = std::floor(qx), iy = std::floor(qy);
			const float h = Hash(ix, iy);
			const float jx = 0.5f + (Hash(ix + 7.0f, iy) - 0.5f) * 0.5f, jy = 0.5f + (Hash(ix, iy + 7.0f) - 0.5f) * 0.5f;
			const float dx = std::abs(qx - ix - jx), dy = std::abs(qy - iy - jy);
			const float Twinkle = std::pow(0.5f + 0.5f * std::sin(Time * 2.5f + h * PI2), 5.0f);
			const float Core = 1.0f - SmoothStep(0.0f, 0.12f + Soft * 0.2f, std::sqrt(dx * dx + dy * dy));
			const float Rays = (1.0f - SmoothStep(0.0f, 0.03f + Soft * 0.04f, std::min(dx, dy))) * (1.0f - SmoothStep(0.0f, 0.4f, std::max(dx, dy)));
			return std::clamp((Core + Rays * 0.8f) * Twinkle, 0.0f, 1.0f);
		}
		case 7:
		{
			const float n = Fbm(px * 0.5f + Offset * 0.5f, py * 0.5f + Time * 0.05f);
			return Band(px * 0.35f + n * 3.0f + Offset * 0.3f, Soft);
		}
		case 8: return Band(std::sqrt(px * px + py * py) * 0.8f - Offset, Soft);
		case 9:
		{
			if(!HasImage)
				return 0.0f;
			const int w = (int)m_Pattern.m_Width, h = (int)m_Pattern.m_Height;
			const float tx = Frac((px + Offset) / 6.0f) * w, ty = Frac(py / 6.0f) * h;
			const uint8_t *p = &m_Pattern.m_pData[((size_t)std::clamp((int)ty, 0, h - 1) * w + std::clamp((int)tx, 0, w - 1)) * 4];
			Color = Inv({p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f});
			return p[3] / 255.0f;
		}
		default: return 0.0f;
		}
	};

	const auto Write = [](uint8_t *pDst, const SRgb &c) {
		pDst[0] = (uint8_t)std::lround(std::clamp(c.r, 0.0f, 1.0f) * 255.0f);
		pDst[1] = (uint8_t)std::lround(std::clamp(c.g, 0.0f, 1.0f) * 255.0f);
		pDst[2] = (uint8_t)std::lround(std::clamp(c.b, 0.0f, 1.0f) * 255.0f);
	};
	// shading above the skin's main tone only adds a soft highlight, so light colours keep their hue
	const auto Shade = [](const SRgb &c, float s) {
		const float Base = std::min(s, 1.0f), Highlight = std::max(s - 1.0f, 0.0f) * 0.3f;
		return SRgb{c.r * Base + Highlight, c.g * Base + Highlight, c.b * Base + Highlight};
	};
	const size_t Pitch = Slot.m_Work.m_Width * 4;

	// body: colour + pattern, shaded by the colourable skin
	{
		const SSpriteRect r = SpriteRect(Slot.m_Work, SPRITE_TEE_BODY);
		for(int y = 0; y < r.h; y++)
			for(int x = 0; x < r.w; x++)
			{
				const size_t i = (size_t)(r.y + y) * Pitch + (size_t)(r.x + x) * 4;
				if(Slot.m_Base.m_pData[i + 3] == 0)
					continue;
				const float u = (x + 0.5f) / r.w, v = (y + 0.5f) / r.h;
				const float g = Slot.m_Gray.m_pData[i] / 255.0f;
				SRgb Base;
				if(GameColours && !Game.m_CustomColoredSkin)
					Base = Inv({Slot.m_Base.m_pData[i] / 255.0f, Slot.m_Base.m_pData[i + 1] / 255.0f, Slot.m_Base.m_pData[i + 2] / 255.0f});
				else if(GameColours)
					Base = Shade(GameBody, g); // exactly like the game's custom colours
				else
					Base = Shade(BodyColor(u, v), g / BASE_SHADE);
				if(Opacity > 0.0f)
				{
					SRgb Pat;
					const float m = PatternAt(u, v, Pat) * Opacity;
					if(m > 0.0f)
						Base = Mix(Base, Shade(Pat, g / BASE_SHADE), m);
				}
				Write(&Slot.m_Work.m_pData[i], Base);
			}
	}

	// feet and hands: one colour each
	SRgb Feet;
	switch(L.m_FeetMode)
	{
	case 1: Feet = GameFeet; break;
	case 2: Feet = C2; break;
	case 3:
	{
		const SRgb b = GameColours ? GameBody : BodyColor(0.5f, 1.0f);
		Feet = {1.0f - b.r, 1.0f - b.g, 1.0f - b.b};
		break;
	}
	default: Feet = GameColours ? GameFeet : BodyColor(0.5f, 1.0f); break;
	}
	const bool FeetOwnPixels = !Game.m_CustomColoredSkin && (GameColours || L.m_FeetMode == 1);
	const SRgb Hands = GameColours ? GameBody : BodyColor(0.5f, 0.5f);
	const struct
	{
		int m_Sprite;
		SRgb m_Color;
		bool m_OwnPixels;
	} aParts[] = {{SPRITE_TEE_FOOT, Feet, FeetOwnPixels}, {SPRITE_TEE_HAND, Hands, GameColours && !Game.m_CustomColoredSkin}};
	for(const auto &Part : aParts)
	{
		const SSpriteRect r = SpriteRect(Slot.m_Work, Part.m_Sprite);
		const float Gain = (GameColours || (Part.m_Sprite == SPRITE_TEE_FOOT && L.m_FeetMode == 1)) ? 1.0f : 1.0f / BASE_SHADE;
		for(int y = 0; y < r.h; y++)
			for(int x = 0; x < r.w; x++)
			{
				const size_t i = (size_t)(r.y + y) * Pitch + (size_t)(r.x + x) * 4;
				if(Slot.m_Base.m_pData[i + 3] == 0)
					continue;
				if(Part.m_OwnPixels)
					Write(&Slot.m_Work.m_pData[i], Inv({Slot.m_Base.m_pData[i] / 255.0f, Slot.m_Base.m_pData[i + 1] / 255.0f, Slot.m_Base.m_pData[i + 2] / 255.0f}));
				else
					Write(&Slot.m_Work.m_pData[i], Shade(Part.m_Color, Slot.m_Gray.m_pData[i] / 255.0f * Gain));
			}
	}

	CSkin::CSkinTextures &t = Slot.m_Textures;
	Graphics()->UnloadTexture(&t.m_Body);
	Graphics()->UnloadTexture(&t.m_Feet);
	Graphics()->UnloadTexture(&t.m_Hands);
	t.m_Body = Graphics()->LoadSpriteTexture(Slot.m_Work, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_BODY]);
	t.m_Feet = Graphics()->LoadSpriteTexture(Slot.m_Work, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_FOOT]);
	t.m_Hands = Graphics()->LoadSpriteTexture(Slot.m_Work, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_HAND]);
}

bool CFogTeeStyle::BakeInto(SSlot &s, CTeeRenderInfo &Info, const SFogLook &Look, const char *pSkinName, float Rate)
{
	if(!pSkinName || !pSkinName[0] || !LoadSlot(s, pSkinName))
		return false;
	const float Now = Client()->GlobalTime();
	if(s.m_LastBake < 0.0f || Now - s.m_LastBake >= 1.0f / Rate || Now < s.m_LastBake)
	{
		s.m_LastBake = Now;
		Bake(s, Info, Look, Now);
	}
	Info.m_OriginalRenderSkin = s.m_Textures;
	Info.m_CustomColoredSkin = false;
	Info.m_ColorBody = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	Info.m_ColorFeet = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	return true;
}

bool CFogTeeStyle::ApplyPreview(CTeeRenderInfo &Info, int Slot, const char *pSkinName)
{
	SFogLook Look = SFogLook::FromConfig(Slot == SLOT_DUMMY);
	Look.m_Style = 1;
	return BakeInto(m_aSlots[Slot], Info, Look, pSkinName, 60.0f);
}

bool CFogTeeStyle::Apply(CTeeRenderInfo &Info, int Slot, const char *pSkinName)
{
	if(!Enabled(Slot))
		return false;
	return ApplyPreview(Info, Slot, pSkinName);
}

bool CFogTeeStyle::ApplyRemote(CTeeRenderInfo &Info, int ClientId, const SFogLook &Look, const char *pSkinName)
{
	if(!Look.m_Style)
		return false;
	SSlot &s = m_Remote[ClientId];
	s.m_LastUsed = Client()->GlobalTime();
	// other players are baked at a calmer rate; fine at game size
	return BakeInto(s, Info, Look, pSkinName, 30.0f);
}

void CFogTeeStyle::SyncDummy()
{
	if(!g_Config.m_FogDummySync)
		return;
	const bool Inverted = g_Config.m_FogDummySync == 2;
	const unsigned Body = Inverted ? InvertPackedHsl(g_Config.m_ClPlayerColorBody) : g_Config.m_ClPlayerColorBody;
	const unsigned Feet = Inverted ? InvertPackedHsl(g_Config.m_ClPlayerColorFeet) : g_Config.m_ClPlayerColorFeet;
	// an inverted dummy always uses custom colours, otherwise the skin's own colours stay
	const int UseCustom = Inverted ? 1 : g_Config.m_ClPlayerUseCustomColor;
	bool Changed = false;
	if(str_comp(g_Config.m_ClDummySkin, g_Config.m_ClPlayerSkin) != 0)
	{
		str_copy(g_Config.m_ClDummySkin, g_Config.m_ClPlayerSkin);
		Changed = true;
	}
	if(g_Config.m_ClDummyUseCustomColor != UseCustom || g_Config.m_ClDummyColorBody != Body || g_Config.m_ClDummyColorFeet != Feet)
	{
		g_Config.m_ClDummyUseCustomColor = UseCustom;
		g_Config.m_ClDummyColorBody = Body;
		g_Config.m_ClDummyColorFeet = Feet;
		Changed = true;
	}
	// servers rate-limit info changes, so a dragged colour slider sends once it settles
	if(Changed)
		m_DummyInfoPending = true;
	if(m_DummyInfoPending && Client()->State() == IClient::STATE_ONLINE && Client()->DummyConnected() && Client()->GlobalTime() >= m_NextDummyInfo)
	{
		GameClient()->SendDummyInfo(false);
		m_DummyInfoPending = false;
		m_NextDummyInfo = Client()->GlobalTime() + 3.0f;
	}
}

void CFogTeeStyle::OnRender()
{
	// forget players that have not been drawn for a while
	const float Now = Client()->GlobalTime();
	for(auto It = m_Remote.begin(); It != m_Remote.end();)
	{
		if(Now - It->second.m_LastUsed > 5.0f)
		{
			UnloadSlot(It->second);
			It = m_Remote.erase(It);
		}
		else
			++It;
	}
	if(!m_PatternsScanned)
		ScanPatterns();
	SyncDummy();
}
