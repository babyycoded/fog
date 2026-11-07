#include "cursor.h"

#include "brand.h"

#include <base/color.h>
#include <base/math.h>
#include <base/str.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <cmath>

bool FogUseCrosshair()
{
	if(!g_Config.m_FogCrosshair)
		return false;
	if(g_Config.m_FogCrosshairOverride)
		return true;
	// without override, a custom game skin keeps its own crosshair files
	return str_comp(g_Config.m_ClAssetGame, "default") == 0 || str_comp(g_Config.m_ClAssetGame, "fog") == 0;
}

void FogDrawCrosshair(IGraphics *pGraphics, vec2 Pos, float Alpha, float Speed)
{
	const ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_FogCrosshairColor)).WithAlpha(g_Config.m_FogCrosshairAlpha / 100.0f * Alpha);
	const ColorRGBA Outline(0.0f, 0.02f, 0.05f, 0.55f * Color.a);
	const float Size = (float)g_Config.m_FogCrosshairSize;
	const float T = (float)g_Config.m_FogCrosshairThickness;
	// fast movement opens the cross a little, like a spread indicator
	const float Open = g_Config.m_FogCrosshairDynamic ? std::min(Speed / 1200.0f, 1.0f) * Size * 0.5f : 0.0f;
	const float Gap = g_Config.m_FogCrosshairGap + Open;
	const int Shape = g_Config.m_FogCrosshairShape;
	const bool Outlined = g_Config.m_FogCrosshairOutline != 0;

	pGraphics->TextureClear();
	const auto Rect = [&](float x, float y, float w, float h, float Round) {
		if(Outlined)
			pGraphics->DrawRect(x - 1.0f, y - 1.0f, w + 2.0f, h + 2.0f, Outline, IGraphics::CORNER_ALL, Round + 1.0f);
		pGraphics->DrawRect(x, y, w, h, Color, IGraphics::CORNER_ALL, Round);
	};
	const auto Dot = [&]() {
		const float D = std::max(T * 1.6f, 2.5f);
		Rect(Pos.x - D / 2.0f, Pos.y - D / 2.0f, D, D, D / 2.0f);
	};
	const auto Cross = [&](bool Top) {
		const float L = Size - Gap > 1.0f ? Size - Gap : 1.0f;
		Rect(Pos.x - Gap - L, Pos.y - T / 2.0f, L, T, T / 2.0f); // left
		Rect(Pos.x + Gap, Pos.y - T / 2.0f, L, T, T / 2.0f); // right
		Rect(Pos.x - T / 2.0f, Pos.y + Gap, T, L, T / 2.0f); // bottom
		if(Top)
			Rect(Pos.x - T / 2.0f, Pos.y - Gap - L, T, L, T / 2.0f);
	};
	const auto Ring = [&]() {
		const float R = Size * 0.75f + Open;
		const int Segments = 28;
		const float SegLen = 2.0f * pi * R / Segments * 1.15f;
		for(int Pass = Outlined ? 0 : 1; Pass < 2; Pass++)
		{
			pGraphics->QuadsBegin();
			pGraphics->SetColor(Pass == 0 ? Outline : Color);
			for(int i = 0; i < Segments; i++)
			{
				const float A = i * 2.0f * pi / Segments;
				pGraphics->QuadsSetRotation(A + pi / 2.0f);
				IGraphics::CQuadItem Quad(Pos.x + std::cos(A) * R, Pos.y + std::sin(A) * R, SegLen + (Pass == 0 ? 2.0f : 0.0f), T + (Pass == 0 ? 2.0f : 0.0f));
				pGraphics->QuadsDraw(&Quad, 1);
			}
			pGraphics->QuadsEnd();
		}
		pGraphics->QuadsSetRotation(0.0f);
	};
	const auto Chevron = [&]() {
		const float L = Size * 0.8f;
		for(int Pass = Outlined ? 0 : 1; Pass < 2; Pass++)
		{
			pGraphics->QuadsBegin();
			pGraphics->SetColor(Pass == 0 ? Outline : Color);
			for(int Side = -1; Side <= 1; Side += 2)
			{
				const float A = Side * pi / 4.0f;
				pGraphics->QuadsSetRotation(-A);
				IGraphics::CQuadItem Quad(Pos.x + Side * L * 0.35f, Pos.y + Gap * 0.5f + L * 0.35f, L + (Pass == 0 ? 2.0f : 0.0f), T + (Pass == 0 ? 2.0f : 0.0f));
				pGraphics->QuadsDraw(&Quad, 1);
			}
			pGraphics->QuadsEnd();
		}
		pGraphics->QuadsSetRotation(0.0f);
	};

	switch(Shape)
	{
	case 0: Cross(true); break;
	case 1: Dot(); break;
	case 2:
		Cross(true);
		Dot();
		break;
	case 3: Ring(); break;
	case 4:
		Ring();
		Dot();
		break;
	case 5: Cross(false); break;
	default:
		Chevron();
		Dot();
		break;
	}
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

bool FogDrawMenuCursor(CGameClient *pClient, IGraphics *pGraphics, vec2 Pos, float Size)
{
	const int Mode = g_Config.m_FogCursor;
	if(Mode == 0)
		return false;
	Size *= g_Config.m_FogCursorSize / 100.0f;
	if(Mode == 1)
	{
		// the hook head from the game skin, tip up and to the left like an arrow
		const vec2 Dir(-0.70710678f, -0.70710678f);
		const float W = Size * 1.1f, H = Size * 0.55f;
		const vec2 Center = Pos - Dir * (W * 0.42f);
		pGraphics->WrapClamp();
		pGraphics->TextureSet(pClient->m_GameSkin.m_SpriteHookHead);
		pGraphics->QuadsBegin();
		pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
		pGraphics->QuadsSetRotation(pi * 1.25f);
		IGraphics::CQuadItem Quad(Center.x, Center.y, W, H);
		pGraphics->QuadsDraw(&Quad, 1);
		pGraphics->QuadsEnd();
		pGraphics->QuadsSetRotation(0.0f);
		pGraphics->WrapNormal();
		return true;
	}
	if(Mode == 2)
	{
		FogBrand::DrawMark(pGraphics, Pos + vec2(Size * 0.3f, Size * 0.3f), Size * 0.9f, ColorRGBA(0.93f, 0.95f, 0.97f, 1.0f), 0.5f);
		return true;
	}
	pGraphics->TextureClear();
	const float D = Size * 0.35f;
	pGraphics->DrawRect(Pos.x - D / 2.0f - 1.0f, Pos.y - D / 2.0f - 1.0f, D + 2.0f, D + 2.0f, ColorRGBA(0.0f, 0.02f, 0.05f, 0.5f), IGraphics::CORNER_ALL, D / 2.0f + 1.0f);
	pGraphics->DrawRect(Pos.x - D / 2.0f, Pos.y - D / 2.0f, D, D, ColorRGBA(0.93f, 0.95f, 0.97f, 1.0f), IGraphics::CORNER_ALL, D / 2.0f);
	return true;
}
