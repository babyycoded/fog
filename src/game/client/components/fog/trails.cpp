#include "trails.h"

#include <base/math.h>
#include <base/time.h>

#include <engine/engine.h>
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
	Echo.m_pShapeJob = nullptr; // a running job keeps its own data alive and is simply dropped
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

	// only the area around the shape can become nonzero: zero samples add nothing to a sum,
	// so skipping them gives exactly the same pixels as convolving the whole canvas
	int MinX = N, MinY = N, MaxX = -1, MaxY = -1;
	for(int y = 0; y < N; y++)
		for(int x = 0; x < N; x++)
			if(vData[(size_t)y * N + x] != 0.0f)
			{
				MinX = std::min(MinX, x);
				MaxX = std::max(MaxX, x);
				MinY = std::min(MinY, y);
				MaxY = std::max(MaxY, y);
			}
	if(MaxX < 0)
		return;
	const int OutX0 = std::max(MinX - Radius, 0), OutX1 = std::min(MaxX + Radius, N - 1);
	const int OutY0 = std::max(MinY - Radius, 0), OutY1 = std::min(MaxY + Radius, N - 1);

	// horizontal pass: rows of the shape, spread sideways
	std::vector<float> vTmp(vData.size(), 0.0f);
	for(int y = MinY; y <= MaxY; y++)
	{
		const float *pIn = &vData[(size_t)y * N];
		float *pOut = &vTmp[(size_t)y * N];
		for(int x = OutX0; x <= OutX1; x++)
		{
			const int k0 = std::max(-Radius, MinX - x), k1 = std::min(Radius, MaxX - x);
			float Acc = 0.0f;
			for(int k = k0; k <= k1; k++)
				Acc += pIn[x + k] * vKernel[k + Radius];
			pOut[x] = Acc;
		}
	}
	// vertical pass: spread those rows up and down
	std::fill(vData.begin(), vData.end(), 0.0f);
	for(int y = OutY0; y <= OutY1; y++)
	{
		const int k0 = std::max(-Radius, MinY - y), k1 = std::min(Radius, MaxY - y);
		float *pOut = &vData[(size_t)y * N];
		for(int x = OutX0; x <= OutX1; x++)
		{
			float Acc = 0.0f;
			for(int k = k0; k <= k1; k++)
				Acc += vTmp[(size_t)(y + k) * N + x] * vKernel[k + Radius];
			pOut[x] = Acc;
		}
	}
}

std::vector<uint8_t> AlphaPixels(const std::vector<float> &vAlpha, int N, float Scale)
{
	std::vector<uint8_t> vPixels((size_t)N * N * 4);
	for(size_t i = 0; i < (size_t)N * N; i++)
	{
		vPixels[i * 4] = vPixels[i * 4 + 1] = vPixels[i * 4 + 2] = 255;
		vPixels[i * 4 + 3] = (uint8_t)std::lround(std::clamp(vAlpha[i] * Scale, 0.0f, 1.0f) * 255.0f);
	}
	return vPixels;
}

IGraphics::CTextureHandle UploadPixels(IGraphics *pGraphics, std::vector<uint8_t> &vPixels, int N)
{
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
// The pose is read here; the pixels are computed on a worker thread (see CFogTrailShapeJob)
// and uploaded by OnRender once ready, so spawning a copy never stalls a frame.
bool CFogTrails::BuildSilhouette(SEcho &Echo)
{
	const std::shared_ptr<const CSkin::SSilhouetteMasks> &pMasks = Echo.m_RenderInfo.m_pSilhouette;
	if(!pMasks || pMasks->m_vBody.empty() || pMasks->m_vFoot.empty())
		return false;

	auto pJob = std::make_shared<CFogTrailShapeJob>();
	pJob->m_pMasks = pMasks;
	CRenderTools::GetRenderTeeAnimScaleAndBaseSize(&Echo.m_RenderInfo, pJob->m_AnimScale, pJob->m_BaseSize);
	// same as CRenderTools::GetRenderTeeBodyScale (private there)
	pJob->m_BodySize = g_Config.m_ClFatSkins ? pJob->m_BaseSize * 1.3f : pJob->m_BaseSize;

	const CAnimKeyframe *pBody = Echo.m_State.GetBody();
	pJob->m_BodyPos = Echo.m_Position + vec2(pBody->m_X, pBody->m_Y) * pJob->m_AnimScale;
	pJob->m_BodyAngle = pBody->m_Angle * pi * 2.0f;
	pJob->m_FlipFeet = Echo.m_Direction.x < 0 && Echo.m_RenderInfo.m_FeetFlipped;
	const CAnimKeyframe *apFeet[2] = {Echo.m_State.GetBackFoot(), Echo.m_State.GetFrontFoot()};
	for(int i = 0; i < 2; i++)
	{
		pJob->m_aFootPos[i] = Echo.m_Position + vec2(apFeet[i]->m_X, apFeet[i]->m_Y) * pJob->m_AnimScale;
		pJob->m_aFootAngle[i] = apFeet[i]->m_Angle * pi * 2.0f;
	}

	// square canvas around the tee, with room for the bloom
	pJob->m_Half = pJob->m_BaseSize * 0.85f + pJob->m_BaseSize * 0.6f;
	pJob->m_N = std::clamp((int)std::ceil(pJob->m_Half * 2.0f * CFogTrailShapeJob::PIXELS_PER_UNIT), 16, 512);
	pJob->m_Center = Echo.m_Position + vec2(0.0f, -pJob->m_BaseSize * 0.12f);

	Echo.m_ShapeCenter = pJob->m_Center;
	Echo.m_ShapeSize = pJob->m_Half * 2.0f;
	Echo.m_pShapeJob = pJob;
	Engine()->AddJob(pJob);
	return true;
}

void CFogTrailShapeJob::Run()
{
	const CSkin::SSilhouetteMasks *pMasks = m_pMasks.get();
	const int N = m_N;
	const float Half = m_Half;
	const float AnimScale = m_AnimScale;

	// the masks are zero outside their rotated quads, so only pixels inside the circles around
	// the body and the feet can be nonzero: sample just that box, the rest stays exactly zero
	float BoxX0 = m_BodyPos.x - m_BodySize * 0.7072f, BoxX1 = m_BodyPos.x + m_BodySize * 0.7072f;
	float BoxY0 = m_BodyPos.y - m_BodySize * 0.7072f, BoxY1 = m_BodyPos.y + m_BodySize * 0.7072f;
	const float FootRadius = AnimScale * 35.78f; // half diagonal of the 64x32 foot quad
	for(const vec2 &FootPos : m_aFootPos)
	{
		BoxX0 = std::min(BoxX0, FootPos.x - FootRadius);
		BoxX1 = std::max(BoxX1, FootPos.x + FootRadius);
		BoxY0 = std::min(BoxY0, FootPos.y - FootRadius);
		BoxY1 = std::max(BoxY1, FootPos.y + FootRadius);
	}
	const auto ToPixel = [&](float World, float CenterAxis) { return ((World - CenterAxis) / Half + 1.0f) * 0.5f * N - 0.5f; };
	const int Px0 = std::clamp((int)std::floor(ToPixel(BoxX0, m_Center.x)) - 1, 0, N), Px1 = std::clamp((int)std::ceil(ToPixel(BoxX1, m_Center.x)) + 1, -1, N - 1);
	const int Py0 = std::clamp((int)std::floor(ToPixel(BoxY0, m_Center.y)) - 1, 0, N), Py1 = std::clamp((int)std::ceil(ToPixel(BoxY1, m_Center.y)) + 1, -1, N - 1);

	std::vector<float> vAlpha((size_t)N * N, 0.0f);
	for(int py = Py0; py <= Py1; py++)
		for(int px = Px0; px <= Px1; px++)
		{
			const vec2 World = m_Center + vec2(((px + 0.5f) / N * 2.0f - 1.0f) * Half, ((py + 0.5f) / N * 2.0f - 1.0f) * Half);
			const vec2 LocalBody = RotateVec(World - m_BodyPos, -m_BodyAngle) / m_BodySize;
			float a = SampleMask(pMasks->m_vBody, pMasks->m_BodyW, pMasks->m_BodyH, LocalBody.x + 0.5f, LocalBody.y + 0.5f);
			for(int i = 0; i < 2; i++)
			{
				const vec2 Local = RotateVec(World - m_aFootPos[i], -m_aFootAngle[i]);
				float u = Local.x / (64.0f * AnimScale) + 0.5f;
				const float v = Local.y / (32.0f * AnimScale) + 0.5f;
				if(m_FlipFeet)
					u = 1.0f - u;
				a = std::max(a, SampleMask(pMasks->m_vFoot, pMasks->m_FootW, pMasks->m_FootH, u, v));
			}
			vAlpha[(size_t)py * N + px] = a;
		}

	m_vShape = AlphaPixels(vAlpha, N, 1.0f);
	Blur(vAlpha, N, m_BaseSize * 0.11f * PIXELS_PER_UNIT);
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
	m_vGlow = AlphaPixels(vAlpha, N, Peak > 0.0f ? 1.0f / Peak : 0.0f);
}

// a finished worker result becomes the two textures; true while the copy is still being built
bool CFogTrails::PollShape(SEcho &Echo)
{
	if(!Echo.m_pShapeJob)
		return false;
	if(Echo.m_pShapeJob->State() != IJob::STATE_DONE)
		return Echo.m_pShapeJob->State() != IJob::STATE_ABORTED;
	Echo.m_Shape = UploadPixels(Graphics(), Echo.m_pShapeJob->m_vShape, Echo.m_pShapeJob->m_N);
	Echo.m_Glow = UploadPixels(Graphics(), Echo.m_pShapeJob->m_vGlow, Echo.m_pShapeJob->m_N);
	Echo.m_pShapeJob = nullptr;
	return false;
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
		for(SEcho &Echo : Track.m_Echoes)
		{
			if(PollShape(Echo))
				continue;
			const float Age = std::clamp((NowNs - Echo.m_SpawnNs) / 1e9f / Lifetime, 0.0f, 1.0f);
			RenderEcho(Preset, Echo, Age, Opacity, Glow);
		}
	}
	Graphics()->BlendNormal();
}
