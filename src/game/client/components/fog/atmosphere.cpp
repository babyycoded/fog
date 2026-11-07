#include "atmosphere.h"
#include "fx.h"

#include "brand.h"

#include <base/math.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <cmath>

namespace {

float Hash(int x, int y, int k)
{
	unsigned h = (unsigned)x * 73856093u ^ (unsigned)y * 19349663u ^ (unsigned)k * 83492791u;
	h ^= h >> 13;
	h *= 0x5bd1e995u;
	h ^= h >> 15;
	return (h & 0xffffff) / (float)0x1000000;
}

struct SLayer
{
	float m_Parallax; // percent
	float m_Alpha;
	float m_Scale;
	float m_Wind;
};

} // namespace

void CFogAtmosphere::OnRender()
{
	if(!g_Config.m_FogAtmo || (m_Front && !g_Config.m_FogAtmoFront))
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	const float Density = g_Config.m_FogAtmoDensity / 100.0f * (1.0f + 0.3f * FogMusicPulse());
	const float Wind = g_Config.m_FogAtmoWind / 100.0f;
	const float Time = Client()->LocalTime();
	const vec2 Center = GameClient()->m_Camera.m_Center;
	const float Zoom = GameClient()->m_Camera.m_Zoom;
	const vec2 PlayerPos = GameClient()->m_LocalCharacterPos;
	const ColorRGBA MistColor(0.72f, 0.77f, 0.84f, 1.0f);

	static const SLayer s_aBack[] = {{68.0f, 0.11f, 1.15f, 0.7f}, {86.0f, 0.08f, 0.85f, 1.0f}};
	static const SLayer s_aFront[] = {{116.0f, 0.06f, 0.75f, 1.4f}};
	const SLayer *pLayers = m_Front ? s_aFront : s_aBack;
	const int NumLayers = m_Front ? (int)std::size(s_aFront) : (int)std::size(s_aBack);

	const CScreenRect OldScreen = Graphics()->GetScreen();
	for(int l = 0; l < NumLayers; l++)
	{
		const SLayer &Layer = pLayers[l];
		const CScreenRect Rect = Graphics()->MapScreenToWorld(Center.x, Center.y, Layer.m_Parallax, Layer.m_Parallax, 100.0f, 0.0f, 0.0f, Graphics()->ScreenAspect(), Zoom);
		Graphics()->MapScreen(Rect);
		const float RX = Rect.m_TopLeft.x, RY = Rect.m_TopLeft.y, RW = Rect.m_BottomRight.x - RX, RH = Rect.m_BottomRight.y - RY;

		// an endless grid of cells around the camera; each cell holds two banks of mist
		const float Cell = 760.0f * Layer.m_Scale;
		const float Drift = Time * Wind * 22.0f * Layer.m_Wind;
		const float Margin = Cell * 1.2f;
		const int X0 = (int)std::floor((RX - Margin - Drift) / Cell), X1 = (int)std::floor((RX + RW + Margin - Drift) / Cell);
		const int Y0 = (int)std::floor((RY - Margin) / Cell), Y1 = (int)std::floor((RY + RH + Margin) / Cell);
		// the player's position as seen on this layer
		const vec2 LayerPlayer = PlayerPos * (Layer.m_Parallax / 100.0f) + (vec2(RX + RW / 2.0f, RY + RH / 2.0f) - Center * (Layer.m_Parallax / 100.0f));
		for(int cy = Y0; cy <= Y1; cy++)
			for(int cx = X0; cx <= X1; cx++)
				for(int k = 0; k < 2; k++)
				{
					const float h1 = Hash(cx, cy, k * 7 + l), h2 = Hash(cx, cy, k * 7 + l + 31), h3 = Hash(cx, cy, k * 7 + l + 63);
					const vec2 Pos((cx + h1) * Cell + Drift, (cy + h2) * Cell + std::sin(Time * 0.09f + h3 * 6.28f) * 30.0f);
					const vec2 Size(Cell * (1.1f + h3 * 0.9f), Cell * (0.55f + h1 * 0.35f));
					float Alpha = Layer.m_Alpha * Density * (0.6f + 0.4f * std::sin(Time * 0.07f + h2 * 6.28f));
					if(m_Front)
					{
						// keep a clear window around your own tee
						const float d = distance(Pos, LayerPlayer) / (Size.x * 0.5f);
						Alpha *= std::clamp((d - 0.35f) / 0.9f, 0.0f, 1.0f);
					}
					if(Alpha <= 0.003f)
						continue;
					FogBrand::DrawMist(Graphics(), Pos, Size, MistColor.WithAlpha(std::min(Alpha, 0.5f)), (h1 - 0.5f) * 0.4f);
				}
	}
	Graphics()->MapScreen(OldScreen);
}
