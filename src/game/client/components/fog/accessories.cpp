#include "accessories.h"
#include "fx.h"

#include <base/color.h>
#include <base/math.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>

#include <engine/image.h>

#include <generated/client_data.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

// names go through Localize() where they are shown
// glow colours
constexpr unsigned FROST = 10203861, WARM = 1638335, ROSE = 16305343, HALO = 2097119, CYAN = 9109452, MOON = 10289132;

const SFogAccessoryDef gs_aCatalogue[] = {
	// above and behind
	{"broken_halo", "Broken halo", 0.0f, -0.66f, 0.55f, 1, SFogAccessoryDef::ANIM_FLICKER, false, false, 2260965, HALO, 0.9f},
	{"fallen_wings", "Fallen wings", 0.0f, -0.10f, 1.35f, 0, SFogAccessoryDef::ANIM_FLAP, false, false, 10361179, FROST, 0.4f},
	{"thorn_crown", "Crown of thorns", 0.0f, -0.44f, 0.62f, 1, SFogAccessoryDef::ANIM_NONE, false, false, 920408, FROST, 0.3f},
	{"crescent_moon", "Crescent moon", 0.42f, -0.58f, 0.30f, 1, SFogAccessoryDef::ANIM_BOB, false, false, 2764265, MOON, 0.8f},
	{"broken_heart", "Broken heart", 0.0f, -0.74f, 0.30f, 1, SFogAccessoryDef::ANIM_PULSE, false, false, 15873413, ROSE, 0.7f},
	{"rain_cloud", "Rain cloud", 0.0f, -0.80f, 0.55f, 1, SFogAccessoryDef::ANIM_RAIN, false, false, 10230936, FROST, 0.35f},
	{"umbrella", "Umbrella", 0.0f, -0.82f, 0.95f, 1, SFogAccessoryDef::ANIM_SWAY, false, false, 10052318, FROST, 0.3f},
	{"lantern", "Paper lantern", 0.46f, -0.46f, 0.40f, 1, SFogAccessoryDef::ANIM_SWAY, false, false, 1605560, WARM, 0.9f},
	// companions
	{"moth", "Moth", 0.0f, -0.30f, 0.28f, 1, SFogAccessoryDef::ANIM_ORBIT, false, false, 2302404, FROST, 0.4f},
	{"wisp", "Wisp", 0.0f, -0.20f, 0.22f, 1, SFogAccessoryDef::ANIM_ORBIT, false, false, 9349849, CYAN, 1.0f},
	// on the tee
	{"headphones", "Headphones", 0.0f, -0.18f, 0.95f, 1, SFogAccessoryDef::ANIM_PULSE, false, false, 10229588, FROST, 0.3f},
	{"veil", "Veil", 0.0f, -0.14f, 1.00f, 1, SFogAccessoryDef::ANIM_SWAY, false, false, 10040287, FROST, 0.25f},
	{"cigarette", "Cigarette", 0.24f, 0.10f, 0.45f, 1, SFogAccessoryDef::ANIM_SMOKE, false, true, 242, WARM, 0.2f},
	{"tears", "Tears", 0.0f, 0.10f, 0.50f, 1, SFogAccessoryDef::ANIM_TEARS, true, false, 9947355, FROST, 0.6f},
	{"wilted_flower", "Wilted flower", 0.18f, -0.55f, 0.45f, 1, SFogAccessoryDef::ANIM_SWAY, false, false, 15143555, FROST, 0.3f},
	{"plasters", "Plasters", -0.22f, -0.22f, 0.28f, 1, SFogAccessoryDef::ANIM_NONE, false, false, 1655759, FROST, 0.25f},
};

vec2 Rotate(vec2 v, float a)
{
	const float c = std::cos(a), s = std::sin(a);
	return vec2(v.x * c - v.y * s, v.x * s + v.y * c);
}

} // namespace

const SFogAccessoryDef *CFogAccessories::Catalogue(int *pNum)
{
	if(pNum)
		*pNum = (int)std::size(gs_aCatalogue);
	return gs_aCatalogue;
}

IGraphics::CTextureHandle CFogAccessories::Texture(int Def) const
{
	if(Def < 0 || Def >= (int)m_vTextures.size())
		return IGraphics::CTextureHandle();
	return m_vTextures[Def];
}

void CFogAccessories::OnInit()
{
	m_vTextures.clear();
	m_vGlowTextures.clear();
	for(const auto &Def : gs_aCatalogue)
	{
		char aPath[IO_MAX_PATH_LENGTH];
		str_format(aPath, sizeof(aPath), "fog/accessories/%s.png", Def.m_pId);
		m_vTextures.push_back(Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL));
		str_format(aPath, sizeof(aPath), "fog/accessories/%s_glow.png", Def.m_pId);
		m_vGlowTextures.push_back(Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL));
	}
	m_PuffTexture = Graphics()->LoadTexture("fog/accessories/fx_puff.png", IStorage::TYPE_ALL);
	m_DropTexture = Graphics()->LoadTexture("fog/accessories/fx_drop.png", IStorage::TYPE_ALL);
	m_RingTexture = Graphics()->LoadTexture("fog/accessories/fx_ring.png", IStorage::TYPE_ALL);
	CRenderTools::ms_pfnFogAccessoryHook = RenderHook;
	CRenderTools::ms_pFogAccessoryUser = this;
}

void CFogAccessories::OnRender()
{
	const float Now = Client()->GlobalTime();
	for(auto It = m_Remote.begin(); It != m_Remote.end();)
	{
		if(Now - It->second.m_LastUsed > 5.0f)
		{
			const int Slot = REMOTE_SLOT + It->first;
			auto Sil = m_Silhouettes.find(Slot);
			if(Sil != m_Silhouettes.end())
			{
				Graphics()->UnloadTexture(&Sil->second.m_Body);
				Graphics()->UnloadTexture(&Sil->second.m_Feet);
				m_Silhouettes.erase(Sil);
			}
			m_Motion.erase(Slot);
			It = m_Remote.erase(It);
		}
		else
			++It;
	}
}

void CFogAccessories::OnShutdown()
{
	CRenderTools::ms_pfnFogAccessoryHook = nullptr;
	CRenderTools::ms_pFogAccessoryUser = nullptr;
	for(auto &Texture : m_vTextures)
		Graphics()->UnloadTexture(&Texture);
	m_vTextures.clear();
	for(auto &Texture : m_vGlowTextures)
		Graphics()->UnloadTexture(&Texture);
	m_vGlowTextures.clear();
	Graphics()->UnloadTexture(&m_PuffTexture);
	Graphics()->UnloadTexture(&m_DropTexture);
	Graphics()->UnloadTexture(&m_RingTexture);
	for(auto &[Slot, Sil] : m_Silhouettes)
	{
		Graphics()->UnloadTexture(&Sil.m_Body);
		Graphics()->UnloadTexture(&Sil.m_Feet);
	}
	m_Silhouettes.clear();
}

char *CFogAccessories::ConfigString(int Slot) const
{
	return Slot == 0 ? g_Config.m_FogTeeAccessories : g_Config.m_FogDummyAccessories;
}

// format: id,x,y,size,angle,colour,flags; x/y/size in thousandths of the tee size,
// flags bit 0 animate, bit 1 mirror
void CFogAccessories::ParseList(const char *pStr, std::vector<SFogAccessory> &vWorn) const
{
	vWorn.clear();
	int Num;
	const SFogAccessoryDef *pDefs = Catalogue(&Num);
	const char *p = pStr;
	while(*p && (int)vWorn.size() < MAX_WORN)
	{
		const char *pEnd = str_find(p, ";");
		std::string Entry = pEnd ? std::string(p, pEnd - p) : std::string(p);
		p = pEnd ? pEnd + 1 : p + str_length(p);
		char aId[32] = "";
		int x = 0, y = 0, Size = 500, Angle = 0, Flags = 1;
		unsigned Color = 0, GlowColor = 0;
		const size_t Comma = Entry.find(',');
		if(Comma == std::string::npos || Comma >= sizeof(aId))
			continue;
		str_copy(aId, Entry.c_str(), Comma + 1);
		const int Fields = std::sscanf(Entry.c_str() + Comma + 1, "%d,%d,%d,%d,%u,%d,%u", &x, &y, &Size, &Angle, &Color, &Flags, &GlowColor);
		if(Fields < 4)
			continue;
		for(int d = 0; d < Num; d++)
			if(str_comp(pDefs[d].m_pId, aId) == 0)
			{
				SFogAccessory a;
				a.m_Def = d;
				a.m_X = std::clamp(x / 1000.0f, -1.5f, 1.5f);
				a.m_Y = std::clamp(y / 1000.0f, -1.5f, 1.5f);
				a.m_Size = std::clamp(Size / 1000.0f, 0.05f, 3.0f);
				a.m_Angle = (float)std::clamp(Angle, -360, 360);
				a.m_Color = Fields >= 5 ? Color : pDefs[d].m_Color;
				a.m_Animate = Flags & 1;
				a.m_Mirror = Flags & 2;
				a.m_GlowColor = Fields >= 7 ? GlowColor : pDefs[d].m_GlowColor;
				vWorn.push_back(a);
				break;
			}
	}
}

// format: id,x,y,size,angle,colour,flags,glow; x/y/size in thousandths of the tee size,
// flags bit 0 animate, bit 1 mirror
void CFogAccessories::Parse(int Slot)
{
	const char *pStr = ConfigString(Slot);
	if(m_aParsedOnce[Slot] && m_aParsed[Slot] == pStr)
		return;
	m_aParsedOnce[Slot] = true;
	m_aParsed[Slot] = pStr;
	ParseList(pStr, m_avWorn[Slot]);
}

int CFogAccessories::SetRemote(int ClientId, const SFogLook &Look)
{
	if(!g_Config.m_FogTeeAccessoriesOn || (Look.m_Accessories.empty() && !Look.m_Aura && !Look.m_Outline))
		return -1;
	SRemote &Remote = m_Remote[ClientId];
	Remote.m_Look = Look;
	Remote.m_LastUsed = Client()->GlobalTime();
	if(Remote.m_Parsed != Look.m_Accessories)
	{
		Remote.m_Parsed = Look.m_Accessories;
		ParseList(Look.m_Accessories.c_str(), Remote.m_vWorn);
	}
	return REMOTE_SLOT + ClientId;
}

const std::vector<SFogAccessory> &CFogAccessories::WornFor(int Slot)
{
	if(Slot < REMOTE_SLOT)
		return Worn(Slot);
	static const std::vector<SFogAccessory> s_vNone;
	auto It = m_Remote.find(Slot - REMOTE_SLOT);
	return It == m_Remote.end() ? s_vNone : It->second.m_vWorn;
}

SFogLook CFogAccessories::LookFor(int Slot) const
{
	if(Slot < REMOTE_SLOT)
		return SFogLook::FromConfig(Slot == 1);
	auto It = m_Remote.find(Slot - REMOTE_SLOT);
	return It == m_Remote.end() ? SFogLook() : It->second.m_Look;
}

void CFogAccessories::Save(int Slot)
{
	const int Real = Slot == 1 && g_Config.m_FogDummySync ? 0 : Slot;
	std::string Out;
	int Num;
	const SFogAccessoryDef *pDefs = Catalogue(&Num);
	for(const SFogAccessory &a : m_avWorn[Real])
	{
		char aBuf[96];
		str_format(aBuf, sizeof(aBuf), "%s%s,%d,%d,%d,%d,%u,%d,%u", Out.empty() ? "" : ";", pDefs[a.m_Def].m_pId, (int)std::lround(a.m_X * 1000.0f), (int)std::lround(a.m_Y * 1000.0f),
			(int)std::lround(a.m_Size * 1000.0f), (int)std::lround(a.m_Angle), a.m_Color, (a.m_Animate ? 1 : 0) | (a.m_Mirror ? 2 : 0), a.m_GlowColor);
		Out += aBuf;
	}
	char *pStr = ConfigString(Real);
	str_copy(pStr, Out.c_str(), Real == 0 ? sizeof(g_Config.m_FogTeeAccessories) : sizeof(g_Config.m_FogDummyAccessories));
	m_aParsed[Real] = pStr;
}

std::vector<SFogAccessory> &CFogAccessories::Worn(int Slot)
{
	const int Real = Slot == 1 && g_Config.m_FogDummySync ? 0 : Slot;
	Parse(Real);
	return m_avWorn[Real];
}

SFogAccessory CFogAccessories::Make(int Def, float X, float Y) const
{
	SFogAccessory a;
	a.m_Def = Def;
	a.m_X = X;
	a.m_Y = Y;
	a.m_Size = gs_aCatalogue[Def].m_Size;
	a.m_Color = gs_aCatalogue[Def].m_Color;
	a.m_GlowColor = gs_aCatalogue[Def].m_GlowColor;
	return a;
}

int CFogAccessories::SlotFor(int Slot) const
{
	if(!g_Config.m_FogTeeAccessoriesOn)
		return -1;
	const int Real = Slot == 1 && g_Config.m_FogDummySync ? 0 : Slot;
	const SFogLook Look = SFogLook::FromConfig(Slot == 1);
	const bool Aura = Look.m_Aura || Look.m_Outline;
	return ConfigString(Real)[0] || Aura ? Slot : -1;
}

bool CFogAccessories::LastRect(int Slot, int Index, vec2 &Center, float &Size) const
{
	if(Index < 0 || Index >= m_aLast[Slot].m_Num)
		return false;
	Center = m_aLast[Slot].m_aCenter[Index];
	Size = m_aLast[Slot].m_aSize[Index];
	return true;
}

void CFogAccessories::RenderHook(void *pUser, int Slot, int Layer, const CAnimState *pAnim, vec2 Pos, vec2 BodyPos, float BaseSize, float BodyAngle, vec2 Dir, float Alpha)
{
	CFogAccessories *pThis = static_cast<CFogAccessories *>(pUser);
	if(Layer == 0 && Slot >= 0)
		pThis->RenderAura(Slot, pAnim, Pos, BodyPos, BaseSize, BodyAngle, Dir, Alpha);
	pThis->Render(Slot, Layer, BodyPos, BaseSize, BodyAngle, Dir, Alpha);
}

void CFogAccessories::Render(int Slot, int Layer, vec2 BodyPos, float BaseSize, float BodyAngle, vec2 Dir, float Alpha)
{
	if(Slot < 0)
		return;
	const std::vector<SFogAccessory> &vWorn = WornFor(Slot);
	const bool Inverted = Slot < REMOTE_SLOT ? Slot == 1 && g_Config.m_FogDummySync == 2 : false;
	const float Now = Client()->GlobalTime();
	const float AnimSpeed = g_Config.m_FogTeeAccessoriesAnim / 100.0f;

	// how the tee moves: drives the swinging; one update per frame (the front layer)
	SMotion &Motion = m_Motion[Slot];
	if(Layer == 1)
	{
		const float Dt = Motion.m_LastTime < 0.0f ? 0.0f : std::clamp(Now - Motion.m_LastTime, 0.0f, 0.1f);
		const vec2 Delta = BodyPos - Motion.m_LastPos;
		if(Dt > 0.0f && length(Delta) < BaseSize * 3.0f)
			Motion.m_Vel = mix(Motion.m_Vel, Delta / std::max(Dt, 1e-4f) / std::max(BaseSize, 1.0f), std::min(Dt * 12.0f, 1.0f));
		else if(Dt > 0.0f)
			Motion.m_Vel = vec2(0.0f, 0.0f); // teleport or another preview: don't fling
		Motion.m_LastPos = BodyPos;
		Motion.m_LastTime = Now;
		for(size_t i = 0; i < vWorn.size() && i < (size_t)MAX_WORN; i++)
		{
			const float Target = std::clamp(-Motion.m_Vel.x * 0.035f, -0.6f, 0.6f) + std::clamp(-Motion.m_Vel.y * 0.01f, -0.15f, 0.15f) * (Motion.m_Vel.x >= 0.0f ? 1.0f : -1.0f);
			const float Stiff = 90.0f, Damp = 7.0f;
			Motion.m_aAngleVel[i] += ((Target - Motion.m_aAngle[i]) * Stiff - Motion.m_aAngleVel[i] * Damp) * Dt;
			Motion.m_aAngle[i] += Motion.m_aAngleVel[i] * Dt;
		}
	}

	static SLast s_Scratch;
	SLast &Last = Slot < REMOTE_SLOT ? m_aLast[Slot] : s_Scratch;
	Last.m_BodyPos = BodyPos;
	Last.m_BaseSize = BaseSize;
	Last.m_Num = (int)std::min(vWorn.size(), (size_t)MAX_WORN);

	const float T = Now * AnimSpeed;
	for(size_t i = 0; i < vWorn.size() && i < (size_t)MAX_WORN; i++)
	{
		const SFogAccessory &a = vWorn[i];
		const SFogAccessoryDef &Def = gs_aCatalogue[a.m_Def];
		const bool Animate = a.m_Animate && AnimSpeed > 0.0f;
		const float Phase = i * 1.7f;
		vec2 Offset = vec2(a.m_X, a.m_Y) * BaseSize;
		if(Def.m_FollowEyes)
			Offset += vec2(Dir.x * 0.125f, Dir.y * 0.10f) * BaseSize;
		const float Size = a.m_Size * BaseSize;
		const vec2 Anchor = BodyPos + Rotate(Offset, BodyAngle);
		if(Layer == 1)
		{
			Last.m_aCenter[i] = Anchor;
			Last.m_aSize[i] = Size;
		}

		ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(a.m_Color));
		if(Inverted)
			Color = ColorRGBA(1.0f - Color.r, 1.0f - Color.g, 1.0f - Color.b, 1.0f);
		Color.a = Alpha;
		ColorRGBA GlowColor = color_cast<ColorRGBA>(ColorHSLA(a.m_GlowColor));
		if(Inverted)
			GlowColor = ColorRGBA(1.0f - GlowColor.r, 1.0f - GlowColor.g, 1.0f - GlowColor.b, 1.0f);
		GlowColor.a = Alpha * Def.m_Glow * (Animate ? 0.85f + 0.15f * std::sin(T * 1.9f + Phase) : 1.0f);
		const bool Flip = a.m_Mirror != (Def.m_FlipWithLook && Dir.x < 0.0f);
		float Angle = a.m_Angle / 180.0f * pi * (Flip ? -1.0f : 1.0f);
		const float Swing = Animate ? Motion.m_aAngle[i] : 0.0f;

		// the moth decides its layer by where it is on its circle
		if(Def.m_Anim == SFogAccessoryDef::ANIM_ORBIT)
		{
			const float Orbit = Animate ? T * 1.4f + Phase : 0.6f;
			const bool Front = std::sin(Orbit) >= 0.0f;
			if((Layer == 1) != Front)
				continue;
			const vec2 Pos = Anchor + vec2(std::cos(Orbit) * 0.55f, std::sin(Orbit) * 0.14f + std::sin(Orbit * 3.0f) * 0.05f) * BaseSize;
			const float Flap = Animate ? 0.35f + 0.65f * std::abs(std::sin(T * 11.0f + Phase)) : 1.0f;
			const float Depth = Front ? 1.0f : 0.8f;
			DrawPiece(a.m_Def, Pos, Size * Flap * Depth, Size * Depth, Angle + std::cos(Orbit) * 0.3f, std::sin(Orbit + pi / 2) < 0.0f, Color.WithMultipliedAlpha(Front ? 1.0f : 0.7f), GlowColor);
			continue;
		}
		if(Def.m_Layer != Layer)
			continue;

		switch(Def.m_Anim)
		{
		default:
		{
			float ScaleX = 1.0f, ScaleY = 1.0f, Glow = -1.0f;
			vec2 Center = Anchor;
			if(Animate)
			{
				switch(Def.m_Anim)
				{
				case SFogAccessoryDef::ANIM_SWAY:
				case SFogAccessoryDef::ANIM_SMOKE:
					Angle += Swing + std::sin(T * 1.3f + Phase) * 0.04f;
					break;
				case SFogAccessoryDef::ANIM_BOB:
				case SFogAccessoryDef::ANIM_RAIN:
					Center.y += std::sin(T * 1.6f + Phase) * BaseSize * 0.03f;
					Angle += Swing * 0.5f;
					break;
				case SFogAccessoryDef::ANIM_FLAP:
					ScaleX = 0.86f + 0.14f * (0.5f + 0.5f * std::sin(T * 2.2f + Phase));
					Center.y += std::sin(T * 2.2f + Phase) * BaseSize * 0.012f;
					break;
				case SFogAccessoryDef::ANIM_PULSE:
					ScaleX = ScaleY = 1.0f + std::pow(0.5f + 0.5f * std::sin(T * 4.0f + Phase), 8.0f) * 0.05f;
					break;
				case SFogAccessoryDef::ANIM_FLICKER:
				{
					// a calm glow with a short stutter every few seconds
					const float k = std::fmod(T + Phase, 4.3f);
					const float Stutter = k < 0.35f ? (std::sin(k * 90.0f) > 0.0f ? 0.0f : 1.0f) : 1.0f;
					Glow = (0.8f + 0.2f * std::sin(T * 2.0f + Phase)) * Stutter;
					if(!Def.m_FollowEyes)
						Center.y += std::sin(T * 1.6f + Phase) * BaseSize * 0.025f;
					break;
				}
				default:
					break;
				}
			}
			if(Glow >= 0.0f)
				GlowColor.a *= Glow;
			DrawPiece(a.m_Def, Center, Size * ScaleX, Size * ScaleY, BodyAngle + Angle, Flip, Color, GlowColor);
			if(!Animate)
				break;

			if(Def.m_Anim == SFogAccessoryDef::ANIM_SMOKE)
			{
				// ember at the tip, smoke curling up and thinning out
				const float Rot = BodyAngle + Angle;
				const vec2 Tip = Center + Rotate(vec2(0.297f * Size * (Flip ? -1.0f : 1.0f), 0.0f), Rot);
				Graphics()->BlendAdditive();
				const float Ember = 0.55f + 0.45f * std::pow(0.5f + 0.5f * std::sin(T * 1.7f + Phase), 3.0f);
				DrawSprite(m_PuffTexture, Tip, Size * 0.16f, Size * 0.16f, 0.0f, false, ColorRGBA(1.0f, 0.45f, 0.18f, Alpha * Ember));
				DrawSprite(m_PuffTexture, Tip, Size * 0.06f, Size * 0.06f, 0.0f, false, ColorRGBA(1.0f, 0.85f, 0.6f, Alpha * Ember));
				Graphics()->BlendNormal();
				for(int p = 0; p < 7; p++)
				{
					const float Life = std::fmod(T * 0.35f + p / 7.0f, 1.0f);
					const float Drift = std::sin(Life * 5.0f + p * 2.1f) * BaseSize * 0.06f * Life;
					const vec2 Pos = Tip + vec2(Drift - Motion.m_Vel.x * BaseSize * 0.004f * Life, -Life * BaseSize * 0.55f);
					const float r = Size * (0.12f + Life * 0.42f);
					DrawSprite(m_PuffTexture, Pos, r, r, 0.0f, false, ColorRGBA(0.82f, 0.84f, 0.87f, Alpha * 0.32f * std::sin(Life * pi) * (1.0f - Life * 0.5f)));
				}
			}
			else if(Def.m_Anim == SFogAccessoryDef::ANIM_TEARS)
			{
				// a drop gathers at the end of each streak, runs off and falls
				for(int e = 0; e < 2; e++)
				{
					const float Cycle = std::fmod(T * 0.45f + e * 0.53f + Phase, 1.0f);
					const vec2 End = Center + Rotate(vec2((e == 0 ? -0.1875f : 0.1875f) * Size, 0.27f * Size), BodyAngle + Angle);
					float Fall = 0.0f, Grow = 1.0f;
					if(Cycle < 0.55f)
						Grow = Cycle / 0.55f;
					else
						Fall = (Cycle - 0.55f) / 0.45f;
					const float d = Size * 0.13f * (0.4f + 0.6f * Grow);
					const vec2 Pos = End + vec2(0.0f, Fall * Fall * BaseSize * 0.9f);
					DrawSprite(m_DropTexture, Pos, d, d * 1.15f, 0.0f, false, Color.WithAlpha(Alpha * (1.0f - Fall)));
				}
			}
			else if(Def.m_Anim == SFogAccessoryDef::ANIM_RAIN)
			{
				// thin streaks from the cloud down to the top of the head
				Graphics()->TextureClear();
				Graphics()->QuadsBegin();
				Graphics()->QuadsSetRotation(0.0f);
				const float Top = Center.y + Size * 0.16f, Bottom = BodyPos.y - BaseSize * 0.36f;
				const float Height = std::max(Bottom - Top, BaseSize * 0.05f);
				for(int r = 0; r < 9; r++)
				{
					const float x = Center.x + (std::fmod(r * 0.618f, 1.0f) - 0.5f) * Size * 0.75f;
					const float Life = std::fmod(T * 1.6f + r * 0.37f, 1.0f);
					const float y = Top + Life * Height;
					const float Len = BaseSize * 0.07f;
					Graphics()->SetColor(Color.r * 0.9f, Color.g * 0.95f, Color.b, Alpha * 0.55f * std::sin(Life * pi));
					IGraphics::CQuadItem Streak(x - BaseSize * 0.006f - Motion.m_Vel.x * 0.002f * BaseSize, y, BaseSize * 0.012f, Len);
					Graphics()->QuadsDrawTL(&Streak, 1);
				}
				Graphics()->QuadsEnd();
			}
			break;
		}
		}
	}
	Graphics()->QuadsSetRotation(0.0f);
}

void CFogAccessories::DrawSprite(IGraphics::CTextureHandle Texture, vec2 Center, float W, float H, float Angle, bool Flip, ColorRGBA Color)
{
	Graphics()->TextureSet(Texture);
	Graphics()->QuadsBegin();
	Graphics()->QuadsSetRotation(Angle);
	Graphics()->QuadsSetSubset(Flip ? 1.0f : 0.0f, 0.0f, Flip ? 0.0f : 1.0f, 1.0f);
	Graphics()->SetColor(Color.r, Color.g, Color.b, Color.a);
	IGraphics::CQuadItem Item(Center.x, Center.y, W, H);
	Graphics()->QuadsDraw(&Item, 1);
	Graphics()->QuadsEnd();
}

void CFogAccessories::DrawPiece(int Def, vec2 Center, float W, float H, float Angle, bool Flip, ColorRGBA Color, ColorRGBA Glow)
{
	if(Glow.a > 0.001f && m_vGlowTextures[Def].IsValid())
	{
		Graphics()->BlendAdditive();
		DrawSprite(m_vGlowTextures[Def], Center, W * 1.05f, H * 1.05f, Angle, Flip, Glow);
		Graphics()->BlendNormal();
	}
	DrawSprite(m_vTextures[Def], Center, W, H, Angle, Flip, Color);
}

void CFogAccessories::DrawIcon(int Def, vec2 Center, float Size, float Alpha)
{
	if(Def < 0 || Def >= (int)m_vTextures.size())
		return;
	ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(gs_aCatalogue[Def].m_Color));
	ColorRGBA Glow = color_cast<ColorRGBA>(ColorHSLA(gs_aCatalogue[Def].m_GlowColor));
	Color.a = Alpha;
	Glow.a = Alpha * std::max(gs_aCatalogue[Def].m_Glow, 0.45f);
	DrawPiece(Def, Center, Size, Size, 0.0f, false, Color, Glow);
	Graphics()->QuadsSetRotation(0.0f);
}

// a white copy of the skin's body and feet outlines: the shape of the tee, ready to be tinted
bool CFogAccessories::LoadSilhouette(int Slot)
{
	SSilhouette &Sil = m_Silhouettes[Slot];
	const char *pSkin = Slot >= REMOTE_SLOT ? GameClient()->m_aClients[std::clamp(Slot - REMOTE_SLOT, 0, MAX_CLIENTS - 1)].m_aSkinName : (Slot == 1 && !g_Config.m_FogDummySync ? g_Config.m_ClDummySkin : g_Config.m_ClPlayerSkin);
	if(!pSkin[0])
		pSkin = "default";
	if(Sil.m_Skin == pSkin)
		return !Sil.m_Failed;
	Graphics()->UnloadTexture(&Sil.m_Body);
	Graphics()->UnloadTexture(&Sil.m_Feet);
	Sil.m_Skin = pSkin;
	Sil.m_Failed = false;
	CImageInfo Image;
	char aPath[IO_MAX_PATH_LENGTH];
	bool Loaded = false;
	for(const char *pName : {pSkin, "default"})
	{
		str_format(aPath, sizeof(aPath), "skins/%s.png", pName);
		Loaded = Graphics()->LoadPng(Image, aPath, IStorage::TYPE_ALL);
		if(!Loaded)
		{
			str_format(aPath, sizeof(aPath), "downloadedskins/%s.png", pName);
			Loaded = Graphics()->LoadPng(Image, aPath, IStorage::TYPE_SAVE);
		}
		if(Loaded && Image.m_Format == CImageInfo::FORMAT_RGBA)
			break;
		Image.Free();
		Loaded = false;
	}
	if(!Loaded)
	{
		Sil.m_Failed = true;
		return false;
	}
	for(size_t i = 0; i < (size_t)Image.m_Width * Image.m_Height; i++)
		Image.m_pData[i * 4] = Image.m_pData[i * 4 + 1] = Image.m_pData[i * 4 + 2] = 255;
	Sil.m_Body = Graphics()->LoadSpriteTexture(Image, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_BODY_OUTLINE]);
	Sil.m_Feet = Graphics()->LoadSpriteTexture(Image, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_FOOT_OUTLINE]);
	Image.Free();
	return true;
}

void CFogAccessories::DrawSilhouette(int Slot, const CAnimState *pAnim, vec2 Pos, vec2 BodyPos, float BaseSize, float BodyAngle, float Scale, vec2 Shift, ColorRGBA Color)
{
	const SSilhouette &Sil = m_Silhouettes[Slot];
	const float AnimScale = BaseSize / 64.0f;
	DrawSprite(Sil.m_Body, BodyPos + Shift, BaseSize * Scale, BaseSize * Scale, BodyAngle, false, Color);
	for(const CAnimKeyframe *pFoot : {pAnim->GetBackFoot(), pAnim->GetFrontFoot()})
	{
		const vec2 FootPos = Pos + vec2(pFoot->m_X, pFoot->m_Y) * AnimScale + Shift;
		DrawSprite(Sil.m_Feet, FootPos, BaseSize * Scale, BaseSize / 2.0f * Scale, pFoot->m_Angle * pi * 2.0f, false, Color);
	}
}

void CFogAccessories::RenderAura(int Slot, const CAnimState *pAnim, vec2 Pos, vec2 BodyPos, float BaseSize, float BodyAngle, vec2 Dir, float Alpha)
{
	const SFogLook Look = LookFor(Slot);
	if(!Look.m_Aura && !Look.m_Outline)
		return;
	const bool Inverted = Look.m_Invert;
	const auto Invert = [&](ColorRGBA c) { return Inverted ? ColorRGBA(1.0f - c.r, 1.0f - c.g, 1.0f - c.b, c.a) : c; };
	const float Now = Client()->GlobalTime();
	const bool Silhouette = LoadSilhouette(Slot);

	// the aura
	const int Aura = Look.m_Aura;
	if(Aura)
	{
		const float T = Now * Look.m_AuraSpeed / 100.0f;
		const float Strength = Look.m_AuraStrength / 100.0f * Alpha * (1.0f + 0.35f * FogMusicPulse());
		const float Size = Look.m_AuraSize / 100.0f;
		ColorRGBA Color = Look.m_AuraRainbow ? color_cast<ColorRGBA>(ColorHSLA(std::fmod(T * 0.06f, 1.0f), 0.7f, 0.65f, 1.0f)) : color_cast<ColorRGBA>(ColorHSLA(Look.m_AuraColor));
		Color = Invert(Color);
		const vec2 Center = BodyPos + vec2(0.0f, BaseSize * 0.08f);
		switch(Aura)
		{
		case 1: // glow: soft layers of light around the shape
			Graphics()->BlendAdditive();
			DrawSprite(m_PuffTexture, Center, BaseSize * 2.4f * Size, BaseSize * 2.2f * Size, 0.0f, false, Color.WithAlpha(Strength * (0.22f + 0.05f * std::sin(T * 2.0f))));
			if(Silhouette)
				for(int k = 1; k <= 4; k++)
					DrawSilhouette(Slot, pAnim, Pos, BodyPos, BaseSize, BodyAngle, 1.0f + 0.07f * k * Size, vec2(0.0f, 0.0f), Color.WithAlpha(Strength * 0.3f / k * (0.85f + 0.15f * std::sin(T * 2.4f))));
			Graphics()->BlendNormal();
			break;
		case 2: // dark aura: a shadow that breathes, and smoke rising from it
		{
			// always a deep shade of the chosen colour
			const ColorRGBA Dark(Color.r * 0.2f, Color.g * 0.2f, Color.b * 0.24f, 1.0f);
			if(Silhouette)
				for(int k = 1; k <= 4; k++)
				{
					const vec2 Wobble(std::sin(T * 2.3f + k) * BaseSize * 0.02f, std::cos(T * 1.7f + k * 2.0f) * BaseSize * 0.02f);
					DrawSilhouette(Slot, pAnim, Pos, BodyPos, BaseSize, BodyAngle, 1.0f + 0.08f * k * Size, Wobble, Dark.WithAlpha(Strength * 0.3f / k));
				}
			for(int p = 0; p < 12; p++)
			{
				const float Life = std::fmod(T * 0.4f + p / 12.0f, 1.0f);
				const float Side = std::sin(p * 2.4f) * BaseSize * 0.45f * Size;
				const vec2 PuffPos = Center + vec2(Side + std::sin(Life * 6.0f + p) * BaseSize * 0.08f, BaseSize * 0.3f - Life * BaseSize * 1.3f * Size);
				const float r = BaseSize * (0.3f + Life * 0.5f) * Size;
				DrawSprite(m_PuffTexture, PuffPos, r, r, 0.0f, false, Dark.WithAlpha(Strength * 0.45f * std::sin(Life * pi)));
			}
			break;
		}
		case 3: // mist: slow banks of fog circling you
			for(int p = 0; p < 9; p++)
			{
				const float a = T * 0.35f + p * pi * 2.0f / 9.0f;
				const vec2 PuffPos = Center + vec2(std::cos(a) * BaseSize * 0.62f * Size, std::sin(a) * BaseSize * 0.22f * Size + std::sin(T + p) * BaseSize * 0.05f);
				const float r = BaseSize * (0.7f + 0.2f * std::sin(T * 0.8f + p)) * Size;
				DrawSprite(m_PuffTexture, PuffPos, r * 1.3f, r, 0.0f, false, Color.WithAlpha(Strength * 0.2f));
			}
			break;
		case 4: // embers drifting up and going out
			Graphics()->BlendAdditive();
			for(int p = 0; p < 16; p++)
			{
				const float Life = std::fmod(T * 0.5f + p * 0.618f, 1.0f);
				const float x = (std::fmod(p * 0.37f, 1.0f) - 0.5f) * BaseSize * 1.1f * Size + std::sin(Life * 7.0f + p) * BaseSize * 0.06f;
				const vec2 EmberPos = Center + vec2(x, BaseSize * 0.4f - Life * BaseSize * 1.5f * Size);
				const float Flicker = 0.6f + 0.4f * std::sin(T * 13.0f + p * 3.0f);
				const float r = BaseSize * 0.09f * (1.0f - Life * 0.6f);
				DrawSprite(m_PuffTexture, EmberPos, r, r, 0.0f, false, Color.WithAlpha(Strength * Flicker * std::sin(Life * pi)));
			}
			Graphics()->BlendNormal();
			break;
		case 5: // stardust: a ring of slow twinkles
			Graphics()->BlendAdditive();
			for(int p = 0; p < 18; p++)
			{
				const float a = p * 2.39996f + T * 0.12f;
				const float Radius = BaseSize * (0.55f + std::fmod(p * 0.31f, 1.0f) * 0.35f) * Size;
				const vec2 DustPos = Center + vec2(std::cos(a) * Radius, std::sin(a) * Radius * 0.75f);
				const float Twinkle = std::pow(0.5f + 0.5f * std::sin(T * 2.2f + p * 1.9f), 4.0f);
				const float r = BaseSize * (0.05f + 0.07f * Twinkle);
				DrawSprite(m_PuffTexture, DustPos, r, r, 0.0f, false, Color.WithAlpha(Strength * (0.25f + 0.75f * Twinkle)));
			}
			Graphics()->BlendNormal();
			break;
		case 6: // rings spreading out like ripples
			Graphics()->BlendAdditive();
			for(int k = 0; k < 3; k++)
			{
				const float Life = std::fmod(T * 0.45f + k / 3.0f, 1.0f);
				const float r = BaseSize * (0.7f + Life * 1.4f) * Size;
				DrawSprite(m_RingTexture, Center, r, r * 0.9f, 0.0f, false, Color.WithAlpha(Strength * 0.55f * (1.0f - Life) * std::min(Life * 5.0f, 1.0f)));
			}
			Graphics()->BlendNormal();
			break;
		default:
			break;
		}
	}

	// the coloured outline: the tee's shape a little bigger, behind its own dark outline
	if(Look.m_Outline && Silhouette)
	{
		ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(Look.m_OutlineColor));
		float a = 1.0f;
		switch(Look.m_OutlineMode)
		{
		case 1: a = 0.55f + 0.45f * std::sin(Now * 3.0f); break;
		case 2: Color = color_cast<ColorRGBA>(ColorHSLA(std::fmod(Now * 0.12f, 1.0f), 0.75f, 0.65f, 1.0f)); break;
		case 3:
		{
			const float k = std::fmod(Now, 2.7f);
			a = k < 0.3f && std::sin(k * 80.0f) < 0.0f ? 0.15f : 1.0f;
			break;
		}
		default: break;
		}
		Color = Invert(Color);
		const float Scale = 1.0f + Look.m_OutlineWidth * 0.022f;
		DrawSilhouette(Slot, pAnim, Pos, BodyPos, BaseSize, BodyAngle, Scale, vec2(0.0f, 0.0f), Color.WithAlpha(Alpha * a));
	}
	Graphics()->QuadsSetRotation(0.0f);
}
