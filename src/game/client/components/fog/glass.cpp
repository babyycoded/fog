#include "glass.h"

#include <base/math.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/ui.h>
#include <game/client/ui_rect.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include <engine/image.h>

ColorRGBA FogGlassTint()
{
	return color_cast<ColorRGBA>(ColorHSLA(g_Config.m_FogUiTint, true));
}

void FogDrawGlass(IGraphics *pGraphics, const CUIRect &Rect, float Rounding, float Alpha)
{
	if(Alpha <= 0.0f)
		return;
	const ColorRGBA Tint = FogGlassTint();
	const float Sheen = g_Config.m_FogUiSheen / 100.0f;
	const float x = Rect.x, y = Rect.y, w = Rect.w, h = Rect.h;

	pGraphics->TextureClear();

	// shadow: wide, faint, offset down so the panel floats over the map
	for(int i = 3; i >= 1; i--)
	{
		const float Spread = i * 2.2f;
		pGraphics->DrawRect(x - Spread, y - Spread + 2.5f, w + Spread * 2, h + Spread * 2, ColorRGBA(0.02f, 0.03f, 0.05f, 0.10f * Alpha / i), IGraphics::CORNER_ALL, Rounding + Spread);
	}

	// rim: a hairline that is brighter at the top, where light hits the glass
	const ColorRGBA RimTop(1.0f, 1.0f, 1.0f, (0.10f + 0.16f * Sheen) * Alpha);
	const ColorRGBA RimBottom(1.0f, 1.0f, 1.0f, 0.04f * Alpha);
	pGraphics->DrawRect4(x - 0.75f, y - 0.75f, w + 1.5f, h + 1.5f, RimTop, RimTop, RimBottom, RimBottom, IGraphics::CORNER_ALL, Rounding + 0.75f);

	// body: frosted tint, slightly lighter at the top
	const auto Lift = [](const ColorRGBA &c, float Amount) {
		return ColorRGBA(std::min(c.r + Amount, 1.0f), std::min(c.g + Amount, 1.0f), std::min(c.b + Amount, 1.0f), c.a);
	};
	const ColorRGBA BodyTop = Lift(Tint, 0.05f).WithMultipliedAlpha(Alpha);
	const ColorRGBA BodyBottom = Tint.WithMultipliedAlpha(Alpha * 1.08f);
	pGraphics->DrawRect4(x, y, w, h, BodyTop, BodyTop, BodyBottom, BodyBottom, IGraphics::CORNER_ALL, Rounding);

	// sheen: light pooled on the upper half, the "liquid" curve of the glass
	if(Sheen > 0.0f)
	{
		const ColorRGBA SheenTop(1.0f, 1.0f, 1.0f, 0.09f * Sheen * Alpha);
		const ColorRGBA SheenBottom(1.0f, 1.0f, 1.0f, 0.0f);
		pGraphics->DrawRect4(x + 1.0f, y + 1.0f, w - 2.0f, h * 0.48f, SheenTop, SheenTop, SheenBottom, SheenBottom, IGraphics::CORNER_T, std::max(Rounding - 1.0f, 0.0f));
	}
}

namespace {

IGraphics *GlassGraphics(IGraphics *pGraphics = nullptr)
{
	static IGraphics *s_pGraphics = nullptr;
	if(pGraphics)
		s_pGraphics = pGraphics;
	return s_pGraphics;
}

ColorRGBA MixRgb(const ColorRGBA &a, const ColorRGBA &b, float t)
{
	return ColorRGBA(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a);
}

} // namespace

void FogGlassInit(IGraphics *pGraphics)
{
	GlassGraphics(pGraphics);
}

bool FogMenuRectHook(const CUIRect &Rect, const ColorRGBA &Color, int Corners, float Rounding)
{
	IGraphics *pGraphics = GlassGraphics();
	if(!pGraphics)
		return false;
	if(Color.a <= 0.001f)
		return true;
	// opaque rects carry real content (color picker swatches, previews): never restyle them
	if(Color.a >= 0.97f)
		return false;

	const ColorHSLA Hsl = color_cast<ColorHSLA>(Color);
	const ColorRGBA Tint = FogGlassTint();
	const float Sheen = g_Config.m_FogUiSheen / 100.0f;
	// DDNet encodes states in brightness and alpha (idle panels faint, hover and active brighter)
	const float Strength = std::clamp(Color.a * 1.5f, 0.12f, 1.0f);
	ColorRGBA Body = MixRgb(Tint, ColorRGBA(0.86f, 0.89f, 0.94f, 1.0f), std::clamp(Hsl.l * Color.a * 0.55f, 0.0f, 0.5f));
	// saturated accents (selected rows, warnings) keep a muted trace of their hue
	if(Hsl.s > 0.3f && Hsl.l > 0.15f && Hsl.l < 0.9f)
	{
		const ColorRGBA Muted = color_cast<ColorRGBA>(ColorHSLA(Hsl.h, Hsl.s * 0.45f, std::clamp(Hsl.l, 0.35f, 0.6f), 1.0f));
		Body = MixRgb(Body, Muted, 0.45f);
	}
	Body.a = std::clamp((0.25f + 0.65f * Strength) * (Tint.a / 0.55f), 0.0f, 0.95f);

	const float x = Rect.x, y = Rect.y, w = Rect.w, h = Rect.h;
	pGraphics->TextureClear();

	// tiny rects (scrollbar rails, separators, check marks): flat, no chrome
	if(w < 7.0f || h < 7.0f)
	{
		pGraphics->DrawRect(x, y, w, h, Body.WithAlpha(std::max(Body.a, Color.a)), Corners, Rounding);
		return true;
	}

	// large panels float above the mist
	if(w * h > 9000.0f)
		for(int i = 2; i >= 1; i--)
		{
			const float Spread = i * 3.0f;
			pGraphics->DrawRect(x - Spread, y - Spread + 3.0f, w + Spread * 2, h + Spread * 2, ColorRGBA(0.01f, 0.02f, 0.04f, 0.08f * Strength / i), Corners, Rounding + Spread);
		}

	// rim: light catches the top edge of the glass
	const ColorRGBA RimTop(1.0f, 1.0f, 1.0f, (0.06f + 0.12f * Sheen) * (0.5f + 0.5f * Strength));
	const ColorRGBA RimBottom(1.0f, 1.0f, 1.0f, 0.025f);
	pGraphics->DrawRect4(x - 0.6f, y - 0.6f, w + 1.2f, h + 1.2f, RimTop, RimTop, RimBottom, RimBottom, Corners, Rounding + 0.6f);

	// frosted body, slightly lighter at the top
	const ColorRGBA BodyTop = MixRgb(Body, ColorRGBA(1, 1, 1, 1), 0.05f);
	pGraphics->DrawRect4(x, y, w, h, BodyTop, BodyTop, Body, Body, Corners, Rounding);

	// liquid sheen pooled in the upper part of taller pieces
	if(Sheen > 0.0f && h > 14.0f)
	{
		const ColorRGBA SheenTop(1.0f, 1.0f, 1.0f, 0.06f * Sheen * (0.4f + 0.6f * Strength));
		const ColorRGBA SheenBottom(1.0f, 1.0f, 1.0f, 0.0f);
		const float SheenH = std::min(h * 0.5f, 40.0f);
		pGraphics->DrawRect4(x + 0.5f, y + 0.5f, w - 1.0f, SheenH, SheenTop, SheenTop, SheenBottom, SheenBottom, Corners & IGraphics::CORNER_T, std::max(Rounding - 0.5f, 0.0f));
	}
	return true;
}

void FogDrawMenuAtmosphere(IGraphics *pGraphics, const CUIRect &Screen, float Time)
{
	GlassGraphics(pGraphics);
	static IGraphics::CTextureHandle s_Mist;
	if(!s_Mist.IsValid())
	{
		constexpr int SIZE = 128;
		std::vector<uint8_t> vPixels(SIZE * SIZE * 4);
		for(int y = 0; y < SIZE; y++)
			for(int x = 0; x < SIZE; x++)
			{
				const float dx = (x + 0.5f) / SIZE * 2.0f - 1.0f, dy = (y + 0.5f) / SIZE * 2.0f - 1.0f;
				const float r2 = dx * dx + dy * dy;
				const float v = r2 >= 1.0f ? 0.0f : std::exp(-r2 * 3.0f) * (1.0f - r2);
				uint8_t *p = &vPixels[(y * SIZE + x) * 4];
				p[0] = p[1] = p[2] = 255;
				p[3] = (uint8_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f);
			}
		CImageInfo Image;
		Image.m_Width = SIZE;
		Image.m_Height = SIZE;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		Image.m_pData = vPixels.data();
		s_Mist = pGraphics->LoadTextureRaw(Image, 0, "fog_menu_mist");
		Image.m_pData = nullptr;
	}

	const float Speed = g_Config.m_FogAnim ? g_Config.m_FogAnimSpeed / 100.0f : 0.0f;
	const float t = Time * Speed;
	const float W = Screen.w, H = Screen.h;

	// cold, desaturating wash that pulls any menu theme toward grey
	pGraphics->TextureClear();
	const ColorRGBA Top(0.11f, 0.13f, 0.16f, 0.50f), Bottom(0.07f, 0.08f, 0.10f, 0.72f);
	pGraphics->DrawRect4(Screen.x, Screen.y, W, H, Top, Top, Bottom, Bottom, IGraphics::CORNER_NONE, 0.0f);

	// fog banks drifting slowly across
	pGraphics->TextureSet(s_Mist);
	pGraphics->QuadsBegin();
	struct SBank
	{
		float m_Y, m_Size, m_Speed, m_Phase, m_Alpha;
	};
	static const SBank s_aBanks[] = {
		{0.78f, 1.10f, 0.012f, 0.10f, 0.20f},
		{0.62f, 0.85f, -0.008f, 0.55f, 0.14f},
		{0.90f, 1.40f, 0.006f, 0.30f, 0.22f},
		{0.35f, 0.75f, 0.010f, 0.80f, 0.08f},
		{0.18f, 0.95f, -0.005f, 0.40f, 0.06f},
	};
	for(const SBank &Bank : s_aBanks)
	{
		const float Span = W * 1.6f;
		float Px = std::fmod(Bank.m_Phase * Span + t * Bank.m_Speed * W, Span);
		if(Px < 0.0f)
			Px += Span;
		Px -= W * 0.3f;
		const float Py = H * Bank.m_Y + std::sin(t * 0.07f + Bank.m_Phase * 6.0f) * H * 0.02f;
		pGraphics->SetColor(ColorRGBA(0.80f, 0.84f, 0.90f, Bank.m_Alpha));
		IGraphics::CQuadItem Quad(vec2(Px, Py), vec2(W * Bank.m_Size, H * Bank.m_Size * 0.45f));
		pGraphics->QuadsDraw(&Quad, 1);
	}
	pGraphics->QuadsEnd();
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void FogLabel(CUi *pUi, ITextRender *pTextRender, const CUIRect &Rect, const char *pText, float Size, int Align, ColorRGBA Color, const char *pFamily, int Weight)
{
	pTextRender->SetDisplayFont(pFamily, Weight);
	pTextRender->SetFontPreset(EFontPreset::DISPLAY_FONT);
	pTextRender->TextColor(Color);
	// brand type is clean: no dark game outline
	const ColorRGBA OldOutline = pTextRender->GetTextOutlineColor();
	pTextRender->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.0f);
	SLabelProperties Props;
	Props.m_MaxWidth = Rect.w;
	Props.m_EllipsisAtEnd = true;
	pUi->DoLabel(&Rect, pText, Size, Align, Props);
	pTextRender->TextOutlineColor(OldOutline);
	pTextRender->TextColor(pTextRender->DefaultTextColor());
	pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
}

bool FogMenusActive()
{
	return CUIRect::ms_pfnDrawHook != nullptr;
}

void FogSpring(float &X, float &V, float Target, float Dt, float Freq, float Bounce)
{
	if(!g_Config.m_FogAnim)
	{
		X = Target;
		V = 0.0f;
		return;
	}
	const float Speed = std::clamp(g_Config.m_FogAnimSpeed / 100.0f, 0.1f, 10.0f);
	const float Omega = 2.0f * pi * Freq * Speed;
	const float Zeta = std::clamp(1.0f - Bounce * std::clamp(g_Config.m_FogAnimIntensity / 100.0f, 0.0f, 2.0f), 0.2f, 1.0f);
	// fixed substeps keep the spring stable at any frame rate (4000 fps or a 100 ms hitch)
	Dt = std::min(Dt, 0.1f);
	const float Step = 1.0f / 480.0f;
	while(Dt > 0.0f)
	{
		const float h = std::min(Dt, Step);
		const float Accel = -Omega * Omega * (X - Target) - 2.0f * Zeta * Omega * V;
		V += Accel * h;
		X += V * h;
		Dt -= h;
	}
	if(std::abs(X - Target) < 0.001f && std::abs(V) < 0.01f)
	{
		X = Target;
		V = 0.0f;
	}
}

float FogEaseOutBack(float T)
{
	T = std::clamp(T, 0.0f, 1.0f);
	const float Over = 1.2f * std::clamp(g_Config.m_FogAnimIntensity / 100.0f, 0.0f, 2.0f);
	const float u = T - 1.0f;
	return 1.0f + (Over + 1.0f) * u * u * u + Over * u * u;
}

void FogDrawAlbumArt(IGraphics *pGraphics, IGraphics::CTextureHandle Texture, const CUIRect &Rect, float Alpha)
{
	pGraphics->TextureClear();
	pGraphics->DrawRect(Rect.x - 1.0f, Rect.y + 1.5f, Rect.w + 2.0f, Rect.h + 1.5f, ColorRGBA(0.0f, 0.01f, 0.03f, 0.30f * Alpha), IGraphics::CORNER_ALL, 4.0f);
	pGraphics->WrapClamp();
	pGraphics->TextureSet(Texture);
	pGraphics->QuadsBegin();
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, Alpha);
	IGraphics::CQuadItem Quad(Rect.x, Rect.y, Rect.w, Rect.h);
	pGraphics->QuadsDrawTL(&Quad, 1);
	pGraphics->QuadsEnd();
	pGraphics->WrapNormal();
	pGraphics->TextureClear();
	// a thin frosted rim so the cover sits in the glass
	const ColorRGBA Rim(1.0f, 1.0f, 1.0f, 0.18f * Alpha);
	pGraphics->DrawRect(Rect.x, Rect.y, Rect.w, 0.6f, Rim, IGraphics::CORNER_NONE, 0.0f);
	pGraphics->DrawRect(Rect.x, Rect.y + Rect.h - 0.6f, Rect.w, 0.6f, Rim, IGraphics::CORNER_NONE, 0.0f);
	pGraphics->DrawRect(Rect.x, Rect.y, 0.6f, Rect.h, Rim, IGraphics::CORNER_NONE, 0.0f);
	pGraphics->DrawRect(Rect.x + Rect.w - 0.6f, Rect.y, 0.6f, Rect.h, Rim, IGraphics::CORNER_NONE, 0.0f);
}
