#include "fx.h"

#include "glass.h"

#include <base/dbg.h>
#include <base/math.h>
#include <base/str.h>

#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/image.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/collision.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <vector>

namespace {

constexpr float TEE = 28.0f; // tee diameter in world units

enum
{
	P_PUFF = 0, // soft round particle (dust, ash, smoke)
	P_SPARK, // additive streak along its velocity
	P_PRINT, // a footprint lying on the ground
	P_DROP, // a falling drop
	P_CONFETTI, // a fluttering paper bit
	P_NOTE, // a music note (icon font)
	P_WIND, // a thin wind line
	P_FLASH, // a short additive flash
};

struct SParticle
{
	vec2 m_Pos, m_Vel;
	float m_Life = 0.0f, m_MaxLife = 1.0f;
	float m_Size = 4.0f;
	float m_Rot = 0.0f, m_RotVel = 0.0f;
	float m_Gravity = 0.0f, m_Drag = 0.0f;
	ColorRGBA m_Color = ColorRGBA(1, 1, 1, 1);
	int m_Type = P_PUFF;
	bool m_Front = false;
};

struct SWeather
{
	vec2 m_Pos, m_Vel;
	float m_Size, m_Seed;
	float m_Settled = -1.0f; // snow resting on a tile: time it landed
};

struct STee
{
	vec2 m_Pos = vec2(0, 0), m_PrevPos = vec2(0, 0), m_Vel = vec2(0, 0), m_PrevVel = vec2(0, 0);
	bool m_Valid = false, m_Grounded = false, m_WasGrounded = false;
	float m_Ground = 0.0f; // y of the floor below, if any
	bool m_HasGround = false;
	float m_PrintDist = 0.0f;
	bool m_PrintLeft = false;
	int m_HookState = 0;
	bool m_Frozen = false;
	float m_FrozenSince = 0.0f;
};

struct SState
{
	std::vector<SParticle> m_vParticles;
	std::vector<SWeather> m_vWeather;
	STee m_aTees[MAX_CLIENTS];
	IGraphics::CTextureHandle m_Soft, m_Hole;
	float m_LastTime = -1.0f;
	float m_Pulse = 0.0f;
	// thoughts
	float m_NextThought = 20.0f, m_ThoughtStart = -100.0f;
	int m_ThoughtKind = 0;
	// finish
	int m_LastFinishTick = -1;
	float m_FinishStart = -100.0f;
	float m_NextNote = 0.0f;
	// camera
	vec2 m_CamLag = vec2(0, 0), m_CamLagVel = vec2(0, 0);
	float m_Vignette = 0.0f;
	std::vector<vec2> m_vPendingDeaths;
	// holidays
	IGraphics::CTextureHandle m_Bat, m_Heart, m_Petal;
	struct SHolidayBit
	{
		vec2 m_Pos, m_Vel;
		float m_Life = 0.0f, m_MaxLife = 1.0f, m_Size = 10.0f, m_Rot = 0.0f, m_RotVel = 0.0f, m_Seed = 0.0f;
		float m_Gravity = 0.0f, m_Drag = 0.0f;
		ColorRGBA m_Color;
		int m_Kind = 0; // 0 bat, 1 heart, 2 petal, 3 firework spark
	};
	std::vector<SHolidayBit> m_vHoliday;
	float m_NextHolidaySpawn = 0.0f, m_NextFirework = 0.0f;
};

SState gs_State;

float Rand01() { return (float)rand() / (float)RAND_MAX; }

// CCollision clamps coordinates outside the map to the border tiles (solid walls), so outside
// the map we must say "air" ourselves
bool SolidInMap(const CCollision *pCollision, vec2 Pos)
{
	if(Pos.x < 0.0f || Pos.y < 0.0f || Pos.x >= pCollision->GetWidth() * 32.0f || Pos.y >= pCollision->GetHeight() * 32.0f)
		return false;
	return pCollision->CheckPoint(Pos);
}
float RandRange(float a, float b) { return a + (b - a) * Rand01(); }

void Spawn(const SParticle &p)
{
	if(gs_State.m_vParticles.size() < 4000)
		gs_State.m_vParticles.push_back(p);
}

// the time of day from the local clock: 0 night .. 1 noon, and the sun's side (-1 morning .. 1 evening)
void DayPhase(float &Light, float &SunSide, ColorRGBA &Tint)
{
	const time_t Now = time(nullptr);
	std::tm Local{};
	localtime_r(&Now, &Local);
	const float h = Local.tm_hour + Local.tm_min / 60.0f;
	SunSide = std::clamp((h - 13.0f) / 6.0f, -1.0f, 1.0f);
	struct SKey
	{
		float m_Hour;
		ColorRGBA m_Tint;
	};
	// tint colour with alpha = how much it covers the world
	static const SKey s_aKeys[] = {
		{0.0f, ColorRGBA(0.10f, 0.13f, 0.28f, 0.42f)},
		{5.0f, ColorRGBA(0.12f, 0.15f, 0.30f, 0.40f)},
		{6.5f, ColorRGBA(0.98f, 0.62f, 0.50f, 0.20f)},
		{9.0f, ColorRGBA(1.0f, 0.95f, 0.88f, 0.03f)},
		{16.0f, ColorRGBA(1.0f, 0.92f, 0.80f, 0.05f)},
		{18.5f, ColorRGBA(0.98f, 0.50f, 0.32f, 0.24f)},
		{20.5f, ColorRGBA(0.30f, 0.22f, 0.40f, 0.34f)},
		{24.0f, ColorRGBA(0.10f, 0.13f, 0.28f, 0.42f)},
	};
	for(size_t i = 0; i + 1 < std::size(s_aKeys); i++)
		if(h >= s_aKeys[i].m_Hour && h <= s_aKeys[i + 1].m_Hour)
		{
			const float t = (h - s_aKeys[i].m_Hour) / (s_aKeys[i + 1].m_Hour - s_aKeys[i].m_Hour);
			const ColorRGBA &a = s_aKeys[i].m_Tint, &b = s_aKeys[i + 1].m_Tint;
			Tint = ColorRGBA(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
			break;
		}
	Light = std::clamp(1.0f - Tint.a * 2.0f, 0.0f, 1.0f);
}

enum
{
	HOLIDAY_NONE = 0,
	HOLIDAY_NEW_YEAR,
	HOLIDAY_HALLOWEEN,
	HOLIDAY_VALENTINE,
	HOLIDAY_MARCH8,
};

int CurrentHoliday(bool &NewYearNight)
{
	const time_t Now = time(nullptr);
	std::tm Local{};
	localtime_r(&Now, &Local);
	const int m = Local.tm_mon + 1, d = Local.tm_mday, h = Local.tm_hour;
	NewYearNight = (m == 12 && d == 31 && h >= 18) || (m == 1 && d == 1 && h < 3);
	if(g_Config.m_FogHolidayPreview)
	{
		NewYearNight = g_Config.m_FogHolidayPreview == HOLIDAY_NEW_YEAR;
		return g_Config.m_FogHolidayPreview;
	}
	if(!g_Config.m_FogHolidays)
		return HOLIDAY_NONE;
	if((m == 12 && d >= 20) || (m == 1 && d <= 7))
		return HOLIDAY_NEW_YEAR;
	if((m == 10 && d >= 24) || (m == 11 && d <= 2))
		return HOLIDAY_HALLOWEEN;
	if(m == 2 && d >= 12 && d <= 15)
		return HOLIDAY_VALENTINE;
	if(m == 3 && d >= 6 && d <= 9)
		return HOLIDAY_MARCH8;
	return HOLIDAY_NONE;
}

// your own weather wins; otherwise the holiday brings its own
int EffectiveWeather(float &Amount)
{
	Amount = g_Config.m_FogWeatherAmount / 100.0f;
	if(g_Config.m_FogWeather)
		return g_Config.m_FogWeather;
	bool Night;
	switch(CurrentHoliday(Night))
	{
	case HOLIDAY_NEW_YEAR: Amount = 0.7f; return 2;
	case HOLIDAY_HALLOWEEN: Amount = 0.35f; return 3;
	case HOLIDAY_MARCH8: Amount = 0.5f; return 4;
	default: return 0;
	}
}

// the solid floor right under a tee: how far it reaches left and right (within Reach) and how
// thick it is, so floor effects never hang over a ledge into the air
struct SFloorSpan
{
	float m_Left, m_Right, m_Bottom;
};
SFloorSpan FloorSpan(const CCollision *pCollision, float x, float Ground, float Reach)
{
	const float y = Ground + 4.0f;
	SFloorSpan Span{x, x, Ground};
	for(float d = 0.0f; d <= Reach; d += 2.0f)
	{
		if(!SolidInMap(pCollision, vec2(x - d, y)))
			break;
		Span.m_Left = x - d;
	}
	for(float d = 0.0f; d <= Reach; d += 2.0f)
	{
		if(!SolidInMap(pCollision, vec2(x + d, y)))
			break;
		Span.m_Right = x + d;
	}
	for(float d = 4.0f; d <= 96.0f; d += 4.0f)
	{
		if(!SolidInMap(pCollision, vec2(x, Ground + d)))
			break;
		Span.m_Bottom = Ground + d;
	}
	return Span;
}

// clip to a world rectangle (in the current world mapping)
void ClipWorld(IGraphics *pGraphics, const CScreenRect &Screen, float L, float T, float R, float B)
{
	const float W = Screen.m_BottomRight.x - Screen.m_TopLeft.x, H = Screen.m_BottomRight.y - Screen.m_TopLeft.y;
	const int x0 = (int)std::floor((L - Screen.m_TopLeft.x) / W * pGraphics->ScreenWidth());
	const int x1 = (int)std::ceil((R - Screen.m_TopLeft.x) / W * pGraphics->ScreenWidth());
	const int y0 = (int)std::floor((T - Screen.m_TopLeft.y) / H * pGraphics->ScreenHeight());
	const int y1 = (int)std::ceil((B - Screen.m_TopLeft.y) / H * pGraphics->ScreenHeight());
	const int cx0 = std::clamp(x0, 0, pGraphics->ScreenWidth()), cx1 = std::clamp(x1, 0, pGraphics->ScreenWidth());
	const int cy0 = std::clamp(y0, 0, pGraphics->ScreenHeight()), cy1 = std::clamp(y1, 0, pGraphics->ScreenHeight());
	pGraphics->ClipEnable(cx0, cy0, std::max(cx1 - cx0, 0), std::max(cy1 - cy0, 0));
}

} // namespace

float FogMusicPulse() { return gs_State.m_Pulse; }

void CFogFx::OnDeath(vec2 Pos)
{
	gs_State.m_vPendingDeaths.push_back(Pos);
}

void CFogFx::OnInit()
{
	if(m_Front)
		return;
	// a soft dot and a "hole" (clear middle, dark rim) made on the CPU
	const auto Make = [&](bool Hole) {
		CImageInfo Image;
		const int N = Hole ? 512 : 128;
		const float Half = N / 2.0f;
		Image.m_Width = Image.m_Height = N;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		Image.Allocate();
		for(int y = 0; y < N; y++)
			for(int x = 0; x < N; x++)
			{
				const float d = std::sqrt((x + 0.5f - Half) * (x + 0.5f - Half) + (y + 0.5f - Half) * (y + 0.5f - Half)) / Half;
				float a;
				if(Hole)
				{
					// a long, soft falloff like real light: clear in the middle, fading slowly to dark,
					// with a whisper of noise so the gradient has no visible bands
					const float t = std::clamp(d, 0.0f, 1.0f);
					const float Soft = t * t * (3.0f - 2.0f * t);
					a = std::pow(Soft, 1.35f) + ((x * 7919 + y * 104729) % 97 / 97.0f - 0.5f) * (2.0f / 255.0f);
					a = std::clamp(a, 0.0f, 1.0f);
				}
				else
				{
					const float t = std::clamp(1.0f - d, 0.0f, 1.0f);
					a = t * t;
				}
				uint8_t *p = &Image.m_pData[((size_t)y * N + x) * 4];
				p[0] = p[1] = p[2] = 255;
				p[3] = (uint8_t)std::lround(a * 255.0f);
			}
		return Graphics()->LoadTextureRawMove(Image, 0, Hole ? "fog_fx_hole" : "fog_fx_soft");
	};
	gs_State.m_Soft = Make(false);
	gs_State.m_Hole = Make(true);
	gs_State.m_Bat = Graphics()->LoadTexture("fog/holiday/bat.png", IStorage::TYPE_ALL);
	gs_State.m_Heart = Graphics()->LoadTexture("fog/holiday/heart.png", IStorage::TYPE_ALL);
	gs_State.m_Petal = Graphics()->LoadTexture("fog/holiday/petal.png", IStorage::TYPE_ALL);
}

void CFogFx::OnShutdown()
{
	if(m_Front)
		return;
	Graphics()->UnloadTexture(&gs_State.m_Soft);
	Graphics()->UnloadTexture(&gs_State.m_Hole);
	Graphics()->UnloadTexture(&gs_State.m_Bat);
	Graphics()->UnloadTexture(&gs_State.m_Heart);
	Graphics()->UnloadTexture(&gs_State.m_Petal);
}

void CFogFx::OnMapLoad()
{
	if(m_Front)
		return;
	gs_State.m_vParticles.clear();
	gs_State.m_vWeather.clear();
	gs_State.m_vHoliday.clear();
	for(auto &Tee : gs_State.m_aTees)
		Tee = STee();
}

void CFogFx::Update(float Dt)
{
	SState &S = gs_State;
	const float Now = Client()->GlobalTime();
	CCollision *pCollision = Collision();

	// music pulse: no audio analysis, a calm pseudo beat while a track plays
	{
		float Target = 0.0f;
		if(g_Config.m_FogMusicPulse && GameClient()->m_FogMusic.Media().m_Playing)
			Target = std::pow(std::max(0.0f, std::sin(Now * pi * 2.0f * 1.9f)), 6.0f) * 0.8f + 0.2f * (0.5f + 0.5f * std::sin(Now * 0.7f));
		S.m_Pulse += (Target - S.m_Pulse) * std::min(Dt * 10.0f, 1.0f);
	}

	// deaths
	for(const vec2 &Pos : S.m_vPendingDeaths)
	{
		if(!g_Config.m_FogFunDeathAsh)
			continue;
		for(int i = 0; i < 70; i++)
		{
			SParticle p;
			p.m_Pos = Pos + vec2(RandRange(-14, 14), RandRange(-14, 14));
			p.m_Vel = vec2(RandRange(-50, 50), RandRange(-120, -20));
			p.m_MaxLife = RandRange(1.4f, 2.8f);
			p.m_Size = RandRange(5.0f, 11.0f);
			p.m_Drag = 1.2f;
			p.m_Gravity = -30.0f;
			const float g = RandRange(0.18f, 0.7f);
			p.m_Color = i % 9 == 0 ? ColorRGBA(1.0f, 0.55f, 0.25f, 1.0f) : ColorRGBA(g, g, g + 0.03f, 1.0f);
			p.m_Type = i % 9 == 0 ? P_SPARK : P_PUFF;
			p.m_Front = true;
			Spawn(p);
		}
	}
	S.m_vPendingDeaths.clear();

	// the tees
	const int Local = GameClient()->m_Snap.m_LocalClientId;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		STee &Tee = S.m_aTees[i];
		const auto &Client = GameClient()->m_aClients[i];
		const bool Active = GameClient()->m_Snap.m_aCharacters[i].m_Active;
		if(!Active)
		{
			Tee.m_Valid = false;
			continue;
		}
		const vec2 Pos = Client.m_RenderPos;
		if(!Tee.m_Valid || distance(Pos, Tee.m_Pos) > 400.0f)
		{
			Tee = STee();
			Tee.m_Pos = Tee.m_PrevPos = Pos;
			Tee.m_Valid = true;
			Tee.m_HookState = Client.m_RenderCur.m_HookState;
			continue;
		}
		Tee.m_PrevPos = Tee.m_Pos;
		Tee.m_Pos = Pos;
		Tee.m_PrevVel = Tee.m_Vel;
		if(Dt > 0.0f)
			Tee.m_Vel = mix(Tee.m_Vel, (Tee.m_Pos - Tee.m_PrevPos) / Dt, std::min(Dt * 20.0f, 1.0f));
		// the floor below (up to three tiles)
		Tee.m_HasGround = false;
		for(float d = 0.0f; d <= 110.0f; d += 4.0f)
			if(SolidInMap(pCollision, vec2(Pos.x, Pos.y + d)))
			{
				Tee.m_Ground = std::floor(round_to_int(Pos.y + d) / 32.0f) * 32.0f;
				Tee.m_HasGround = true;
				break;
			}
		Tee.m_WasGrounded = Tee.m_Grounded;
		Tee.m_Grounded = Tee.m_HasGround && Tee.m_Ground - Pos.y < 18.0f;

		// footprints
		if(g_Config.m_FogFunFootprints && Tee.m_Grounded && std::abs(Tee.m_Vel.x) > 60.0f)
		{
			Tee.m_PrintDist += std::abs(Tee.m_Pos.x - Tee.m_PrevPos.x);
			if(Tee.m_PrintDist > 18.0f)
			{
				Tee.m_PrintDist = 0.0f;
				Tee.m_PrintLeft = !Tee.m_PrintLeft;
				SParticle p;
				p.m_Pos = vec2(Pos.x + (Tee.m_PrintLeft ? -5.0f : 5.0f), Tee.m_Ground + 2.0f);
				p.m_Vel = vec2(0, 0);
				p.m_MaxLife = 3.2f;
				p.m_Size = 7.0f;
				p.m_Color = ColorRGBA(0.05f, 0.06f, 0.08f, 0.4f);
				p.m_Type = P_PRINT;
				Spawn(p);
			}
		}
		// dust: landing, jumping, hard stops
		if(g_Config.m_FogFunDust)
		{
			const auto Dust = [&](vec2 At, int Count, float Spread, float Up) {
				for(int k = 0; k < Count; k++)
				{
					SParticle p;
					p.m_Pos = At + vec2(RandRange(-6, 6), 0);
					p.m_Vel = vec2(RandRange(-Spread, Spread), RandRange(-Up, -Up * 0.3f));
					p.m_MaxLife = RandRange(0.4f, 0.8f);
					p.m_Size = RandRange(5.0f, 10.0f);
					p.m_Drag = 4.0f;
					p.m_Color = ColorRGBA(0.62f, 0.64f, 0.68f, 0.35f);
					Spawn(p);
				}
			};
			if(Tee.m_Grounded && !Tee.m_WasGrounded && Tee.m_PrevVel.y > 350.0f)
				Dust(vec2(Pos.x, Tee.m_Ground), 9, 140.0f, 60.0f);
			if(!Tee.m_Grounded && Tee.m_WasGrounded && Tee.m_Vel.y < -300.0f)
				Dust(vec2(Pos.x, Pos.y + 14.0f), 5, 70.0f, 40.0f);
			if(Tee.m_Grounded && std::abs(Tee.m_PrevVel.x) > 450.0f && std::abs(Tee.m_Vel.x) < 150.0f)
				Dust(vec2(Pos.x + (Tee.m_PrevVel.x > 0 ? 10.0f : -10.0f), Tee.m_Ground), 6, 90.0f, 30.0f);
		}
		// wind lines
		if(g_Config.m_FogFunWind && length(Tee.m_Vel) > 950.0f && Rand01() < Dt * 40.0f)
		{
			SParticle p;
			const vec2 Dir = normalize(Tee.m_Vel);
			p.m_Pos = Pos + vec2(-Dir.y, Dir.x) * RandRange(-16, 16) - Dir * RandRange(10, 30);
			p.m_Vel = Dir * 60.0f;
			p.m_MaxLife = 0.25f;
			p.m_Size = std::min(length(Tee.m_Vel) * 0.04f, 60.0f);
			p.m_Rot = std::atan2(Dir.y, Dir.x);
			p.m_Color = ColorRGBA(0.85f, 0.9f, 1.0f, 0.35f);
			p.m_Type = P_WIND;
			p.m_Front = true;
			Spawn(p);
		}
		// hook sparks
		const int HookState = Client.m_RenderCur.m_HookState;
		if(g_Config.m_FogFunHookSparks && HookState == HOOK_GRABBED && Tee.m_HookState != HOOK_GRABBED)
		{
			const vec2 HookPos(Client.m_RenderCur.m_HookX, Client.m_RenderCur.m_HookY);
			SParticle Flash;
			Flash.m_Pos = HookPos;
			Flash.m_MaxLife = 0.18f;
			Flash.m_Size = 26.0f;
			Flash.m_Color = ColorRGBA(0.9f, 0.95f, 1.0f, 0.8f);
			Flash.m_Type = P_FLASH;
			Flash.m_Front = true;
			Spawn(Flash);
			for(int k = 0; k < 10; k++)
			{
				SParticle p;
				p.m_Pos = HookPos;
				const float a = RandRange(0.0f, pi * 2.0f);
				p.m_Vel = vec2(std::cos(a), std::sin(a)) * RandRange(120.0f, 320.0f);
				p.m_MaxLife = RandRange(0.2f, 0.45f);
				p.m_Size = RandRange(4.0f, 8.0f);
				p.m_Gravity = 600.0f;
				p.m_Color = ColorRGBA(0.95f, 0.92f, 0.8f, 1.0f);
				p.m_Type = P_SPARK;
				p.m_Front = true;
				Spawn(p);
			}
		}
		Tee.m_HookState = HookState;
		// frost: thaw drops when the freeze ends
		const bool Frozen = Client.m_FreezeEnd != 0;
		if(Frozen && !Tee.m_Frozen)
			Tee.m_FrozenSince = Now;
		if(!Frozen && Tee.m_Frozen && g_Config.m_FogFunFrost)
			for(int k = 0; k < 10; k++)
			{
				SParticle p;
				p.m_Pos = Pos + vec2(RandRange(-12, 12), RandRange(-10, 6));
				p.m_Vel = vec2(RandRange(-20, 20), RandRange(0, 40));
				p.m_MaxLife = RandRange(0.6f, 1.1f);
				p.m_Size = RandRange(3.0f, 5.0f);
				p.m_Gravity = 700.0f;
				p.m_Color = ColorRGBA(0.75f, 0.88f, 1.0f, 0.8f);
				p.m_Type = P_DROP;
				p.m_Front = true;
				Spawn(p);
			}
		Tee.m_Frozen = Frozen;
		(void)Local;
	}

	// finish: tears and confetti
	const int FinishTick = GameClient()->m_Hud.FogFinishTick();
	if(S.m_LastFinishTick >= 0 && FinishTick != S.m_LastFinishTick && FinishTick > 0 && g_Config.m_FogFunFinish && Local >= 0 && S.m_aTees[Local].m_Valid)
	{
		S.m_FinishStart = Now;
		const vec2 Pos = S.m_aTees[Local].m_Pos;
		for(int k = 0; k < 90; k++)
		{
			SParticle p;
			p.m_Pos = Pos + vec2(RandRange(-30, 30), -40.0f);
			p.m_Vel = vec2(RandRange(-260, 260), RandRange(-520, -200));
			p.m_MaxLife = RandRange(1.8f, 3.2f);
			p.m_Size = RandRange(4.0f, 7.0f);
			p.m_Gravity = 420.0f;
			p.m_Drag = 1.6f;
			p.m_Rot = RandRange(0, 6.28f);
			p.m_RotVel = RandRange(-9, 9);
			const float g = RandRange(0.55f, 0.95f);
			p.m_Color = ColorRGBA(g, g + 0.01f, g + 0.04f, 1.0f);
			p.m_Type = P_CONFETTI;
			p.m_Front = true;
			Spawn(p);
		}
	}
	S.m_LastFinishTick = FinishTick;
	if(Local >= 0 && S.m_aTees[Local].m_Valid && Now - S.m_FinishStart < 4.0f && Rand01() < Dt * 6.0f)
	{
		// tears fall from the eyes for a few seconds
		const vec2 Pos = S.m_aTees[Local].m_Pos;
		SParticle p;
		p.m_Pos = Pos + vec2(Rand01() < 0.5f ? -5.0f : 6.0f, -2.0f);
		p.m_Vel = vec2(RandRange(-10, 10), 10.0f);
		p.m_MaxLife = 0.9f;
		p.m_Size = 3.5f;
		p.m_Gravity = 500.0f;
		p.m_Color = ColorRGBA(0.72f, 0.84f, 1.0f, 0.9f);
		p.m_Type = P_DROP;
		p.m_Front = true;
		Spawn(p);
	}

	// notes out of the headphones accessory
	if(g_Config.m_FogFunNotes && Local >= 0 && S.m_aTees[Local].m_Valid && GameClient()->m_FogMusic.Media().m_Playing && Now >= S.m_NextNote &&
		str_find(g_Config.m_FogTeeAccessories, "headphones") && g_Config.m_FogTeeAccessoriesOn)
	{
		S.m_NextNote = Now + RandRange(0.35f, 0.8f);
		SParticle p;
		p.m_Pos = S.m_aTees[Local].m_Pos + vec2(Rand01() < 0.5f ? -16.0f : 16.0f, -6.0f);
		p.m_Vel = vec2(RandRange(-25, 25), RandRange(-60, -35));
		p.m_MaxLife = 1.6f;
		p.m_Size = RandRange(7.0f, 10.0f);
		p.m_Rot = RandRange(-0.4f, 0.4f);
		p.m_Color = ColorRGBA(0.88f, 0.92f, 1.0f, 1.0f);
		p.m_Type = P_NOTE;
		p.m_Front = true;
		Spawn(p);
	}

	// thoughts
	if(Now > S.m_NextThought && Local >= 0 && S.m_aTees[Local].m_Valid)
	{
		S.m_ThoughtStart = Now;
		S.m_NextThought = Now + RandRange(25.0f, 45.0f);
		S.m_ThoughtKind = 0;
		if(GameClient()->m_FogMusic.Media().m_Playing)
			S.m_ThoughtKind = 1;
		for(int i = 0; i < MAX_CLIENTS; i++)
			if(i != Local && S.m_aTees[i].m_Valid && GameClient()->m_aClients[i].m_Friend && distance(S.m_aTees[i].m_Pos, S.m_aTees[Local].m_Pos) < 400.0f)
				S.m_ThoughtKind = 2;
	}

	// particles
	for(auto &p : S.m_vParticles)
	{
		p.m_Life += Dt;
		p.m_Vel.y += p.m_Gravity * Dt;
		p.m_Vel *= std::max(0.0f, 1.0f - p.m_Drag * Dt);
		if(p.m_Type == P_CONFETTI)
			p.m_Vel.x += std::sin(p.m_Life * 6.0f + p.m_Rot) * 120.0f * Dt;
		p.m_Pos += p.m_Vel * Dt;
		p.m_Rot += p.m_RotVel * Dt;
	}
	S.m_vParticles.erase(std::remove_if(S.m_vParticles.begin(), S.m_vParticles.end(), [](const SParticle &p) { return p.m_Life >= p.m_MaxLife; }), S.m_vParticles.end());

	// weather around the camera
	float Amount = 1.0f;
	const int Weather = EffectiveWeather(Amount);
	if(!Weather)
		S.m_vWeather.clear();
	else
	{
		const vec2 Center = GameClient()->m_Camera.m_Center;
		const float Zoom = GameClient()->m_Camera.m_Zoom;
		const float HalfW = 900.0f * Zoom * Graphics()->ScreenAspect() / 1.6f, HalfH = 560.0f * Zoom;
		const float Wind = g_Config.m_FogWeatherWind / 100.0f;
		const float Rate = (Weather == 1 ? 260.0f : Weather == 2 ? 90.0f : 60.0f) * Amount * Zoom;
		static float s_Spawn = 0.0f;
		s_Spawn += Rate * Dt;
		while(s_Spawn > 1.0f && S.m_vWeather.size() < 2500)
		{
			s_Spawn -= 1.0f;
			SWeather w;
			w.m_Pos = vec2(Center.x + RandRange(-HalfW * 1.3f, HalfW * 1.3f) - Wind * 200.0f, Center.y - HalfH - RandRange(0.0f, 60.0f));
			w.m_Seed = Rand01() * 100.0f;
			switch(Weather)
			{
			case 1: w.m_Vel = vec2(Wind * 260.0f, RandRange(1100.0f, 1400.0f)); w.m_Size = RandRange(14.0f, 22.0f); break;
			case 2: w.m_Vel = vec2(Wind * 60.0f, RandRange(50.0f, 90.0f)); w.m_Size = RandRange(2.5f, 5.0f); break;
			case 3: w.m_Vel = vec2(Wind * 70.0f, RandRange(25.0f, 60.0f)); w.m_Size = RandRange(2.0f, 4.5f); break;
			default: w.m_Vel = vec2(Wind * 80.0f, RandRange(20.0f, 45.0f)); w.m_Size = RandRange(2.0f, 3.5f); break;
			}
			S.m_vWeather.push_back(w);
		}
		for(auto &w : S.m_vWeather)
		{
			if(w.m_Settled >= 0.0f)
				continue;
			vec2 Vel = w.m_Vel;
			if(Weather >= 2)
				Vel.x += std::sin(Now * 1.3f + w.m_Seed) * (Weather == 4 ? 40.0f : 20.0f);
			const vec2 Next = w.m_Pos + Vel * Dt;
			if(SolidInMap(pCollision, Next))
			{
				if(Weather == 1)
				{
					// rain splashes into a few tiny drops
					for(int k = 0; k < 2; k++)
					{
						SParticle p;
						p.m_Pos = vec2(w.m_Pos.x, std::floor(round_to_int(Next.y) / 32.0f) * 32.0f - 1.0f);
						p.m_Vel = vec2(RandRange(-70, 70), RandRange(-110, -40));
						p.m_MaxLife = 0.3f;
						p.m_Size = 2.0f;
						p.m_Gravity = 800.0f;
						p.m_Color = ColorRGBA(0.75f, 0.82f, 0.92f, 0.6f);
						p.m_Type = P_DROP;
						p.m_Front = true;
						if(Rand01() < 0.5f)
							Spawn(p);
					}
					w.m_Pos.y = 1e9f; // removed below
				}
				else if(Weather == 2)
				{
					w.m_Pos.y = std::floor(round_to_int(Next.y) / 32.0f) * 32.0f - 1.5f;
					w.m_Settled = Now;
				}
				else
					w.m_Pos.y = 1e9f;
				continue;
			}
			w.m_Pos = Next;
		}
		S.m_vWeather.erase(std::remove_if(S.m_vWeather.begin(), S.m_vWeather.end(), [&](const SWeather &w) {
			return w.m_Pos.y > Center.y + HalfH + 80.0f || (w.m_Settled >= 0.0f && Now - w.m_Settled > 5.0f) || std::abs(w.m_Pos.x - Center.x) > HalfW * 1.8f;
		}),
			S.m_vWeather.end());
	}

	// holidays: bats, hearts, petals and fireworks around the camera
	{
		bool NewYearNight;
		const int Holiday = CurrentHoliday(NewYearNight);
		const vec2 Center = GameClient()->m_Camera.m_Center;
		const float Zoom = GameClient()->m_Camera.m_Zoom;
		const float HalfW = 900.0f * Zoom * Graphics()->ScreenAspect() / 1.6f, HalfH = 560.0f * Zoom;
		if(Holiday && Now >= S.m_NextHolidaySpawn && S.m_vHoliday.size() < 400)
		{
			SState::SHolidayBit b;
			b.m_Seed = Rand01() * 100.0f;
			if(Holiday == HOLIDAY_HALLOWEEN)
			{
				// a bat crossing the sky above you
				const bool FromLeft = Rand01() < 0.5f;
				b.m_Kind = 0;
				b.m_Pos = vec2(Center.x + (FromLeft ? -1.0f : 1.0f) * (HalfW + 60.0f), Center.y - RandRange(HalfH * 0.1f, HalfH * 0.45f));
				b.m_Vel = vec2((FromLeft ? 1.0f : -1.0f) * RandRange(160.0f, 280.0f), RandRange(-20.0f, 20.0f));
				b.m_MaxLife = (HalfW * 2.0f + 120.0f) / std::abs(b.m_Vel.x);
				b.m_Size = RandRange(18.0f, 34.0f);
				b.m_Color = ColorRGBA(0.07f, 0.06f, 0.09f, 0.9f);
				S.m_NextHolidaySpawn = Now + RandRange(1.5f, 4.5f);
			}
			else if(Holiday == HOLIDAY_VALENTINE || Holiday == HOLIDAY_MARCH8)
			{
				b.m_Kind = Holiday == HOLIDAY_VALENTINE ? 1 : 2;
				b.m_Pos = vec2(Center.x + RandRange(-HalfW, HalfW), Center.y - HalfH - 30.0f);
				b.m_Vel = vec2(RandRange(-20.0f, 20.0f), RandRange(35.0f, 70.0f));
				b.m_MaxLife = (HalfH * 2.0f + 60.0f) / b.m_Vel.y;
				b.m_Size = RandRange(8.0f, 15.0f);
				b.m_Rot = RandRange(-0.6f, 0.6f);
				b.m_RotVel = RandRange(-0.8f, 0.8f);
				b.m_Color = Holiday == HOLIDAY_VALENTINE ? ColorRGBA(0.95f, 0.55f, 0.66f, 0.75f) : (Rand01() < 0.5f ? ColorRGBA(1.0f, 0.86f, 0.9f, 0.8f) : ColorRGBA(1.0f, 0.95f, 0.75f, 0.8f));
				S.m_NextHolidaySpawn = Now + RandRange(0.2f, 0.55f);
			}
			else
				S.m_NextHolidaySpawn = Now + 1.0f;
			if(Holiday != HOLIDAY_NEW_YEAR)
				S.m_vHoliday.push_back(b);
		}
		// fireworks on New Year's night
		if(NewYearNight && Now >= S.m_NextFirework)
		{
			S.m_NextFirework = Now + RandRange(1.2f, 3.5f);
			const vec2 At(Center.x + RandRange(-HalfW * 0.45f, HalfW * 0.45f), Center.y - RandRange(HalfH * 0.25f, HalfH * 0.5f));
			const ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(Rand01(), 0.55f, 0.7f, 1.0f));
			{
				// the bright heart of the burst
				SState::SHolidayBit Flash;
				Flash.m_Kind = 3;
				Flash.m_Pos = At;
				Flash.m_Vel = vec2(0, 0);
				Flash.m_MaxLife = 0.35f;
				Flash.m_Size = 70.0f;
				Flash.m_Color = Color;
				S.m_vHoliday.push_back(Flash);
			}
			for(int k = 0; k < 70; k++)
			{
				SState::SHolidayBit b;
				b.m_Kind = 3;
				const float a = RandRange(0.0f, pi * 2.0f), Speed = RandRange(80.0f, 260.0f);
				b.m_Pos = At;
				b.m_Vel = vec2(std::cos(a), std::sin(a)) * Speed;
				b.m_MaxLife = RandRange(1.2f, 2.0f);
				b.m_Size = RandRange(7.0f, 12.0f);
				b.m_Gravity = 120.0f;
				b.m_Drag = 1.3f;
				b.m_Seed = Rand01() * 100.0f;
				b.m_Color = k % 6 == 0 ? ColorRGBA(1.0f, 0.97f, 0.9f, 1.0f) : Color;
				S.m_vHoliday.push_back(b);
			}
		}
		for(auto &b : S.m_vHoliday)
		{
			b.m_Life += Dt;
			b.m_Vel.y += b.m_Gravity * Dt;
			b.m_Vel *= std::max(0.0f, 1.0f - b.m_Drag * Dt);
			b.m_Pos += b.m_Vel * Dt;
			if(b.m_Kind == 0)
				b.m_Pos.y += std::sin(b.m_Life * 3.0f + b.m_Seed) * 40.0f * Dt;
			else if(b.m_Kind <= 2)
				b.m_Pos.x += std::sin(b.m_Life * 1.6f + b.m_Seed) * 30.0f * Dt;
			b.m_Rot += b.m_RotVel * Dt;
		}
		S.m_vHoliday.erase(std::remove_if(S.m_vHoliday.begin(), S.m_vHoliday.end(), [](const SState::SHolidayBit &b) { return b.m_Life >= b.m_MaxLife; }), S.m_vHoliday.end());
		if(!Holiday)
			S.m_vHoliday.clear();
	}

	// soft camera and the speed vignette
	if(Local >= 0 && S.m_aTees[Local].m_Valid)
	{
		const vec2 Vel = S.m_aTees[Local].m_Vel;
		const float Speed = length(Vel);
		const float VTarget = g_Config.m_FogSpeedVignette ? std::clamp((Speed - 700.0f) / 900.0f, 0.0f, 1.0f) : 0.0f;
		S.m_Vignette += (VTarget - S.m_Vignette) * std::min(Dt * 4.0f, 1.0f);
		vec2 Target = vec2(0, 0);
		if(g_Config.m_FogCameraLag)
		{
			Target = -Vel * (g_Config.m_FogCameraLag / 100.0f) * 0.06f;
			if(length(Target) > 90.0f)
				Target = normalize(Target) * 90.0f;
		}
		FogSpring(S.m_CamLag.x, S.m_CamLagVel.x, Target.x, Dt, 2.2f, 0.15f);
		FogSpring(S.m_CamLag.y, S.m_CamLagVel.y, Target.y, Dt, 2.2f, 0.15f);
	}
}

void CFogFx::RenderReflections()
{
	SState &S = gs_State;
	const CScreenRect Screen = Graphics()->GetScreen();
	float RainAmount;
	const float Rain = EffectiveWeather(RainAmount) == 1 ? 1.6f : 1.0f;
	const float Alpha = std::min(g_Config.m_FogReflectionsAlpha / 100.0f * Rain, 0.8f);
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const STee &Tee = S.m_aTees[i];
		if(!Tee.m_Valid || !Tee.m_HasGround)
			continue;
		const float Height = Tee.m_Ground - Tee.m_Pos.y;
		if(Height > 80.0f)
			continue;
		const float g = Tee.m_Ground;
		// mirror the world about the floor line and draw the tee again, clipped below the floor
		const SFloorSpan Span = FloorSpan(Collision(), Tee.m_Pos.x, g, 40.0f);
		if(Span.m_Right - Span.m_Left < 4.0f || Span.m_Bottom <= g)
			continue;
		ClipWorld(Graphics(), Screen, Span.m_Left - 1.0f, g, Span.m_Right + 1.0f, Span.m_Bottom + 4.0f);
		Graphics()->MapScreen(CScreenRect(vec2(Screen.m_TopLeft.x, 2.0f * g - Screen.m_TopLeft.y), vec2(Screen.m_BottomRight.x, 2.0f * g - Screen.m_BottomRight.y)));
		CTeeRenderInfo Info = GameClient()->m_aClients[i].m_RenderInfo;
		Info.m_Size = 64.0f;
		const float Angle = GameClient()->m_aClients[i].m_RenderCur.m_Angle / 256.0f;
		const vec2 Dir(std::cos(Angle), std::sin(Angle));
		RenderTools()->RenderTee(CAnimState::GetIdle(), &Info, EMOTE_NORMAL, Dir, Tee.m_Pos, Alpha * (1.0f - Height / 80.0f));
		Graphics()->MapScreen(Screen);
		Graphics()->ClipDisable();
	}
}

void CFogFx::RenderShadows()
{
	SState &S = gs_State;
	float Light, SunSide;
	ColorRGBA Tint;
	DayPhase(Light, SunSide, Tint);
	if(!g_Config.m_FogDayCycle)
		SunSide = 0.35f;
	const CScreenRect Screen = Graphics()->GetScreen();
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const STee &Tee = S.m_aTees[i];
		if(!Tee.m_Valid || !Tee.m_HasGround)
			continue;
		const float Height = Tee.m_Ground - Tee.m_Pos.y;
		if(Height > 100.0f)
			continue;
		// the shadow falls on the top face of the floor tiles, so draw it only below the floor line
		const SFloorSpan Span = FloorSpan(Collision(), Tee.m_Pos.x, Tee.m_Ground, 150.0f);
		if(Span.m_Right - Span.m_Left < 4.0f)
			continue;
		// only below the floor line; sideways the shadow fades out softly where the floor ends
		ClipWorld(Graphics(), Screen, Screen.m_TopLeft.x, Tee.m_Ground, Screen.m_BottomRight.x, Span.m_Bottom + 4.0f);
		const float Fade = 1.0f - Height / 100.0f;
		const float Len = TEE * (1.5f + std::abs(SunSide) * 2.4f);
		const float CenterX = Tee.m_Pos.x + SunSide * Len * 0.35f, ShadowH = TEE * 0.42f;
		const float X0 = CenterX - Len / 2.0f, Top = Tee.m_Ground + 2.0f - ShadowH / 2.0f;
		constexpr int SLICES = 32;
		constexpr float EDGE_SOFT = 12.0f; // world units over which the shadow fades at a ledge
		Graphics()->TextureSet(S.m_Soft);
		Graphics()->QuadsBegin();
		for(int k = 0; k < SLICES; k++)
		{
			const float u0 = k / (float)SLICES, u1 = (k + 1) / (float)SLICES;
			const float sx = X0 + (u0 + u1) * 0.5f * Len;
			const float d = std::min(sx - Span.m_Left, Span.m_Right - sx);
			float Edge = std::clamp((d + 2.0f) / EDGE_SOFT, 0.0f, 1.0f);
			Edge = Edge * Edge * (3.0f - 2.0f * Edge);
			if(Edge <= 0.0f)
				continue;
			Graphics()->QuadsSetSubset(u0, 0.0f, u1, 1.0f);
			Graphics()->SetColor(0.0f, 0.01f, 0.03f, 0.55f * Fade * Edge);
			IGraphics::CQuadItem Slice(X0 + u0 * Len, Top, Len / SLICES + 0.1f, ShadowH);
			Graphics()->QuadsDrawTL(&Slice, 1);
		}
		Graphics()->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
		Graphics()->QuadsEnd();
		Graphics()->ClipDisable();
	}
}

void CFogFx::RenderBack()
{
	SState &S = gs_State;
	// dust behind the tees (footprints, shadows and reflections lie on the floor tiles, which
	// are drawn after the tees, so they go in the front pass)
	Graphics()->TextureSet(S.m_Soft);
	Graphics()->QuadsBegin();
	for(const auto &p : S.m_vParticles)
	{
		if(p.m_Front || p.m_Type != P_PUFF)
			continue;
		const float t = p.m_Life / p.m_MaxLife;
		const float Alpha = p.m_Type == P_PRINT ? (1.0f - t * t) : std::sin(t * pi) * 1.2f;
		Graphics()->SetColor(p.m_Color.r, p.m_Color.g, p.m_Color.b, p.m_Color.a * std::min(Alpha, 1.0f));
		const float Size = p.m_Type == P_PRINT ? p.m_Size : p.m_Size * (0.6f + t * 0.9f);
		IGraphics::CQuadItem Item(p.m_Pos.x, p.m_Pos.y, Size, p.m_Type == P_PRINT ? p.m_Size * 0.42f : Size);
		Graphics()->QuadsDraw(&Item, 1);
	}
	Graphics()->QuadsEnd();
}

void CFogFx::RenderFront()
{
	SState &S = gs_State;
	const float Now = Client()->GlobalTime();
	const int Local = GameClient()->m_Snap.m_LocalClientId;
	const CScreenRect Screen = Graphics()->GetScreen();

	// on the floor: shadows, reflections, footprints
	if(g_Config.m_FogShadows)
		RenderShadows();
	if(g_Config.m_FogReflections)
		RenderReflections();
	Graphics()->TextureSet(S.m_Soft);
	Graphics()->QuadsBegin();
	for(const auto &p : S.m_vParticles)
	{
		if(p.m_Type != P_PRINT)
			continue;
		const float t = p.m_Life / p.m_MaxLife;
		Graphics()->SetColor(p.m_Color.r, p.m_Color.g, p.m_Color.b, p.m_Color.a * (1.0f - t * t));
		IGraphics::CQuadItem Item(p.m_Pos.x, p.m_Pos.y, p.m_Size, p.m_Size * 0.42f);
		Graphics()->QuadsDraw(&Item, 1);
	}
	Graphics()->QuadsEnd();

	// weather
	float WeatherAmount;
	const int CurrentWeather = EffectiveWeather(WeatherAmount);
	if(CurrentWeather && !S.m_vWeather.empty())
	{
		const int Weather = CurrentWeather;
		if(Weather == 1)
		{
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			for(const auto &w : S.m_vWeather)
			{
				const float a = std::atan2(w.m_Vel.y, w.m_Vel.x) - pi / 2.0f;
				Graphics()->QuadsSetRotation(a);
				Graphics()->SetColor(0.72f, 0.78f, 0.88f, 0.32f);
				IGraphics::CQuadItem Item(w.m_Pos.x, w.m_Pos.y, 1.3f, w.m_Size);
				Graphics()->QuadsDraw(&Item, 1);
			}
			Graphics()->QuadsSetRotation(0.0f);
			Graphics()->QuadsEnd();
		}
		else
		{
			Graphics()->TextureSet(S.m_Soft);
			Graphics()->QuadsBegin();
			for(const auto &w : S.m_vWeather)
			{
				ColorRGBA c = Weather == 2 ? ColorRGBA(0.92f, 0.95f, 1.0f, 0.85f) : Weather == 3 ? ColorRGBA(0.45f, 0.45f, 0.47f, 0.75f) : ColorRGBA(1.0f, 0.92f, 0.65f, 0.7f);
				if(Weather == 3 && std::fmod(w.m_Seed, 7.0f) < 0.6f)
					c = ColorRGBA(1.0f, 0.5f, 0.2f, 0.6f + 0.4f * std::sin(Now * 9.0f + w.m_Seed)); // a live ember now and then
				if(w.m_Settled >= 0.0f)
					c.a *= std::clamp(1.0f - (Now - w.m_Settled) / 5.0f, 0.0f, 1.0f);
				Graphics()->SetColor(c.r, c.g, c.b, c.a);
				IGraphics::CQuadItem Item(w.m_Pos.x, w.m_Pos.y, w.m_Size * 2.0f, w.m_Size * 2.0f);
				Graphics()->QuadsDraw(&Item, 1);
			}
			Graphics()->QuadsEnd();
		}
	}

	// frost on frozen tees
	if(g_Config.m_FogFunFrost)
	{
		Graphics()->TextureSet(S.m_Soft);
		Graphics()->BlendAdditive();
		Graphics()->QuadsBegin();
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			const STee &Tee = S.m_aTees[i];
			if(!Tee.m_Valid || !Tee.m_Frozen)
				continue;
			const float In = std::min((Now - Tee.m_FrozenSince) * 2.0f, 1.0f);
			Graphics()->SetColor(0.55f, 0.75f, 1.0f, 0.18f * In);
			IGraphics::CQuadItem Halo(Tee.m_Pos.x, Tee.m_Pos.y, TEE * 2.0f, TEE * 2.0f);
			Graphics()->QuadsDraw(&Halo, 1);
			for(int k = 0; k < 7; k++)
			{
				const float a = k * 0.9f + i;
				const vec2 At = Tee.m_Pos + vec2(std::cos(a) * 11.0f, std::sin(a * 1.3f) * 11.0f);
				const float Twinkle = std::pow(0.5f + 0.5f * std::sin(Now * 4.0f + k * 2.1f + i), 3.0f);
				Graphics()->SetColor(0.85f, 0.95f, 1.0f, (0.25f + 0.75f * Twinkle) * In);
				IGraphics::CQuadItem Sparkle(At.x, At.y, 3.0f + 4.0f * Twinkle, 3.0f + 4.0f * Twinkle);
				Graphics()->QuadsDraw(&Sparkle, 1);
			}
		}
		Graphics()->QuadsEnd();
		Graphics()->BlendNormal();
	}

	// front particles
	for(int Pass = 0; Pass < 2; Pass++) // normal, then additive
	{
		if(Pass == 1)
			Graphics()->BlendAdditive();
		Graphics()->TextureSet(S.m_Soft);
		Graphics()->QuadsBegin();
		for(const auto &p : S.m_vParticles)
		{
			if(!p.m_Front || p.m_Type == P_NOTE || p.m_Type == P_CONFETTI)
				continue;
			const bool Additive = p.m_Type == P_SPARK || p.m_Type == P_FLASH || p.m_Type == P_WIND;
			if(Additive != (Pass == 1))
				continue;
			const float t = p.m_Life / p.m_MaxLife;
			float a = p.m_Color.a * (1.0f - t);
			float w = p.m_Size, h = p.m_Size;
			float Rot = 0.0f;
			if(p.m_Type == P_SPARK || p.m_Type == P_WIND)
			{
				Rot = p.m_Type == P_WIND ? p.m_Rot : std::atan2(p.m_Vel.y, p.m_Vel.x);
				w = p.m_Size * (p.m_Type == P_WIND ? 1.0f : 1.6f);
				h = p.m_Type == P_WIND ? 1.2f : 2.0f;
				if(p.m_Type == P_WIND)
					a = p.m_Color.a * std::sin(t * pi);
			}
			else if(p.m_Type == P_FLASH)
				w = h = p.m_Size * (0.6f + t);
			else if(p.m_Type == P_PUFF)
			{
				w = h = p.m_Size * (0.7f + t * 0.8f);
				a = p.m_Color.a * std::sin(t * pi) * 0.9f;
			}
			else if(p.m_Type == P_DROP)
				h = p.m_Size * 1.4f;
			Graphics()->QuadsSetRotation(Rot);
			Graphics()->SetColor(p.m_Color.r, p.m_Color.g, p.m_Color.b, a);
			IGraphics::CQuadItem Item(p.m_Pos.x, p.m_Pos.y, w, h);
			Graphics()->QuadsDraw(&Item, 1);
		}
		Graphics()->QuadsSetRotation(0.0f);
		Graphics()->QuadsEnd();
		if(Pass == 1)
			Graphics()->BlendNormal();
	}
	// confetti: flat paper bits that turn as they flutter
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	for(const auto &p : S.m_vParticles)
	{
		if(p.m_Type != P_CONFETTI)
			continue;
		const float t = p.m_Life / p.m_MaxLife;
		const float Flip = std::abs(std::cos(p.m_Rot * 1.7f));
		Graphics()->QuadsSetRotation(p.m_Rot);
		Graphics()->SetColor(p.m_Color.r * (0.7f + 0.3f * Flip), p.m_Color.g * (0.7f + 0.3f * Flip), p.m_Color.b * (0.7f + 0.3f * Flip), 1.0f - t * t);
		IGraphics::CQuadItem Item(p.m_Pos.x, p.m_Pos.y, p.m_Size, p.m_Size * 0.55f * (0.2f + 0.8f * Flip));
		Graphics()->QuadsDraw(&Item, 1);
	}
	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->QuadsEnd();
	// notes
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	for(const auto &p : S.m_vParticles)
	{
		if(p.m_Type != P_NOTE)
			continue;
		const float t = p.m_Life / p.m_MaxLife;
		TextRender()->TextColor(p.m_Color.WithAlpha(std::sin(t * pi)));
		const float Sway = std::sin(p.m_Life * 4.0f) * 4.0f;
		TextRender()->Text(p.m_Pos.x + Sway - p.m_Size * 0.4f, p.m_Pos.y - p.m_Size * 0.5f, p.m_Size, FontIcon::MUSIC, -1.0f);
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

	// a thought bubble above your tee
	if(g_Config.m_FogFunThoughts && Local >= 0 && S.m_aTees[Local].m_Valid)
	{
		const float Since = Now - S.m_ThoughtStart;
		if(Since >= 0.0f && Since < 3.6f)
		{
			const float In = FogEaseOutBack(std::min(Since * 3.0f, 1.0f)) * std::clamp((3.6f - Since) / 0.4f, 0.0f, 1.0f);
			const vec2 Base = S.m_aTees[Local].m_Pos + vec2(18.0f, -30.0f);
			const vec2 Bubble = Base + vec2(10.0f, -18.0f - std::sin(Now * 2.0f) * 1.5f);
			Graphics()->TextureClear();
			const ColorRGBA Paper(0.92f, 0.94f, 0.97f, 0.88f * std::min(In, 1.0f));
			Graphics()->DrawRect(Base.x - 2.0f * In, Base.y - 2.0f * In, 4.0f * In, 4.0f * In, Paper, IGraphics::CORNER_ALL, 2.0f * In);
			Graphics()->DrawRect(Base.x + 4.0f - 3.0f * In, Base.y - 9.0f - 3.0f * In, 6.0f * In, 6.0f * In, Paper, IGraphics::CORNER_ALL, 3.0f * In);
			const float W = 30.0f * In, H = 20.0f * In;
			Graphics()->DrawRect(Bubble.x - W / 2.0f, Bubble.y - H / 2.0f, W, H, Paper, IGraphics::CORNER_ALL, H / 2.0f);
			const ColorRGBA Ink(0.25f, 0.28f, 0.34f, std::min(In, 1.0f));
			if(S.m_ThoughtKind == 0)
			{
				for(int k = 0; k < 3; k++)
				{
					const float Hop = std::max(0.0f, std::sin(Now * 5.0f - k * 0.8f)) * 2.0f;
					Graphics()->DrawRect(Bubble.x - 8.0f + k * 7.0f - 1.6f, Bubble.y - 1.6f - Hop, 3.2f, 3.2f, Ink, IGraphics::CORNER_ALL, 1.6f);
				}
			}
			else
			{
				TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
				TextRender()->TextColor(S.m_ThoughtKind == 2 ? ColorRGBA(0.85f, 0.45f, 0.52f, Ink.a) : Ink);
				const char *pIcon = S.m_ThoughtKind == 2 ? FontIcon::HEART : FontIcon::MUSIC;
				const float Size = 10.0f * In;
				const float TextW = TextRender()->TextWidth(Size, pIcon);
				TextRender()->Text(Bubble.x - TextW / 2.0f, Bubble.y - Size * 0.62f, Size, pIcon, -1.0f);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
				TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			}
		}
	}

	// holidays
	{
		bool NewYearNight;
		const int Holiday = CurrentHoliday(NewYearNight);
		for(int Pass = 0; Pass < 2; Pass++)
			for(const auto &b : S.m_vHoliday)
			{
				const bool Spark = b.m_Kind == 3;
				if(Spark != (Pass == 1))
					continue;
				const float t = b.m_Life / b.m_MaxLife;
				IGraphics::CTextureHandle Tex = b.m_Kind == 0 ? S.m_Bat : b.m_Kind == 1 ? S.m_Heart : b.m_Kind == 2 ? S.m_Petal : S.m_Soft;
				if(Spark)
					Graphics()->BlendAdditive();
				Graphics()->TextureSet(Tex);
				Graphics()->QuadsBegin();
				float w = b.m_Size, h = b.m_Size, a = b.m_Color.a;
				if(b.m_Kind == 0)
				{
					h = b.m_Size * (0.35f + 0.65f * std::abs(std::sin(b.m_Life * 9.0f + b.m_Seed))); // wing beats
					a *= std::min(t * 6.0f, 1.0f) * std::min((1.0f - t) * 6.0f, 1.0f);
					Graphics()->QuadsSetSubset(b.m_Vel.x < 0.0f ? 1.0f : 0.0f, 0.0f, b.m_Vel.x < 0.0f ? 0.0f : 1.0f, 1.0f);
				}
				else if(Spark)
				{
					a *= (1.0f - t) * (0.7f + 0.3f * std::sin(b.m_Life * 30.0f + b.m_Seed));
					w = h = b.m_Size * (1.0f - t * 0.5f);
				}
				else
					a *= std::min(t * 5.0f, 1.0f) * std::min((1.0f - t) * 5.0f, 1.0f);
				Graphics()->QuadsSetRotation(b.m_Rot);
				Graphics()->SetColor(b.m_Color.r, b.m_Color.g, b.m_Color.b, a);
				IGraphics::CQuadItem Item(b.m_Pos.x, b.m_Pos.y, w, h);
				Graphics()->QuadsDraw(&Item, 1);
				Graphics()->QuadsSetRotation(0.0f);
				Graphics()->QuadsEnd();
				if(Spark)
					Graphics()->BlendNormal();
			}
		// the holiday's light over the world
		ColorRGBA Tint(0, 0, 0, 0);
		if(Holiday == HOLIDAY_HALLOWEEN)
			Tint = ColorRGBA(0.32f, 0.14f, 0.42f, 0.16f);
		else if(Holiday == HOLIDAY_NEW_YEAR)
			Tint = ColorRGBA(0.62f, 0.76f, 1.0f, 0.06f);
		else if(Holiday == HOLIDAY_VALENTINE)
			Tint = ColorRGBA(1.0f, 0.62f, 0.74f, 0.06f);
		else if(Holiday == HOLIDAY_MARCH8)
			Tint = ColorRGBA(1.0f, 0.92f, 0.6f, 0.05f);
		if(Tint.a > 0.0f)
		{
			Graphics()->TextureClear();
			const float W = Screen.m_BottomRight.x - Screen.m_TopLeft.x, H = Screen.m_BottomRight.y - Screen.m_TopLeft.y;
			Graphics()->DrawRect(Screen.m_TopLeft.x, Screen.m_TopLeft.y, W, H, Tint, IGraphics::CORNER_NONE, 0.0f);
			if(Holiday == HOLIDAY_HALLOWEEN)
			{
				// a low orange glow rising from the bottom, like distant lanterns
				const ColorRGBA Glow(0.95f, 0.45f, 0.15f, 0.10f), Clear(0.95f, 0.45f, 0.15f, 0.0f);
				Graphics()->DrawRect4(Screen.m_TopLeft.x, Screen.m_TopLeft.y + H * 0.6f, W, H * 0.4f, Clear, Clear, Glow, Glow, IGraphics::CORNER_NONE, 0.0f);
			}
		}
		// New Year: a soft garland of lights sagging across the top of the screen
		if(Holiday == HOLIDAY_NEW_YEAR)
		{
			Graphics()->MapScreen(CScreenRect(0.0f, 0.0f, 100.0f * Graphics()->ScreenAspect(), 100.0f));
			const float W = 100.0f * Graphics()->ScreenAspect();
			static const ColorRGBA s_aBulbs[] = {ColorRGBA(1.0f, 0.85f, 0.6f, 1), ColorRGBA(0.95f, 0.45f, 0.45f, 1), ColorRGBA(0.5f, 0.85f, 0.6f, 1), ColorRGBA(0.55f, 0.7f, 1.0f, 1), ColorRGBA(1.0f, 0.75f, 0.35f, 1)};
			Graphics()->BlendAdditive();
			Graphics()->TextureSet(S.m_Soft);
			Graphics()->QuadsBegin();
			const int Count = 22;
			for(int k = 0; k < Count; k++)
			{
				const float x = (k + 0.5f) / Count * W;
				const float Segment = std::fmod(x / W * 3.0f, 1.0f); // three sagging arcs
				const float y = 2.5f + 5.0f * std::sin(Segment * pi);
				const float Twinkle = 0.45f + 0.55f * std::pow(0.5f + 0.5f * std::sin(Now * 1.7f + k * 2.3f), 2.0f);
				const ColorRGBA &c = s_aBulbs[k % 5];
				Graphics()->SetColor(c.r, c.g, c.b, 0.35f * Twinkle);
				IGraphics::CQuadItem Glow(x, y, 7.0f, 7.0f);
				Graphics()->QuadsDraw(&Glow, 1);
				Graphics()->SetColor(c.r, c.g, c.b, 0.9f * Twinkle);
				IGraphics::CQuadItem Bulb(x, y, 1.6f, 1.6f);
				Graphics()->QuadsDraw(&Bulb, 1);
			}
			Graphics()->QuadsEnd();
			Graphics()->BlendNormal();
			Graphics()->MapScreen(Screen);
		}
	}

	// time of day over the world
	const float WorldW = Screen.m_BottomRight.x - Screen.m_TopLeft.x, WorldH = Screen.m_BottomRight.y - Screen.m_TopLeft.y;
	if(g_Config.m_FogDayCycle)
	{
		float Light, SunSide;
		ColorRGBA Tint;
		DayPhase(Light, SunSide, Tint);
		Graphics()->TextureClear();
		Graphics()->DrawRect(Screen.m_TopLeft.x, Screen.m_TopLeft.y, WorldW, WorldH, Tint.WithAlpha(Tint.a * g_Config.m_FogDayCycleStrength / 100.0f), IGraphics::CORNER_NONE, 0.0f);
	}

	// the flashlight: dark everywhere except a soft circle around you
	if(g_Config.m_FogFlashlight && Local >= 0 && S.m_aTees[Local].m_Valid)
	{
		const vec2 P = S.m_aTees[Local].m_Pos;
		// the soft edge reaches well past the radius you set, so the light fades out naturally
		const float R = (float)g_Config.m_FogFlashlightRadius * 1.7f * (1.0f + 0.04f * std::sin(Now * 2.3f));
		const ColorRGBA Dark(0.0f, 0.01f, 0.02f, g_Config.m_FogFlashlightDark / 100.0f);
		const float L = Screen.m_TopLeft.x, T = Screen.m_TopLeft.y, Rr = Screen.m_BottomRight.x, B = Screen.m_BottomRight.y;
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(Dark.r, Dark.g, Dark.b, Dark.a);
		const IGraphics::CQuadItem aRects[] = {
			IGraphics::CQuadItem(L, T, Rr - L, std::max(0.0f, P.y - R - T)),
			IGraphics::CQuadItem(L, P.y + R, Rr - L, std::max(0.0f, B - (P.y + R))),
			IGraphics::CQuadItem(L, P.y - R, std::max(0.0f, P.x - R - L), R * 2.0f),
			IGraphics::CQuadItem(P.x + R, P.y - R, std::max(0.0f, Rr - (P.x + R)), R * 2.0f),
		};
		Graphics()->QuadsDrawTL(aRects, 4);
		Graphics()->QuadsEnd();
		Graphics()->TextureSet(S.m_Hole);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(Dark.r, Dark.g, Dark.b, Dark.a);
		IGraphics::CQuadItem Hole(P.x - R, P.y - R, R * 2.0f, R * 2.0f);
		Graphics()->QuadsDrawTL(&Hole, 1);
		Graphics()->QuadsEnd();
		// a faint glow where your hook holds on
		const auto &Me = GameClient()->m_aClients[Local];
		if(Me.m_RenderCur.m_HookState == HOOK_GRABBED)
		{
			Graphics()->BlendAdditive();
			Graphics()->TextureSet(S.m_Soft);
			Graphics()->QuadsBegin();
			Graphics()->SetColor(0.8f, 0.85f, 1.0f, 0.18f);
			IGraphics::CQuadItem Glow(Me.m_RenderCur.m_HookX - 70.0f, Me.m_RenderCur.m_HookY - 70.0f, 140.0f, 140.0f);
			Graphics()->QuadsDrawTL(&Glow, 1);
			Graphics()->QuadsEnd();
			Graphics()->BlendNormal();
		}
	}

	// the speed vignette, in screen space
	if(S.m_Vignette > 0.01f)
	{
		Graphics()->MapScreen(CScreenRect(0.0f, 0.0f, 100.0f, 100.0f));
		Graphics()->TextureClear();
		const float a = S.m_Vignette * 0.45f;
		const ColorRGBA Edge(0.0f, 0.01f, 0.03f, a), Clear(0.0f, 0.01f, 0.03f, 0.0f);
		const float D = 16.0f;
		Graphics()->DrawRect4(0.0f, 0.0f, 100.0f, D, Edge, Edge, Clear, Clear, IGraphics::CORNER_NONE, 0.0f);
		Graphics()->DrawRect4(0.0f, 100.0f - D, 100.0f, D, Clear, Clear, Edge, Edge, IGraphics::CORNER_NONE, 0.0f);
		Graphics()->DrawRect4(0.0f, 0.0f, D, 100.0f, Edge, Clear, Edge, Clear, IGraphics::CORNER_NONE, 0.0f);
		Graphics()->DrawRect4(100.0f - D, 0.0f, D, 100.0f, Clear, Edge, Clear, Edge, IGraphics::CORNER_NONE, 0.0f);
		Graphics()->MapScreen(Screen);
	}
}

void CFogFx::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	const CScreenRect OldScreen = Graphics()->GetScreen();
	// world mapping (no parallax), like the players
	const vec2 Center = GameClient()->m_Camera.m_Center;
	const CScreenRect World = Graphics()->MapScreenToWorld(Center.x, Center.y, 100.0f, 100.0f, 100.0f, 0.0f, 0.0f, Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom);
	Graphics()->MapScreen(World);
	if(!m_Front)
	{
		const float Now = Client()->GlobalTime();
		const float Dt = gs_State.m_LastTime < 0.0f ? 0.0f : std::clamp(Now - gs_State.m_LastTime, 0.0f, 0.1f);
		gs_State.m_LastTime = Now;
		Update(Dt);
		RenderBack();
	}
	else
		RenderFront();
	Graphics()->MapScreen(OldScreen);
}

vec2 FogCameraLag() { return gs_State.m_CamLag; }
