#include "brand.h"

#include "glass.h"

#include <base/math.h>

#include <engine/graphics.h>
#include <engine/image.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/ui.h>
#include <game/client/ui_rect.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

IGraphics::CTextureHandle s_Mark, s_MarkGlow, s_Blob, s_Streak, s_Dot;

float SmoothStep(float a, float b, float x)
{
	const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

float Hash(int i, int Salt)
{
	unsigned h = (unsigned)i * 2654435761u ^ (unsigned)Salt * 2246822519u;
	h ^= h >> 15;
	h *= 3266489917u;
	h ^= h >> 13;
	return (h & 0xffff) / 65536.0f;
}

IGraphics::CTextureHandle Upload(IGraphics *pGraphics, const std::vector<float> &vAlpha, int W, int H, const char *pName)
{
	std::vector<uint8_t> vPixels((size_t)W * H * 4);
	for(size_t i = 0; i < (size_t)W * H; i++)
	{
		vPixels[i * 4] = vPixels[i * 4 + 1] = vPixels[i * 4 + 2] = 255;
		vPixels[i * 4 + 3] = (uint8_t)std::lround(std::clamp(vAlpha[i], 0.0f, 1.0f) * 255.0f);
	}
	CImageInfo Image;
	Image.m_Width = W;
	Image.m_Height = H;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.m_pData = vPixels.data();
	IGraphics::CTextureHandle Texture = pGraphics->LoadTextureRaw(Image, 0, pName);
	Image.m_pData = nullptr;
	return Texture;
}

float Capsule(vec2 p, vec2 a, vec2 b, float Radius)
{
	const vec2 pa = p - a, ba = b - a;
	const float h = std::clamp(dot(pa, ba) / dot(ba, ba), 0.0f, 1.0f);
	return length(pa - ba * h) - Radius;
}

// The mark as alpha: a round tee body with knocked-out eyes, its lower half dissolving
// into three strata of mist.
std::vector<float> BuildMark(int N)
{
	std::vector<float> vAlpha((size_t)N * N);
	const float Px = 1.6f / N;
	for(int y = 0; y < N; y++)
		for(int x = 0; x < N; x++)
		{
			const vec2 p((x + 0.5f) / N, (y + 0.5f) / N);
			const float DiscD = length(p - vec2(0.5f, 0.45f)) - 0.34f;
			float Body = 1.0f - SmoothStep(-Px, Px, DiscD);
			Body *= 1.0f - SmoothStep(0.50f, 0.70f, p.y);

			// eyes looking a little down, cut out of the body
			float Eyes = 0.0f;
			for(float Ex : {0.435f, 0.575f})
			{
				const vec2 d = (p - vec2(Ex, 0.44f)) / vec2(0.034f, 0.072f);
				Eyes = std::max(Eyes, 1.0f - SmoothStep(1.0f - Px * 25.0f, 1.0f + Px * 25.0f, length(d)));
			}
			Body *= 1.0f - Eyes;

			float Strata = 0.0f;
			const struct
			{
				float m_Y, m_X0, m_X1, m_Alpha;
			} aBands[] = {{0.665f, 0.20f, 0.80f, 0.95f}, {0.765f, 0.30f, 0.76f, 0.70f}, {0.865f, 0.40f, 0.64f, 0.45f}};
			for(const auto &Band : aBands)
			{
				const float d = Capsule(p, vec2(Band.m_X0, Band.m_Y), vec2(Band.m_X1, Band.m_Y), 0.024f);
				Strata = std::max(Strata, (1.0f - SmoothStep(-Px, Px, d)) * Band.m_Alpha);
			}
			vAlpha[(size_t)y * N + x] = std::max(Body, Strata);
		}
	return vAlpha;
}

void Blur(std::vector<float> &vData, int W, int H, float Sigma)
{
	const int Radius = (int)std::ceil(Sigma * 3.0f);
	std::vector<float> vKernel(Radius * 2 + 1);
	float Sum = 0.0f;
	for(int i = -Radius; i <= Radius; i++)
		Sum += vKernel[i + Radius] = std::exp(-(i * i) / (2.0f * Sigma * Sigma));
	for(float &k : vKernel)
		k /= Sum;
	std::vector<float> vTmp(vData.size());
	for(int Pass = 0; Pass < 2; Pass++)
	{
		const std::vector<float> &vIn = Pass == 0 ? vData : vTmp;
		std::vector<float> &vOut = Pass == 0 ? vTmp : vData;
		for(int y = 0; y < H; y++)
			for(int x = 0; x < W; x++)
			{
				float Acc = 0.0f;
				for(int k = -Radius; k <= Radius; k++)
				{
					const int sx = Pass == 0 ? x + k : x, sy = Pass == 0 ? y : y + k;
					if(sx >= 0 && sx < W && sy >= 0 && sy < H)
						Acc += vIn[(size_t)sy * W + sx] * vKernel[k + Radius];
				}
				vOut[(size_t)y * W + x] = Acc;
			}
	}
}

ColorRGBA Mix(const ColorRGBA &a, const ColorRGBA &b, float t)
{
	return ColorRGBA(mix(a.r, b.r, t), mix(a.g, b.g, t), mix(a.b, b.b, t), mix(a.a, b.a, t));
}

// smooth walk through the iridescent tints
ColorRGBA Iris(float t)
{
	static const ColorRGBA s_aStops[] = {FogBrand::IRIS_TEAL, FogBrand::IRIS_LILAC, FogBrand::IRIS_ROSE, FogBrand::MIST};
	t = t - std::floor(t);
	const float f = t * 4.0f;
	const int i = (int)f % 4;
	const float k = f - std::floor(f);
	return Mix(s_aStops[i], s_aStops[(i + 1) % 4], k * k * (3.0f - 2.0f * k));
}

} // namespace

void FogBrand::Init(IGraphics *pGraphics)
{
	if(s_Mark.IsValid())
		return;

	constexpr int MARK = 256;
	std::vector<float> vMark = BuildMark(MARK);
	s_Mark = Upload(pGraphics, vMark, MARK, MARK, "fog_mark");
	Blur(vMark, MARK, MARK, 9.0f);
	s_MarkGlow = Upload(pGraphics, vMark, MARK, MARK, "fog_mark_glow");

	constexpr int BLOB = 128;
	std::vector<float> vBlob((size_t)BLOB * BLOB);
	for(int y = 0; y < BLOB; y++)
		for(int x = 0; x < BLOB; x++)
		{
			const float dx = (x + 0.5f) / BLOB * 2.0f - 1.0f, dy = (y + 0.5f) / BLOB * 2.0f - 1.0f;
			const float r2 = dx * dx + dy * dy;
			vBlob[(size_t)y * BLOB + x] = r2 >= 1.0f ? 0.0f : std::exp(-r2 * 3.0f) * (1.0f - r2);
		}
	s_Blob = Upload(pGraphics, vBlob, BLOB, BLOB, "fog_blob");

	constexpr int SW = 256, SH = 32;
	std::vector<float> vStreak((size_t)SW * SH);
	for(int y = 0; y < SH; y++)
		for(int x = 0; x < SW; x++)
		{
			const float u = (x + 0.5f) / SW, v = (y + 0.5f) / SH * 2.0f - 1.0f;
			vStreak[(size_t)y * SW + x] = std::exp(-v * v * 4.0f) * std::sin(u * pi);
		}
	s_Streak = Upload(pGraphics, vStreak, SW, SH, "fog_streak");

	constexpr int DOT = 32;
	std::vector<float> vDot((size_t)DOT * DOT);
	for(int y = 0; y < DOT; y++)
		for(int x = 0; x < DOT; x++)
		{
			const float dx = (x + 0.5f) / DOT * 2.0f - 1.0f, dy = (y + 0.5f) / DOT * 2.0f - 1.0f;
			vDot[(size_t)y * DOT + x] = std::exp(-(dx * dx + dy * dy) * 5.0f);
		}
	s_Dot = Upload(pGraphics, vDot, DOT, DOT, "fog_dot");
}

static void DrawSoftQuad(IGraphics *pGraphics, IGraphics::CTextureHandle Texture, vec2 Center, vec2 Size, ColorRGBA Color, float Rotation)
{
	pGraphics->TextureSet(Texture);
	pGraphics->QuadsBegin();
	pGraphics->QuadsSetRotation(Rotation);
	pGraphics->SetColor(Color);
	IGraphics::CQuadItem Quad(Center, Size);
	pGraphics->QuadsDraw(&Quad, 1);
	pGraphics->QuadsEnd();
	pGraphics->QuadsSetRotation(0.0f);
}

void FogBrand::DrawMist(IGraphics *pGraphics, vec2 Center, vec2 Size, ColorRGBA Color, float Rotation)
{
	Init(pGraphics);
	DrawSoftQuad(pGraphics, s_Blob, Center, Size, Color, Rotation);
}

void FogBrand::DrawStreak(IGraphics *pGraphics, vec2 Center, vec2 Size, ColorRGBA Color, float Rotation)
{
	Init(pGraphics);
	DrawSoftQuad(pGraphics, s_Streak, Center, Size, Color, Rotation);
}

void FogBrand::DrawMark(IGraphics *pGraphics, vec2 Center, float Size, ColorRGBA Color, float Glow)
{
	Init(pGraphics);
	if(Glow > 0.0f)
	{
		pGraphics->BlendAdditive();
		pGraphics->TextureSet(s_MarkGlow);
		pGraphics->QuadsBegin();
		pGraphics->SetColor(Color.WithMultipliedAlpha(Glow));
		IGraphics::CQuadItem GlowQuad(Center, vec2(Size, Size) * 1.15f);
		pGraphics->QuadsDraw(&GlowQuad, 1);
		pGraphics->QuadsEnd();
		pGraphics->BlendNormal();
	}
	pGraphics->TextureSet(s_Mark);
	pGraphics->QuadsBegin();
	pGraphics->SetColor(Color);
	IGraphics::CQuadItem Quad(Center, vec2(Size, Size));
	pGraphics->QuadsDraw(&Quad, 1);
	pGraphics->QuadsEnd();
}

void FogBrand::DrawLockup(IGraphics *pGraphics, ITextRender *pTextRender, CUi *pUi, const CUIRect &Rect, float Height, float Alpha)
{
	const vec2 MarkCenter(Rect.x + Height * 0.5f, Rect.y + Rect.h * 0.5f);
	DrawMark(pGraphics, MarkCenter, Height * 1.15f, FROST.WithAlpha(Alpha), 0.35f * Alpha);
	CUIRect Word = Rect;
	Word.x += Height * 1.15f;
	Word.w -= Height * 1.15f;
	FogLabel(pUi, pTextRender, Word, "fog", Height * 0.95f, TEXTALIGN_ML, FROST.WithAlpha(Alpha), "Nunito", 900);
}

static void DrawVignette(IGraphics *pGraphics, const CUIRect &Screen, float Strength)
{
	const float W = Screen.w, H = Screen.h;
	pGraphics->TextureClear();
	const ColorRGBA Edge = FogBrand::INK.WithAlpha(Strength), Clear = FogBrand::INK.WithAlpha(0.0f);
	const float Vx = W * 0.22f, Vy = H * 0.25f;
	pGraphics->DrawRect4(Screen.x, Screen.y, Vx, H, Edge, Clear, Edge, Clear, IGraphics::CORNER_NONE, 0.0f);
	pGraphics->DrawRect4(Screen.x + W - Vx, Screen.y, Vx, H, Clear, Edge, Clear, Edge, IGraphics::CORNER_NONE, 0.0f);
	pGraphics->DrawRect4(Screen.x, Screen.y, W, Vy, Edge, Edge, Clear, Clear, IGraphics::CORNER_NONE, 0.0f);
	pGraphics->DrawRect4(Screen.x, Screen.y + H - Vy, W, Vy, Clear, Clear, Edge, Edge, IGraphics::CORNER_NONE, 0.0f);
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

// 2: aurora, long ribbons of teal and lilac light drifting over a night sky
static void DrawAurora(IGraphics *pGraphics, const CUIRect &Screen, float t)
{
	const float W = Screen.w, H = Screen.h;
	pGraphics->TextureClear();
	const ColorRGBA Top(0.05f, 0.07f, 0.10f, 1.0f), Bottom(0.09f, 0.10f, 0.13f, 1.0f);
	pGraphics->DrawRect4(Screen.x, Screen.y, W, H, Top, Top, Bottom, Bottom, IGraphics::CORNER_NONE, 0.0f);
	pGraphics->BlendAdditive();
	pGraphics->TextureSet(s_Streak);
	pGraphics->QuadsBegin();
	for(int Band = 0; Band < 3; Band++)
		for(int i = 0; i < 9; i++)
		{
			const float u = (i + 0.5f) / 9.0f;
			const float Wave = std::sin(u * 5.0f + t * (0.11f + Band * 0.03f) + Band * 1.7f) * H * 0.07f + std::sin(u * 11.0f - t * 0.07f) * H * 0.03f;
			const float y = Screen.y + H * (0.28f + Band * 0.12f) + Wave;
			const float Pulse = 0.55f + 0.45f * std::sin(t * 0.35f + i * 0.8f + Band);
			pGraphics->QuadsSetRotation(std::cos(u * 5.0f + t * 0.1f + Band) * 0.18f);
			pGraphics->SetColor(Iris(0.15f + Band * 0.22f + u * 0.2f + t * 0.01f).WithAlpha(0.13f * Pulse));
			IGraphics::CQuadItem Quad(vec2(Screen.x + u * W, y), vec2(W * 0.32f, H * (0.16f + Band * 0.04f)));
			pGraphics->QuadsDraw(&Quad, 1);
		}
	pGraphics->QuadsEnd();
	pGraphics->QuadsSetRotation(0.0f);
	// a few faint stars
	pGraphics->TextureSet(s_Dot);
	pGraphics->QuadsBegin();
	for(int i = 0; i < 60; i++)
	{
		const float Twinkle = 0.5f + 0.5f * std::sin(t * (0.5f + Hash(i, 21)) + i * 3.1f);
		pGraphics->SetColor(ColorRGBA(0.85f, 0.88f, 0.95f, 0.18f * Twinkle * Hash(i, 22)));
		IGraphics::CQuadItem Quad(vec2(Screen.x + Hash(i, 23) * W, Screen.y + Hash(i, 24) * H * 0.7f), vec2(6.0f, 6.0f));
		pGraphics->QuadsDraw(&Quad, 1);
	}
	pGraphics->QuadsEnd();
	pGraphics->BlendNormal();
	DrawVignette(pGraphics, Screen, 0.55f);
}

// 3: rain on glass, blurred city lights behind slowly sliding drops
static void DrawRain(IGraphics *pGraphics, const CUIRect &Screen, float t)
{
	const float W = Screen.w, H = Screen.h;
	pGraphics->TextureClear();
	pGraphics->DrawRect4(Screen.x, Screen.y, W, H, FogBrand::SLATE, FogBrand::SLATE, FogBrand::INK, FogBrand::INK, IGraphics::CORNER_NONE, 0.0f);
	// bokeh lights far away
	pGraphics->BlendAdditive();
	pGraphics->TextureSet(s_Blob);
	pGraphics->QuadsBegin();
	for(int i = 0; i < 26; i++)
	{
		const float Size = W * (0.05f + Hash(i, 31) * 0.09f);
		const float Breath = 0.6f + 0.4f * std::sin(t * (0.2f + Hash(i, 32) * 0.3f) + i);
		const vec2 Pos(Screen.x + Hash(i, 33) * W + std::sin(t * 0.03f + i) * 10.0f, Screen.y + H * (0.25f + Hash(i, 34) * 0.6f));
		pGraphics->SetColor(Iris(Hash(i, 35)).WithAlpha(0.15f * Breath));
		IGraphics::CQuadItem Quad(Pos, vec2(Size, Size));
		pGraphics->QuadsDraw(&Quad, 1);
	}
	pGraphics->QuadsEnd();
	pGraphics->BlendNormal();
	// drops: a bright head with a thin trail, sliding down at their own pace
	pGraphics->TextureSet(s_Streak);
	pGraphics->QuadsBegin();
	pGraphics->QuadsSetRotation(pi / 2.0f);
	for(int i = 0; i < 55; i++)
	{
		const float Fall = 6.0f + Hash(i, 41) * 30.0f;
		const float Len = 20.0f + Hash(i, 42) * 70.0f;
		const float y = Screen.y - Len + std::fmod(t * Fall + Hash(i, 43) * (H + Len * 2.0f), H + Len * 2.0f);
		const float x = Screen.x + Hash(i, 44) * W;
		pGraphics->SetColor(ColorRGBA(0.80f, 0.85f, 0.92f, 0.10f + 0.10f * Hash(i, 45)));
		IGraphics::CQuadItem Quad(vec2(x, y), vec2(Len, 3.0f + Hash(i, 46) * 3.0f));
		pGraphics->QuadsDraw(&Quad, 1);
	}
	pGraphics->QuadsEnd();
	pGraphics->QuadsSetRotation(0.0f);
	pGraphics->TextureSet(s_Dot);
	pGraphics->QuadsBegin();
	for(int i = 0; i < 90; i++)
	{
		const float Size = 3.0f + Hash(i, 51) * 7.0f;
		pGraphics->SetColor(ColorRGBA(0.85f, 0.89f, 0.95f, 0.06f + 0.08f * Hash(i, 52)));
		IGraphics::CQuadItem Quad(vec2(Screen.x + Hash(i, 53) * W, Screen.y + Hash(i, 54) * H), vec2(Size, Size * 1.15f));
		pGraphics->QuadsDraw(&Quad, 1);
	}
	pGraphics->QuadsEnd();
	DrawVignette(pGraphics, Screen, 0.5f);
}

// 4: the deep, light from above wavering through water, particles rising
static void DrawDeep(IGraphics *pGraphics, const CUIRect &Screen, float t)
{
	const float W = Screen.w, H = Screen.h;
	pGraphics->TextureClear();
	const ColorRGBA Top(0.09f, 0.13f, 0.17f, 1.0f), Bottom(0.03f, 0.04f, 0.06f, 1.0f);
	pGraphics->DrawRect4(Screen.x, Screen.y, W, H, Top, Top, Bottom, Bottom, IGraphics::CORNER_NONE, 0.0f);
	pGraphics->BlendAdditive();
	// god rays swaying from the surface
	pGraphics->TextureSet(s_Streak);
	pGraphics->QuadsBegin();
	for(int i = 0; i < 6; i++)
	{
		pGraphics->QuadsSetRotation(pi / 2.0f + 0.25f + std::sin(t * 0.08f + i * 1.3f) * 0.08f);
		const float x = Screen.x + W * (0.1f + i * 0.16f) + std::sin(t * 0.05f + i) * 30.0f;
		pGraphics->SetColor(ColorRGBA(0.55f, 0.72f, 0.80f, 0.05f + 0.03f * std::sin(t * 0.4f + i * 2.0f)));
		IGraphics::CQuadItem Quad(vec2(x, Screen.y + H * 0.35f), vec2(H * 1.1f, W * 0.07f));
		pGraphics->QuadsDraw(&Quad, 1);
	}
	pGraphics->QuadsEnd();
	pGraphics->QuadsSetRotation(0.0f);
	// caustic shimmer near the top
	pGraphics->TextureSet(s_Blob);
	pGraphics->QuadsBegin();
	for(int i = 0; i < 10; i++)
	{
		const vec2 Pos(Screen.x + W * Hash(i, 61) + std::sin(t * 0.3f + i) * 40.0f, Screen.y + H * 0.08f + std::cos(t * 0.25f + i * 1.3f) * 20.0f);
		pGraphics->SetColor(ColorRGBA(0.60f, 0.78f, 0.86f, 0.05f));
		IGraphics::CQuadItem Quad(Pos, vec2(W * 0.25f, H * 0.1f));
		pGraphics->QuadsDraw(&Quad, 1);
	}
	pGraphics->QuadsEnd();
	// rising particles
	pGraphics->TextureSet(s_Dot);
	pGraphics->QuadsBegin();
	for(int i = 0; i < 110; i++)
	{
		const float Rise = 5.0f + Hash(i, 71) * 18.0f;
		const float y = Screen.y + H + 10.0f - std::fmod(t * Rise + Hash(i, 72) * (H + 20.0f), H + 20.0f);
		const float x = Screen.x + Hash(i, 73) * W + std::sin(t * 0.5f + i) * 10.0f;
		const float Size = 2.0f + Hash(i, 74) * 4.0f;
		pGraphics->SetColor(ColorRGBA(0.70f, 0.85f, 0.92f, 0.08f + 0.16f * Hash(i, 75)));
		IGraphics::CQuadItem Quad(vec2(x, y), vec2(Size, Size) * 2.5f);
		pGraphics->QuadsDraw(&Quad, 1);
	}
	pGraphics->QuadsEnd();
	pGraphics->BlendNormal();
	DrawVignette(pGraphics, Screen, 0.6f);
}

// 5: ember dusk, muted rose and sand haze with slow embers drifting up
static void DrawDusk(IGraphics *pGraphics, const CUIRect &Screen, float t)
{
	const float W = Screen.w, H = Screen.h;
	pGraphics->TextureClear();
	const ColorRGBA Top(0.12f, 0.10f, 0.12f, 1.0f), Bottom(0.07f, 0.06f, 0.07f, 1.0f);
	pGraphics->DrawRect4(Screen.x, Screen.y, W, H, Top, Top, Bottom, Bottom, IGraphics::CORNER_NONE, 0.0f);
	pGraphics->TextureSet(s_Blob);
	pGraphics->QuadsBegin();
	for(int i = 0; i < 6; i++)
	{
		const vec2 Pos(Screen.x + W * (0.5f + std::sin(t * 0.02f * (i + 1) + i * 2.0f) * 0.45f), Screen.y + H * (0.55f + std::cos(t * 0.017f * (i + 1) + i) * 0.35f));
		const float Hue = 0.95f + Hash(i, 81) * 0.12f;
		pGraphics->SetColor(color_cast<ColorRGBA>(ColorHSLA(std::fmod(Hue, 1.0f), 0.30f, 0.45f)).WithAlpha(0.16f));
		IGraphics::CQuadItem Quad(Pos, vec2(W * 0.7f, H * 0.55f));
		pGraphics->QuadsDraw(&Quad, 1);
	}
	pGraphics->QuadsEnd();
	pGraphics->BlendAdditive();
	pGraphics->TextureSet(s_Dot);
	pGraphics->QuadsBegin();
	for(int i = 0; i < 70; i++)
	{
		const float Rise = 8.0f + Hash(i, 91) * 22.0f;
		const float y = Screen.y + H + 10.0f - std::fmod(t * Rise + Hash(i, 92) * (H + 20.0f), H + 20.0f);
		const float x = Screen.x + Hash(i, 93) * W + std::sin(t * 0.7f + i * 1.7f) * 18.0f;
		const float Flicker = 0.5f + 0.5f * std::sin(t * (2.0f + Hash(i, 94) * 3.0f) + i);
		pGraphics->SetColor(ColorRGBA(0.95f, 0.70f, 0.55f, (0.08f + 0.22f * Flicker) * Hash(i, 95)));
		IGraphics::CQuadItem Quad(vec2(x, y), vec2(5.0f, 5.0f) * (1.0f + Hash(i, 96)));
		pGraphics->QuadsDraw(&Quad, 1);
	}
	pGraphics->QuadsEnd();
	pGraphics->BlendNormal();
	DrawVignette(pGraphics, Screen, 0.55f);
}

void FogBrand::DrawBackground(IGraphics *pGraphics, const CUIRect &Screen, float Time)
{
	Init(pGraphics);
	// fog_bg_style: 0 a still frame of the fog, 1 fog, 2 aurora, 3 rain on glass, 4 the deep, 5 ember dusk
	const int Style = g_Config.m_FogBgStyle;
	const float Speed = g_Config.m_FogAnim && Style != 0 ? g_Config.m_FogAnimSpeed / 100.0f : 0.0f;
	const float t = Style == 0 ? 40.0f : Time * Speed;
	switch(Style)
	{
	case 2: DrawAurora(pGraphics, Screen, t); return;
	case 3: DrawRain(pGraphics, Screen, t); return;
	case 4: DrawDeep(pGraphics, Screen, t); return;
	case 5: DrawDusk(pGraphics, Screen, t); return;
	default: break;
	}
	const float W = Screen.w, H = Screen.h;
	const vec2 Mid(Screen.x + W * 0.5f, Screen.y + H * 0.5f);

	// deep graphite base, a touch lighter at the top
	pGraphics->TextureClear();
	pGraphics->DrawRect4(Screen.x, Screen.y, W, H, SLATE, SLATE, INK, INK, IGraphics::CORNER_NONE, 0.0f);

	// slow fields of tinted mist on their own orbits; their tints flow into each other
	pGraphics->TextureSet(s_Blob);
	pGraphics->QuadsBegin();
	for(int i = 0; i < 7; i++)
	{
		const float a = 0.021f + Hash(i, 1) * 0.02f, b = 0.017f + Hash(i, 2) * 0.02f;
		const vec2 Pos = Mid + vec2(std::sin(t * a * 2.0f * pi + Hash(i, 3) * 6.28f) * W * 0.42f, std::cos(t * b * 2.0f * pi + Hash(i, 4) * 6.28f) * H * 0.38f);
		const float Size = W * (0.55f + Hash(i, 5) * 0.45f);
		pGraphics->SetColor(Iris(t * 0.012f + i / 7.0f).WithAlpha(0.17f + Hash(i, 6) * 0.12f));
		IGraphics::CQuadItem Quad(Pos, vec2(Size, Size * 0.72f));
		pGraphics->QuadsDraw(&Quad, 1);
	}
	pGraphics->QuadsEnd();

	// iridescent glints: smaller, faster, additive, so the fog seems to shimmer
	pGraphics->BlendAdditive();
	pGraphics->QuadsBegin();
	for(int i = 0; i < 4; i++)
	{
		const vec2 Pos = Mid + vec2(std::sin(t * (0.05f + i * 0.013f) * 2.0f * pi + i * 1.7f) * W * 0.35f, std::sin(t * (0.037f + i * 0.011f) * 2.0f * pi + i * 2.3f) * H * 0.3f);
		const float Size = W * (0.28f + Hash(i, 9) * 0.2f);
		const float Pulse = 0.5f + 0.5f * std::sin(t * 0.6f + i * 1.9f);
		pGraphics->SetColor(Iris(t * 0.03f + i * 0.27f).WithAlpha(0.06f + 0.07f * Pulse));
		IGraphics::CQuadItem Quad(Pos, vec2(Size, Size * 0.8f));
		pGraphics->QuadsDraw(&Quad, 1);
	}
	pGraphics->QuadsEnd();

	// light refracted through glass: two wide streaks sweeping slowly across
	pGraphics->TextureSet(s_Streak);
	pGraphics->QuadsBegin();
	pGraphics->QuadsSetRotation(-0.38f);
	for(int i = 0; i < 2; i++)
	{
		const float Period = 26.0f + i * 9.0f;
		float Phase = std::fmod(t + i * 11.0f, Period) / Period;
		const float x = Screen.x - W * 0.6f + Phase * W * 2.2f;
		pGraphics->SetColor(Iris(t * 0.02f + 0.5f * i).WithAlpha(0.08f));
		IGraphics::CQuadItem Quad(vec2(x, Mid.y + (i ? H * 0.15f : -H * 0.1f)), vec2(W * 1.5f, H * 0.22f));
		pGraphics->QuadsDraw(&Quad, 1);
	}
	pGraphics->QuadsEnd();
	pGraphics->QuadsSetRotation(0.0f);

	// dust rising through the mist, twinkling
	pGraphics->TextureSet(s_Dot);
	pGraphics->QuadsBegin();
	for(int i = 0; i < 80; i++)
	{
		const float Rise = 4.0f + Hash(i, 11) * 14.0f;
		const float y = Screen.y + H + 10.0f - std::fmod(t * Rise + Hash(i, 12) * (H + 20.0f), H + 20.0f);
		const float x = Screen.x + Hash(i, 13) * W + std::sin(t * 0.3f + i) * 6.0f;
		const float Twinkle = 0.5f + 0.5f * std::sin(t * (0.8f + Hash(i, 14) * 1.5f) + i);
		const float Size = 1.5f + Hash(i, 15) * 2.5f;
		pGraphics->SetColor(ColorRGBA(0.85f, 0.88f, 0.93f, (0.05f + 0.22f * Twinkle) * (0.4f + 0.6f * Hash(i, 16))));
		IGraphics::CQuadItem Quad(vec2(x, y), vec2(Size, Size) * 3.0f);
		pGraphics->QuadsDraw(&Quad, 1);
	}
	pGraphics->QuadsEnd();
	pGraphics->BlendNormal();

	// vignette pulls the eye to the middle
	pGraphics->TextureClear();
	const ColorRGBA Edge = INK.WithAlpha(0.5f), Clear = INK.WithAlpha(0.0f);
	const float Vx = W * 0.22f, Vy = H * 0.25f;
	pGraphics->DrawRect4(Screen.x, Screen.y, Vx, H, Edge, Clear, Edge, Clear, IGraphics::CORNER_NONE, 0.0f);
	pGraphics->DrawRect4(Screen.x + W - Vx, Screen.y, Vx, H, Clear, Edge, Clear, Edge, IGraphics::CORNER_NONE, 0.0f);
	pGraphics->DrawRect4(Screen.x, Screen.y, W, Vy, Edge, Edge, Clear, Clear, IGraphics::CORNER_NONE, 0.0f);
	pGraphics->DrawRect4(Screen.x, Screen.y + H - Vy, W, Vy, Clear, Clear, Edge, Edge, IGraphics::CORNER_NONE, 0.0f);
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}
