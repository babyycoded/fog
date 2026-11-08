#include "lighting.h"

#include "accessories.h"

#include <base/math.h>

#include <engine/image.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/client/gameclient.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <array>
#include <vector>

namespace {

constexpr float TILE = 32.0f;

// CCollision clamps outside coordinates to the solid border, so outside the map is air here
bool Solid(const CCollision *pCollision, float x, float y)
{
	if(x < 0.0f || y < 0.0f || x >= pCollision->GetWidth() * TILE || y >= pCollision->GetHeight() * TILE)
		return false;
	return pCollision->CheckPoint(x, y);
}

void ClipWorld(IGraphics *pGraphics, const CScreenRect &Screen, float L, float T, float R, float B)
{
	const float W = Screen.m_BottomRight.x - Screen.m_TopLeft.x, H = Screen.m_BottomRight.y - Screen.m_TopLeft.y;
	const int x0 = std::clamp((int)std::floor((L - Screen.m_TopLeft.x) / W * pGraphics->ScreenWidth()), 0, pGraphics->ScreenWidth());
	const int x1 = std::clamp((int)std::ceil((R - Screen.m_TopLeft.x) / W * pGraphics->ScreenWidth()), 0, pGraphics->ScreenWidth());
	const int y0 = std::clamp((int)std::floor((T - Screen.m_TopLeft.y) / H * pGraphics->ScreenHeight()), 0, pGraphics->ScreenHeight());
	const int y1 = std::clamp((int)std::ceil((B - Screen.m_TopLeft.y) / H * pGraphics->ScreenHeight()), 0, pGraphics->ScreenHeight());
	pGraphics->ClipEnable(x0, y0, std::max(x1 - x0, 0), std::max(y1 - y0, 0));
}

struct SLight
{
	vec2 m_Pos;
	float m_Radius;
	ColorRGBA m_Color;
	float m_Inner = 0.0f; // no light inside this (a tee is not lit by its own light)
};

// the direction the light comes from (soft, from the upper left): rim lights and shadows agree
const vec2 LIGHT_FROM = normalize(vec2(-0.55f, -1.0f));

} // namespace

void CFogLighting::OnInit()
{
	CImageInfo Image;
	Image.m_Width = Image.m_Height = 64;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.Allocate();
	for(int y = 0; y < 64; y++)
		for(int x = 0; x < 64; x++)
		{
			const float d = std::sqrt((x + 0.5f - 32.0f) * (x + 0.5f - 32.0f) + (y + 0.5f - 32.0f) * (y + 0.5f - 32.0f)) / 32.0f;
			const float t = std::clamp(1.0f - d, 0.0f, 1.0f);
			uint8_t *p = &Image.m_pData[((size_t)y * 64 + x) * 4];
			p[0] = p[1] = p[2] = 255;
			p[3] = (uint8_t)std::lround(t * t * 255.0f);
		}
	m_Soft = Graphics()->LoadTextureRawMove(Image, 0, "fog_light_soft");
}

void CFogLighting::OnShutdown()
{
	Graphics()->UnloadTexture(&m_Soft);
}

// ---------------------------------------------------------------- behind the tees

void CFogLighting::RenderAmbientOcclusion()
{
	const CScreenRect Screen = Graphics()->GetScreen();
	const CCollision *pCol = Collision();
	const int x0 = std::max(0, (int)std::floor(Screen.m_TopLeft.x / TILE) - 1), x1 = std::min(pCol->GetWidth() - 1, (int)std::ceil(Screen.m_BottomRight.x / TILE) + 1);
	const int y0 = std::max(0, (int)std::floor(Screen.m_TopLeft.y / TILE) - 1), y1 = std::min(pCol->GetHeight() - 1, (int)std::ceil(Screen.m_BottomRight.y / TILE) + 1);
	const float a = 0.30f * g_Config.m_FogAoStrength / 100.0f;
	const float Reach = 16.0f;
	const ColorRGBA Dark(0.0f, 0.01f, 0.03f, a), Clear(0.0f, 0.01f, 0.03f, 0.0f);
	const auto IsSolid = [&](int tx, int ty) { return tx >= 0 && ty >= 0 && tx < pCol->GetWidth() && ty < pCol->GetHeight() && pCol->CheckPoint(tx * TILE + 16.0f, ty * TILE + 16.0f); };
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	for(int ty = y0; ty <= y1; ty++)
		for(int tx = x0; tx <= x1; tx++)
		{
			if(IsSolid(tx, ty))
				continue;
			// this air tile: shade the sides that touch a wall
			const float X = tx * TILE, Y = ty * TILE;
			if(IsSolid(tx, ty + 1)) // floor below
			{
				Graphics()->SetColor4(Clear, Clear, Dark, Dark);
				IGraphics::CQuadItem q(X, Y + TILE - Reach, TILE, Reach);
				Graphics()->QuadsDrawTL(&q, 1);
			}
			if(IsSolid(tx, ty - 1)) // ceiling above
			{
				Graphics()->SetColor4(Dark, Dark, Clear, Clear);
				IGraphics::CQuadItem q(X, Y, TILE, Reach);
				Graphics()->QuadsDrawTL(&q, 1);
			}
			if(IsSolid(tx - 1, ty)) // wall left
			{
				Graphics()->SetColor4(Dark, Clear, Dark, Clear);
				IGraphics::CQuadItem q(X, Y, Reach, TILE);
				Graphics()->QuadsDrawTL(&q, 1);
			}
			if(IsSolid(tx + 1, ty)) // wall right
			{
				Graphics()->SetColor4(Clear, Dark, Clear, Dark);
				IGraphics::CQuadItem q(X + TILE - Reach, Y, Reach, TILE);
				Graphics()->QuadsDrawTL(&q, 1);
			}
		}
	Graphics()->QuadsEnd();
}

void CFogLighting::RenderGodRays()
{
	const CScreenRect Screen = Graphics()->GetScreen();
	const CCollision *pCol = Collision();
	const float W = Screen.m_BottomRight.x - Screen.m_TopLeft.x, H = Screen.m_BottomRight.y - Screen.m_TopLeft.y;
	const float Now = Client()->GlobalTime();
	const vec2 Dir = -LIGHT_FROM; // falling down and to the right
	const float s = g_Config.m_FogGodRaysStrength / 100.0f;
	Graphics()->BlendAdditive();
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	constexpr int SHAFTS = 16;
	for(int k = 0; k < SHAFTS; k++)
	{
		// shafts start along the top edge (and a little to the left, they lean right)
		const float u = (k + 0.5f) / SHAFTS + 0.02f * std::sin(Now * 0.13f + k * 1.7f);
		const vec2 Start(Screen.m_TopLeft.x - H * 0.5f + u * (W + H * 0.5f), Screen.m_TopLeft.y);
		if(Solid(pCol, Start.x, Start.y))
			continue;
		float Len = 0.0f;
		const float MaxLen = H * 1.3f;
		while(Len < MaxLen && !Solid(pCol, Start.x + Dir.x * Len, Start.y + Dir.y * Len))
			Len += 12.0f;
		if(Len < 60.0f)
			continue;
		const float Flicker = 0.55f + 0.45f * std::sin(Now * 0.4f + k * 2.3f);
		const float Width = (22.0f + 26.0f * std::fmod(k * 0.618f, 1.0f)) * (0.8f + 0.2f * Flicker);
		const vec2 Side(-Dir.y, Dir.x), End = Start + Dir * Len;
		// bright along the middle, fading to nothing at both sides and towards the end
		const ColorRGBA Core(1.0f, 0.94f, 0.82f, 0.08f * s * Flicker), Edge(1.0f, 0.94f, 0.82f, 0.0f);
		const vec2 TopL = Start - Side * Width * 0.5f, TopR = Start + Side * Width * 0.5f;
		const vec2 EndL = End - Side * Width * 0.9f, EndR = End + Side * Width * 0.9f;
		const vec2 TopM = (TopL + TopR) * 0.5f, EndM = (EndL + EndR) * 0.5f;
		Graphics()->SetColor4(Edge, Core, Edge, Edge);
		const IGraphics::CFreeformItem Left(TopL, TopM, EndL, EndM);
		Graphics()->QuadsDrawFreeform(&Left, 1);
		Graphics()->SetColor4(Core, Edge, Edge, Edge);
		const IGraphics::CFreeformItem Right(TopM, TopR, EndM, EndR);
		Graphics()->QuadsDrawFreeform(&Right, 1);
	}
	Graphics()->QuadsEnd();
	Graphics()->BlendNormal();
}

void CFogLighting::RenderRimLight()
{
	// a white copy of each tee a hair towards the light, behind the tee: only the lit edge shows
	const vec2 Shift = LIGHT_FROM * 1.8f;
	Graphics()->BlendAdditive();
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!GameClient()->m_Snap.m_aCharacters[i].m_Active)
			continue;
		GameClient()->m_FogAccessories.DrawPlayerSilhouette(i, GameClient()->m_aClients[i].m_RenderPos, 64.0f, Shift, ColorRGBA(0.85f, 0.9f, 1.0f, 0.32f));
	}
	Graphics()->BlendNormal();
}

// ---------------------------------------------------------------- over the map

void CFogLighting::RenderLights()
{
	const CScreenRect Screen = Graphics()->GetScreen();
	const CCollision *pCol = Collision();
	const float RadiusScale = g_Config.m_FogLightRadius / 100.0f;
	const float Strength = 0.24f * g_Config.m_FogLightStrength / 100.0f;

	// collect the lights in view
	std::vector<SLight> vLights;
	const auto InView = [&](vec2 p, float r) { return p.x + r > Screen.m_TopLeft.x && p.x - r < Screen.m_BottomRight.x && p.y + r > Screen.m_TopLeft.y && p.y - r < Screen.m_BottomRight.y; };
	const int Local = GameClient()->m_Snap.m_LocalClientId;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!GameClient()->m_Snap.m_aCharacters[i].m_Active)
			continue;
		const vec2 p = GameClient()->m_aClients[i].m_RenderPos;
		const float r = (i == Local ? 300.0f : 220.0f) * RadiusScale;
		if(InView(p, r) && vLights.size() < 24)
			vLights.push_back({p, r, i == Local ? ColorRGBA(0.85f, 0.9f, 1.0f, 1.0f) : ColorRGBA(0.75f, 0.8f, 0.92f, 0.8f), 17.0f});
	}
	const int NumItems = Client()->SnapNumItems(IClient::SNAP_CURRENT);
	for(int i = 0; i < NumItems && vLights.size() < 48; i++)
	{
		const IClient::CSnapItem Item = Client()->SnapGetItem(IClient::SNAP_CURRENT, i);
		vec2 From, To;
		bool Laser = false;
		if(Item.m_Type == NETOBJTYPE_LASER)
		{
			const CNetObj_Laser *p = (const CNetObj_Laser *)Item.m_pData;
			From = vec2(p->m_FromX, p->m_FromY);
			To = vec2(p->m_X, p->m_Y);
			Laser = true;
		}
		else if(Item.m_Type == NETOBJTYPE_DDNETLASER)
		{
			const CNetObj_DDNetLaser *p = (const CNetObj_DDNetLaser *)Item.m_pData;
			From = vec2(p->m_FromX, p->m_FromY);
			To = vec2(p->m_ToX, p->m_ToY);
			Laser = true;
		}
		else if(Item.m_Type == NETOBJTYPE_PICKUP)
		{
			const CNetObj_Pickup *p = (const CNetObj_Pickup *)Item.m_pData;
			const vec2 Pos(p->m_X, p->m_Y);
			ColorRGBA c(0.9f, 0.9f, 1.0f, 0.7f);
			if(p->m_Type == POWERUP_HEALTH)
				c = ColorRGBA(1.0f, 0.45f, 0.45f, 0.8f);
			else if(p->m_Type == POWERUP_ARMOR)
				c = ColorRGBA(1.0f, 0.85f, 0.4f, 0.8f);
			else if(p->m_Type == POWERUP_NINJA)
				c = ColorRGBA(0.75f, 0.5f, 1.0f, 0.8f);
			if(InView(Pos, 140.0f))
				vLights.push_back({Pos, 120.0f * RadiusScale, c});
		}
		if(Laser)
		{
			// a laser lights along its length
			const float Len = distance(From, To);
			const int Steps = std::clamp((int)(Len / 110.0f), 1, 6);
			for(int s = 0; s <= Steps && vLights.size() < 48; s++)
			{
				const vec2 Pos = mix(From, To, s / (float)Steps);
				if(InView(Pos, 160.0f))
					vLights.push_back({Pos, 150.0f * RadiusScale, ColorRGBA(0.55f, 0.75f, 1.0f, 0.9f)});
			}
		}
	}

	// the world gets darker where no light reaches...
	const float Ambient = g_Config.m_FogLightAmbient / 100.0f;
	Graphics()->TextureClear();
	Graphics()->DrawRect(Screen.m_TopLeft.x, Screen.m_TopLeft.y, Screen.m_BottomRight.x - Screen.m_TopLeft.x, Screen.m_BottomRight.y - Screen.m_TopLeft.y, ColorRGBA(0.01f, 0.015f, 0.03f, Ambient), IGraphics::CORNER_NONE, 0.0f);

	// ...and each light fills what it can see: rays stop at tiles, so walls cast real shadows
	Graphics()->BlendAdditive();
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	constexpr int RAYS = 120;
	for(const SLight &Light : vLights)
	{
		std::array<float, RAYS> aDist;
		for(int r = 0; r < RAYS; r++)
		{
			const float a = r * 2.0f * pi / RAYS;
			const vec2 d(std::cos(a), std::sin(a));
			float t = 4.0f;
			while(t < Light.m_Radius && !Solid(pCol, Light.m_Pos.x + d.x * t, Light.m_Pos.y + d.y * t))
				t += 6.0f;
			// light a few units into the wall face so edges catch the light
			aDist[r] = std::min(t + 3.0f, Light.m_Radius);
		}
		const float I = Strength * Light.m_Color.a;
		const auto Fall = [&](float d) {
			const float f = std::clamp(1.0f - d / Light.m_Radius, 0.0f, 1.0f);
			return f * f;
		};
		const ColorRGBA Base(Light.m_Color.r * I, Light.m_Color.g * I, Light.m_Color.b * I, 1.0f);
		const auto Col = [&](float k) { return ColorRGBA(Base.r * k, Base.g * k, Base.b * k, 1.0f); };
		for(int r = 0; r < RAYS; r++)
		{
			const int n = (r + 1) % RAYS;
			const float a0 = r * 2.0f * pi / RAYS, a1 = n * 2.0f * pi / RAYS;
			const vec2 d0(std::cos(a0), std::sin(a0)), d1(std::cos(a1), std::sin(a1));
			const float i0 = std::min(Light.m_Inner, aDist[r]), i1 = std::min(Light.m_Inner, aDist[n]);
			const float m0 = (aDist[r] + i0) * 0.5f, m1 = (aDist[n] + i1) * 0.5f;
			// inner half and outer half, so the falloff stays round instead of linear; the light
			// starts at the edge of its source
			Graphics()->SetColor4(Col(Fall(i0)), Col(Fall(i1)), Col(Fall(m0)), Col(Fall(m1)));
			const IGraphics::CFreeformItem Inner(Light.m_Pos + d0 * i0, Light.m_Pos + d1 * i1, Light.m_Pos + d0 * m0, Light.m_Pos + d1 * m1);
			Graphics()->QuadsDrawFreeform(&Inner, 1);
			Graphics()->SetColor4(Col(Fall(m0)), Col(Fall(m1)), Col(Fall(aDist[r])), Col(Fall(aDist[n])));
			const IGraphics::CFreeformItem Outer(Light.m_Pos + d0 * m0, Light.m_Pos + d1 * m1, Light.m_Pos + d0 * aDist[r], Light.m_Pos + d1 * aDist[n]);
			Graphics()->QuadsDrawFreeform(&Outer, 1);
		}
	}
	Graphics()->QuadsEnd();
	Graphics()->BlendNormal();
}

void CFogLighting::RenderWallShadows()
{
	// tees close to a wall darken it softly on the side away from the light
	const CScreenRect Screen = Graphics()->GetScreen();
	const CCollision *pCol = Collision();
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!GameClient()->m_Snap.m_aCharacters[i].m_Active)
			continue;
		const vec2 Pos = GameClient()->m_aClients[i].m_RenderPos;
		for(int Side = -1; Side <= 1; Side += 2)
		{
			float Dist = -1.0f;
			for(float d = 14.0f; d <= 70.0f; d += 4.0f)
				if(Solid(pCol, Pos.x + Side * d, Pos.y))
				{
					Dist = d;
					break;
				}
			if(Dist < 0.0f)
				continue;
			// the wall face and how far the wall goes in
			const float Face = Pos.x + Side * Dist;
			const float Away = (Side > 0) == (LIGHT_FROM.x < 0.0f) ? 1.0f : 0.45f; // stronger on the side the light pushes to
			const float a = 0.45f * Away * (1.0f - (Dist - 14.0f) / 60.0f);
			if(Side > 0)
				ClipWorld(Graphics(), Screen, Face, Pos.y - 60.0f, Face + 40.0f, Pos.y + 60.0f);
			else
				ClipWorld(Graphics(), Screen, Face - 40.0f, Pos.y - 60.0f, Face, Pos.y + 60.0f);
			Graphics()->TextureSet(m_Soft);
			Graphics()->QuadsBegin();
			Graphics()->SetColor(0.0f, 0.01f, 0.03f, a);
			const float Size = 34.0f + Dist * 0.4f;
			IGraphics::CQuadItem Shadow(Face, Pos.y + 4.0f, Size * 0.8f, Size);
			Graphics()->QuadsDraw(&Shadow, 1);
			Graphics()->QuadsEnd();
			Graphics()->ClipDisable();
		}
	}
}

void CFogLighting::RenderFreezeShimmer()
{
	const CScreenRect Screen = Graphics()->GetScreen();
	const CCollision *pCol = Collision();
	const float Now = Client()->GlobalTime();
	const int x0 = std::max(0, (int)std::floor(Screen.m_TopLeft.x / TILE)), x1 = std::min(pCol->GetWidth() - 1, (int)std::ceil(Screen.m_BottomRight.x / TILE));
	const int y0 = std::max(0, (int)std::floor(Screen.m_TopLeft.y / TILE)), y1 = std::min(pCol->GetHeight() - 1, (int)std::ceil(Screen.m_BottomRight.y / TILE));
	Graphics()->BlendAdditive();
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	for(int ty = y0; ty <= y1; ty++)
		for(int tx = x0; tx <= x1; tx++)
		{
			const int Index = ty * pCol->GetWidth() + tx;
			const int Game = pCol->GetTileIndex(Index), Front = pCol->GetFrontTileIndex(Index);
			const bool Freeze = Game == TILE_FREEZE || Front == TILE_FREEZE || Game == TILE_DFREEZE || Front == TILE_DFREEZE;
			const bool Death = Game == TILE_DEATH || Front == TILE_DEATH;
			if(!Freeze && !Death)
				continue;
			const float X = tx * TILE, Y = ty * TILE;
			const float Seed = tx * 1.37f + ty * 2.11f;
			const ColorRGBA Tint = Freeze ? ColorRGBA(0.55f, 0.8f, 1.0f, 1.0f) : ColorRGBA(1.0f, 0.45f, 0.4f, 1.0f);
			// a slow breathing glow
			const float Breath = 0.035f + 0.025f * std::sin(Now * 1.6f + Seed);
			Graphics()->SetColor(Tint.r * Breath, Tint.g * Breath, Tint.b * Breath, 1.0f);
			IGraphics::CQuadItem Glow(X, Y, TILE, TILE);
			Graphics()->QuadsDrawTL(&Glow, 1);
			// heat/frost haze: thin wavering bands drifting up through the tile
			for(int b = 0; b < 2; b++)
			{
				const float Life = std::fmod(Now * 0.5f + Seed * 0.3f + b * 0.5f, 1.0f);
				const float Wave = std::sin(Now * 3.0f + Seed + b * 2.0f) * 4.0f;
				const float By = Y + TILE - Life * TILE * 1.4f;
				const float k = 0.05f * std::sin(Life * pi);
				const ColorRGBA c(Tint.r * k, Tint.g * k, Tint.b * k, 1.0f), z(0, 0, 0, 1);
				Graphics()->SetColor4(z, z, c, c);
				const IGraphics::CFreeformItem Band(X + 3.0f + Wave, By - 5.0f, X + TILE - 3.0f + Wave * 0.6f, By - 5.0f, X + 3.0f - Wave * 0.4f, By, X + TILE - 3.0f + Wave, By);
				Graphics()->QuadsDrawFreeform(&Band, 1);
			}
		}
	Graphics()->QuadsEnd();
	Graphics()->BlendNormal();
}

void CFogLighting::RenderSourceGlow()
{
	// soft halos only where something gives off light
	Graphics()->BlendAdditive();
	Graphics()->TextureSet(m_Soft);
	Graphics()->QuadsBegin();
	const float Now = Client()->GlobalTime();
	const int NumItems = Client()->SnapNumItems(IClient::SNAP_CURRENT);
	for(int i = 0; i < NumItems; i++)
	{
		const IClient::CSnapItem Item = Client()->SnapGetItem(IClient::SNAP_CURRENT, i);
		if(Item.m_Type == NETOBJTYPE_LASER || Item.m_Type == NETOBJTYPE_DDNETLASER)
		{
			vec2 From, To;
			if(Item.m_Type == NETOBJTYPE_LASER)
			{
				const CNetObj_Laser *p = (const CNetObj_Laser *)Item.m_pData;
				From = vec2(p->m_FromX, p->m_FromY);
				To = vec2(p->m_X, p->m_Y);
			}
			else
			{
				const CNetObj_DDNetLaser *p = (const CNetObj_DDNetLaser *)Item.m_pData;
				From = vec2(p->m_FromX, p->m_FromY);
				To = vec2(p->m_ToX, p->m_ToY);
			}
			const float Len = distance(From, To);
			const int Steps = std::clamp((int)(Len / 14.0f), 1, 80);
			Graphics()->SetColor(0.45f, 0.65f, 1.0f, 0.22f);
			for(int s = 0; s <= Steps; s++)
			{
				const vec2 p = mix(From, To, s / (float)Steps);
				IGraphics::CQuadItem Halo(p.x, p.y, 34.0f, 34.0f);
				Graphics()->QuadsDraw(&Halo, 1);
			}
			Graphics()->SetColor(0.7f, 0.85f, 1.0f, 0.5f);
			IGraphics::CQuadItem Tip(To.x, To.y, 46.0f, 46.0f);
			Graphics()->QuadsDraw(&Tip, 1);
		}
		else if(Item.m_Type == NETOBJTYPE_PICKUP)
		{
			const CNetObj_Pickup *p = (const CNetObj_Pickup *)Item.m_pData;
			ColorRGBA c(0.8f, 0.85f, 1.0f, 0.28f);
			if(p->m_Type == POWERUP_HEALTH)
				c = ColorRGBA(1.0f, 0.4f, 0.4f, 0.32f);
			else if(p->m_Type == POWERUP_ARMOR)
				c = ColorRGBA(1.0f, 0.8f, 0.35f, 0.32f);
			else if(p->m_Type == POWERUP_NINJA)
				c = ColorRGBA(0.7f, 0.45f, 1.0f, 0.32f);
			const float Pulse = 0.8f + 0.2f * std::sin(Now * 2.5f + Item.m_Id);
			Graphics()->SetColor(c.r, c.g, c.b, c.a * Pulse);
			IGraphics::CQuadItem Halo(p->m_X, p->m_Y, 70.0f, 70.0f);
			Graphics()->QuadsDraw(&Halo, 1);
		}
	}
	Graphics()->QuadsEnd();
	Graphics()->BlendNormal();
}

void CFogLighting::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	const CScreenRect OldScreen = Graphics()->GetScreen();
	const vec2 Center = GameClient()->m_Camera.m_Center;
	const CScreenRect World = Graphics()->MapScreenToWorld(Center.x, Center.y, 100.0f, 100.0f, 100.0f, 0.0f, 0.0f, Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom);
	Graphics()->MapScreen(World);
	if(!m_Front)
	{
		if(g_Config.m_FogAo)
			RenderAmbientOcclusion();
		if(g_Config.m_FogGodRays)
			RenderGodRays();
		if(g_Config.m_FogRimLight)
			RenderRimLight();
	}
	else
	{
		if(g_Config.m_FogWallShadows)
			RenderWallShadows();
		if(g_Config.m_FogLight)
			RenderLights();
		if(g_Config.m_FogFreezeShimmer)
			RenderFreezeShimmer();
		if(g_Config.m_FogSourceGlow)
			RenderSourceGlow();
	}
	Graphics()->MapScreen(OldScreen);
}
