#include "trails.h"

#include <base/math.h>
#include <base/time.h>

#include <engine/graphics.h>
#include <engine/image.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr float TILE = 32.0f;

// Edgerunners-like afterimage palette: each copy takes the next color
constexpr ColorRGBA SANDEVISTAN_PALETTE[] = {
	ColorRGBA(0.42f, 1.00f, 0.54f, 1.0f), // lime
	ColorRGBA(0.24f, 0.91f, 1.00f, 1.0f), // cyan
	ColorRGBA(0.55f, 0.42f, 1.00f, 1.0f), // violet
	ColorRGBA(1.00f, 0.31f, 0.85f, 1.0f), // magenta
	ColorRGBA(1.00f, 0.89f, 0.36f, 1.0f), // yellow
};

float Hash01(int a)
{
	unsigned h = (unsigned)a * 2654435761u;
	h ^= h >> 15;
	h *= 2246822519u;
	h ^= h >> 13;
	return (h & 0xffff) / 65536.0f;
}

ColorRGBA ConfigColor(unsigned Packed)
{
	return color_cast<ColorRGBA>(ColorHSLA(Packed));
}

} // namespace

void CFogTrails::PresetTiming(int Preset, int &IntervalMs, int &LifetimeMs, int &Spacing)
{
	switch(Preset)
	{
	case PRESET_FOG: IntervalMs = 20, LifetimeMs = 650, Spacing = 14; break;
	case PRESET_GLITCH: IntervalMs = 60, LifetimeMs = 280, Spacing = 40; break;
	case PRESET_PHANTOM: IntervalMs = 90, LifetimeMs = 700, Spacing = 70; break;
	case PRESET_CUSTOM: IntervalMs = 35, LifetimeMs = 500, Spacing = 36; break;
	default: IntervalMs = 30, LifetimeMs = 450, Spacing = 44; break;
	}
}

void CFogTrails::ReleaseEcho(SEcho &Echo)
{
	if(Echo.m_Shape.IsValid())
		Graphics()->UnloadTexture(&Echo.m_Shape);
	if(Echo.m_Glow.IsValid())
		Graphics()->UnloadTexture(&Echo.m_Glow);
}

void CFogTrails::OnReset()
{
	for(STrack &Track : m_aTracks)
	{
		for(SEcho &Echo : Track.m_Echoes)
			ReleaseEcho(Echo);
		Track = STrack();
	}
}

void CFogTrails::OnShutdown()
{
	OnReset();
}

namespace {

float SampleMask(const std::vector<uint8_t> &vMask, int W, int H, float u, float v)
{
	if(u <= 0.0f || v <= 0.0f || u >= 1.0f || v >= 1.0f || vMask.empty())
		return 0.0f;
	const float x = u * W - 0.5f, y = v * H - 0.5f;
	const int x0 = std::clamp((int)std::floor(x), 0, W - 1), y0 = std::clamp((int)std::floor(y), 0, H - 1);
	const int x1 = std::min(x0 + 1, W - 1), y1 = std::min(y0 + 1, H - 1);
	const float fx = std::clamp(x - x0, 0.0f, 1.0f), fy = std::clamp(y - y0, 0.0f, 1.0f);
	const float a = mix((float)vMask[(size_t)y0 * W + x0], (float)vMask[(size_t)y0 * W + x1], fx);
	const float b = mix((float)vMask[(size_t)y1 * W + x0], (float)vMask[(size_t)y1 * W + x1], fx);
	return mix(a, b, fy) / 255.0f;
}

void Blur(std::vector<float> &vData, int N, float Sigma)
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
		for(int y = 0; y < N; y++)
			for(int x = 0; x < N; x++)
			{
				float Acc = 0.0f;
				for(int k = -Radius; k <= Radius; k++)
				{
					const int sx = Pass == 0 ? x + k : x, sy = Pass == 0 ? y : y + k;
					if(sx >= 0 && sx < N && sy >= 0 && sy < N)
						Acc += vIn[(size_t)sy * N + sx] * vKernel[k + Radius];
				}
				vOut[(size_t)y * N + x] = Acc;
			}
	}
}

IGraphics::CTextureHandle UploadAlpha(IGraphics *pGraphics, const std::vector<float> &vAlpha, int N, float Scale)
{
	std::vector<uint8_t> vPixels((size_t)N * N * 4);
	for(size_t i = 0; i < (size_t)N * N; i++)
	{
		vPixels[i * 4] = vPixels[i * 4 + 1] = vPixels[i * 4 + 2] = 255;
		vPixels[i * 4 + 3] = (uint8_t)std::lround(std::clamp(vAlpha[i] * Scale, 0.0f, 1.0f) * 255.0f);
	}
	CImageInfo Image;
	Image.m_Width = N;
	Image.m_Height = N;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.m_pData = vPixels.data();
	IGraphics::CTextureHandle Texture = pGraphics->LoadTextureRaw(Image, 0, "fog_trail_echo");
	Image.m_pData = nullptr;
	return Texture;
}

vec2 RotateVec(vec2 v, float Angle)
{
	const float c = std::cos(Angle), s = std::sin(Angle);
	return vec2(v.x * c - v.y * s, v.x * s + v.y * c);
}

} // namespace

// Rebuilds the tee pose exactly like CRenderTools::RenderTee6 (body quad, two rotated feet,
// mirrored feet when flipped), but as max() of the outline masks: one flat shape, one alpha.
bool CFogTrails::BuildSilhouette(SEcho &Echo)
{
	const CSkin::SSilhouetteMasks *pMasks = Echo.m_RenderInfo.m_pSilhouette.get();
	if(!pMasks || pMasks->m_vBody.empty() || pMasks->m_vFoot.empty())
		return false;

	float AnimScale, BaseSize;
	CRenderTools::GetRenderTeeAnimScaleAndBaseSize(&Echo.m_RenderInfo, AnimScale, BaseSize);
	// same as CRenderTools::GetRenderTeeBodyScale (private there)
	const float BodySize = g_Config.m_ClFatSkins ? BaseSize * 1.3f : BaseSize;

	const CAnimKeyframe *pBody = Echo.m_State.GetBody();
	const vec2 BodyPos = Echo.m_Position + vec2(pBody->m_X, pBody->m_Y) * AnimScale;
	const float BodyAngle = pBody->m_Angle * pi * 2.0f;
	const bool FlipFeet = Echo.m_Direction.x < 0 && Echo.m_RenderInfo.m_FeetFlipped;
	const CAnimKeyframe *apFeet[2] = {Echo.m_State.GetBackFoot(), Echo.m_State.GetFrontFoot()};

	// square canvas around the tee, with room for the bloom
	const float Half = BaseSize * 0.85f + BaseSize * 0.6f;
	const float PixelsPerUnit = 1.25f;
	const int N = std::clamp((int)std::ceil(Half * 2.0f * PixelsPerUnit), 16, 512);
	const vec2 Center = Echo.m_Position + vec2(0.0f, -BaseSize * 0.12f);

	std::vector<float> vAlpha((size_t)N * N, 0.0f);
	for(int py = 0; py < N; py++)
		for(int px = 0; px < N; px++)
		{
			const vec2 World = Center + vec2(((px + 0.5f) / N * 2.0f - 1.0f) * Half, ((py + 0.5f) / N * 2.0f - 1.0f) * Half);
			const vec2 LocalBody = RotateVec(World - BodyPos, -BodyAngle) / BodySize;
			float a = SampleMask(pMasks->m_vBody, pMasks->m_BodyW, pMasks->m_BodyH, LocalBody.x + 0.5f, LocalBody.y + 0.5f);
			for(const CAnimKeyframe *pFoot : apFeet)
			{
				const vec2 FootPos = Echo.m_Position + vec2(pFoot->m_X, pFoot->m_Y) * AnimScale;
				const vec2 Local = RotateVec(World - FootPos, -pFoot->m_Angle * pi * 2.0f);
				float u = Local.x / (64.0f * AnimScale) + 0.5f;
				const float v = Local.y / (32.0f * AnimScale) + 0.5f;
				if(FlipFeet)
					u = 1.0f - u;
				a = std::max(a, SampleMask(pMasks->m_vFoot, pMasks->m_FootW, pMasks->m_FootH, u, v));
			}
			vAlpha[(size_t)py * N + px] = a;
		}

	Echo.m_Shape = UploadAlpha(Graphics(), vAlpha, N, 1.0f);
	Blur(vAlpha, N, BaseSize * 0.11f * PixelsPerUnit);
	// fade the bloom out before the canvas edge, so no square border ever shows
	for(int py = 0; py < N; py++)
		for(int px = 0; px < N; px++)
		{
			const float Edge = std::min({px + 0.5f, py + 0.5f, N - px - 0.5f, N - py - 0.5f}) / (N * 0.12f);
			const float Window = std::clamp(Edge, 0.0f, 1.0f);
			vAlpha[(size_t)py * N + px] *= Window * Window * (3.0f - 2.0f * Window);
		}
	float Peak = 0.0f;
	for(float v : vAlpha)
		Peak = std::max(Peak, v);
	Echo.m_Glow = UploadAlpha(Graphics(), vAlpha, N, Peak > 0.0f ? 1.0f / Peak : 0.0f);
	Echo.m_ShapeCenter = Center;
	Echo.m_ShapeSize = Half * 2.0f;
	return true;
}

void CFogTrails::DrawShape(const SEcho &Echo, IGraphics::CTextureHandle Texture, vec2 Offset, float Scale, ColorRGBA Color)
{
	if(Color.a <= 0.002f)
		return;
	Graphics()->TextureSet(Texture);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(Color);
	IGraphics::CQuadItem Quad(Echo.m_ShapeCenter + Offset, vec2(Echo.m_ShapeSize, Echo.m_ShapeSize) * Scale);
	Graphics()->QuadsDraw(&Quad, 1);
	Graphics()->QuadsEnd();
}

void CFogTrails::Record(int ClientId, const CAnimState &State, const CTeeRenderInfo &RenderInfo, int Emote, vec2 Direction, vec2 Position)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !g_Config.m_FogTrail)
		return;
	if(!g_Config.m_FogTrailOthers && ClientId != GameClient()->m_Snap.m_LocalClientId)
		return;

	STrack &Track = m_aTracks[ClientId];
	const int64_t NowNs = time_get_nanoseconds().count();
	if(!Track.m_Valid || distance(Track.m_LastPos, Position) > TILE * 12.0f)
	{
		// first sight or a teleport: never connect two unrelated places
		Track.m_Valid = true;
		Track.m_LastPos = Position;
		Track.m_LastSampleNs = NowNs;
		Track.m_Speed = 0.0f;
		for(SEcho &Echo : Track.m_Echoes)
			ReleaseEcho(Echo);
		Track.m_Echoes.clear();
		return;
	}

	const float Dt = (NowNs - Track.m_LastSampleNs) / 1e9f;
	if(Dt <= 0.0005f)
		return;
	const float Instant = distance(Track.m_LastPos, Position) / Dt;
	Track.m_Speed = mix(Track.m_Speed, Instant, 1.0f - std::exp(-Dt * 18.0f));
	Track.m_LastPos = Position;
	Track.m_LastSampleNs = NowNs;

	if(Track.m_Speed < g_Config.m_FogTrailMinSpeed * TILE)
		return;
	if((NowNs - Track.m_LastSpawnNs) / 1e6f < g_Config.m_FogTrailInterval)
		return;
	// distinct copies, not a smear: wait until the tee has moved far enough
	if(!Track.m_Echoes.empty() && distance(Track.m_LastSpawnPos, Position) < g_Config.m_FogTrailSpacing)
		return;

	Track.m_LastSpawnNs = NowNs;
	Track.m_LastSpawnPos = Position;
	SEcho Echo;
	Echo.m_State = State;
	Echo.m_RenderInfo = RenderInfo;
	Echo.m_Emote = Emote;
	Echo.m_Direction = Direction;
	Echo.m_Position = Position;
	Echo.m_SpawnNs = NowNs;
	Echo.m_Serial = Track.m_Serial++;
	BuildSilhouette(Echo);
	Track.m_Echoes.push_back(Echo);
	while((int)Track.m_Echoes.size() > g_Config.m_FogTrailMax)
	{
		ReleaseEcho(Track.m_Echoes.front());
		Track.m_Echoes.pop_front();
	}
}

ColorRGBA CFogTrails::EchoColor(int Preset, const SEcho &Echo, float Age) const
{
	switch(Preset)
	{
	case PRESET_SANDEVISTAN:
	{
		// glide through the palette: neighbouring copies differ by a soft hue step
		const float t = Echo.m_Serial * 0.45f;
		const int n = (int)std::size(SANDEVISTAN_PALETTE);
		const int i = (int)t % n;
		const ColorRGBA a = SANDEVISTAN_PALETTE[i], b = SANDEVISTAN_PALETTE[(i + 1) % n];
		const float f = t - std::floor(t);
		return ColorRGBA(mix(a.r, b.r, f), mix(a.g, b.g, f), mix(a.b, b.b, f), 1.0f);
	}
	case PRESET_FOG:
		return ColorRGBA(0.80f, 0.83f, 0.88f, 1.0f);
	case PRESET_GLITCH:
		return ColorRGBA(0.85f, 0.90f, 1.0f, 1.0f);
	case PRESET_PHANTOM:
		return ColorRGBA(0.95f, 0.96f, 1.0f, 1.0f);
	default:
	{
		const ColorRGBA a = ConfigColor(g_Config.m_FogTrailColor1), b = ConfigColor(g_Config.m_FogTrailColor2);
		return ColorRGBA(mix(a.r, b.r, Age), mix(a.g, b.g, Age), mix(a.b, b.b, Age), 1.0f);
	}
	}
}

void CFogTrails::RenderEcho(int Preset, const SEcho &Echo, float Age, float Opacity, float Glow)
{
	// Age: 0 fresh .. 1 gone
	const ColorRGBA Color = EchoColor(Preset, Echo, Age);
	const float Fade = 1.0f - Age;
	const float Alpha = Opacity * Fade;

	if(!Echo.m_Shape.IsValid())
	{
		// no masks (0.7 skins): a tinted copy of the tee instead of a flat silhouette
		CTeeRenderInfo Info = Echo.m_RenderInfo;
		if(Info.m_ColorableRenderSkin.m_Body.IsValid())
			Info.m_CustomColoredSkin = true;
		Info.m_ColorBody = Info.m_ColorFeet = Color;
		RenderTools()->RenderTee(&Echo.m_State, &Info, Echo.m_Emote, Echo.m_Direction, Echo.m_Position, Alpha * 0.4f);
		return;
	}

	switch(Preset)
	{
	case PRESET_SANDEVISTAN:
		// translucent colored silhouette, a wide soft bloom and a light core
		Graphics()->BlendAdditive();
		DrawShape(Echo, Echo.m_Glow, vec2(0, 0), 1.0f, Color.WithAlpha(Glow * 0.55f * Fade * Fade));
		Graphics()->BlendNormal();
		DrawShape(Echo, Echo.m_Shape, vec2(0, 0), 1.0f, Color.WithAlpha(Alpha * 0.42f));
		Graphics()->BlendAdditive();
		DrawShape(Echo, Echo.m_Shape, vec2(0, 0), 1.0f, Color.WithAlpha(Alpha * 0.22f));
		Graphics()->BlendNormal();
		break;
	case PRESET_FOG:
	{
		// breath in cold air: copies swell, drift up and soften into mist
		const vec2 Drift(0.0f, -Age * 10.0f);
		const float Swell = 1.0f + 0.3f * Age;
		DrawShape(Echo, Echo.m_Glow, Drift, Swell * 1.05f, Color.WithAlpha(Alpha * 0.35f * Fade));
		DrawShape(Echo, Echo.m_Shape, Drift, Swell, Color.WithAlpha(Alpha * 0.22f * (1.0f - Age * 0.7f)));
		if(Glow > 0.0f)
		{
			Graphics()->BlendAdditive();
			DrawShape(Echo, Echo.m_Glow, Drift, Swell * 1.2f, Color.WithAlpha(Glow * 0.15f * Fade));
			Graphics()->BlendNormal();
		}
		break;
	}
	case PRESET_GLITCH:
	{
		// RGB split that snaps around in steps, like a corrupted frame
		const float Step = std::floor(Age * 6.0f);
		const vec2 Jitter((Hash01(Echo.m_Serial * 7 + (int)Step) - 0.5f) * 10.0f, (Hash01(Echo.m_Serial * 13 + (int)Step) - 0.5f) * 4.0f);
		const float Split = 3.0f + 5.0f * Age;
		Graphics()->BlendAdditive();
		DrawShape(Echo, Echo.m_Shape, Jitter + vec2(-Split, 0), 1.0f, ColorRGBA(1.0f, 0.15f, 0.25f, Alpha * 0.5f));
		DrawShape(Echo, Echo.m_Shape, Jitter + vec2(Split, 0), 1.0f, ColorRGBA(0.1f, 0.9f, 1.0f, Alpha * 0.5f));
		if(Glow > 0.0f)
			DrawShape(Echo, Echo.m_Glow, Jitter, 1.0f, Color.WithAlpha(Glow * 0.2f * Fade));
		Graphics()->BlendNormal();
		if(Hash01(Echo.m_Serial * 31 + (int)Step) > 0.35f)
			DrawShape(Echo, Echo.m_Shape, Jitter, 1.0f, Color.WithAlpha(Alpha * 0.3f));
		break;
	}
	case PRESET_PHANTOM:
		DrawShape(Echo, Echo.m_Shape, vec2(0, 0), 1.0f, Color.WithAlpha(Alpha * 0.32f));
		if(Glow > 0.0f)
		{
			Graphics()->BlendAdditive();
			DrawShape(Echo, Echo.m_Glow, vec2(0, 0), 1.0f, Color.WithAlpha(Glow * 0.22f * Fade));
			Graphics()->BlendNormal();
		}
		break;
	default:
		if(Glow > 0.0f)
		{
			Graphics()->BlendAdditive();
			DrawShape(Echo, Echo.m_Glow, vec2(0, 0), 1.0f, Color.WithAlpha(Glow * 0.4f * Fade));
			Graphics()->BlendNormal();
		}
		DrawShape(Echo, Echo.m_Shape, vec2(0, 0), 1.0f, Color.WithAlpha(Alpha * 0.45f));
		break;
	}
}

void CFogTrails::OnRender()
{
	if(!g_Config.m_FogTrail || (Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK))
		return;

	const int64_t NowNs = time_get_nanoseconds().count();
	const float Lifetime = std::max(1, g_Config.m_FogTrailLifetime) / 1000.0f;
	const float Opacity = g_Config.m_FogTrailOpacity / 100.0f;
	const float Glow = g_Config.m_FogTrailGlow / 100.0f;
	const int Preset = std::clamp(g_Config.m_FogTrailPreset, 0, NUM_PRESETS - 1);

	for(STrack &Track : m_aTracks)
	{
		while(!Track.m_Echoes.empty() && (NowNs - Track.m_Echoes.front().m_SpawnNs) / 1e9f >= Lifetime)
		{
			ReleaseEcho(Track.m_Echoes.front());
			Track.m_Echoes.pop_front();
		}
		// oldest first, so fresher copies sit on top
		for(const SEcho &Echo : Track.m_Echoes)
		{
			const float Age = std::clamp((NowNs - Echo.m_SpawnNs) / 1e9f / Lifetime, 0.0f, 1.0f);
			RenderEcho(Preset, Echo, Age, Opacity, Glow);
		}
	}
	Graphics()->BlendNormal();
}
