#ifndef GAME_CLIENT_COMPONENTS_FOG_TRAILS_H
#define GAME_CLIENT_COMPONENTS_FOG_TRAILS_H

#include <base/color.h>
#include <base/vmath.h>

#include <engine/shared/jobs.h>
#include <engine/shared/protocol.h>

#include <game/client/animstate.h>
#include <game/client/component.h>
#include <game/client/render.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

// Builds the flat silhouette and bloom of one afterimage on a worker thread.
class CFogTrailShapeJob : public IJob
{
public:
	static constexpr float PIXELS_PER_UNIT = 1.25f;

	// the pose, read on the main thread
	std::shared_ptr<const CSkin::SSilhouetteMasks> m_pMasks;
	float m_AnimScale = 1.0f, m_BaseSize = 64.0f, m_BodySize = 64.0f;
	vec2 m_BodyPos = vec2(0, 0);
	float m_BodyAngle = 0.0f;
	bool m_FlipFeet = false;
	vec2 m_aFootPos[2] = {vec2(0, 0), vec2(0, 0)};
	float m_aFootAngle[2] = {0.0f, 0.0f};
	vec2 m_Center = vec2(0, 0);
	float m_Half = 0.0f;
	int m_N = 16;

	// RGBA pixels of the shape and the bloom, ready to upload
	std::vector<uint8_t> m_vShape;
	std::vector<uint8_t> m_vGlow;

protected:
	void Run() override;
};

// Cosmetic afterimage trails behind fast tees (Sandevistan and friends). The players component
// reports every rendered pose through Record(); the trail renders the frozen copies behind tees.
class CFogTrails : public CComponent
{
public:
	enum EPreset
	{
		PRESET_SANDEVISTAN = 0,
		PRESET_FOG,
		PRESET_GLITCH,
		PRESET_PHANTOM,
		PRESET_CUSTOM,
		NUM_PRESETS,
	};
	// Recommended spawn interval and lifetime (ms) for each preset, applied when one is picked.
	static void PresetTiming(int Preset, int &IntervalMs, int &LifetimeMs, int &Spacing);

	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnReset() override;
	void OnShutdown() override;

	void Record(int ClientId, const CAnimState &State, const CTeeRenderInfo &RenderInfo, int Emote, vec2 Direction, vec2 Position);

private:
	struct SEcho
	{
		CAnimState m_State;
		CTeeRenderInfo m_RenderInfo;
		int m_Emote;
		vec2 m_Direction;
		vec2 m_Position;
		int64_t m_SpawnNs;
		int m_Serial; // spawn counter, drives color cycling and glitch jitter

		// composited flat silhouette of this pose (one alpha, no overlapping parts) and its bloom
		IGraphics::CTextureHandle m_Shape;
		IGraphics::CTextureHandle m_Glow;
		vec2 m_ShapeCenter = vec2(0, 0);
		float m_ShapeSize = 0.0f; // world size of the square texture
		std::shared_ptr<CFogTrailShapeJob> m_pShapeJob; // set while the worker builds the textures
	};
	struct STrack
	{
		std::deque<SEcho> m_Echoes;
		vec2 m_LastPos = vec2(0, 0);
		int64_t m_LastSampleNs = 0;
		int64_t m_LastSpawnNs = 0;
		vec2 m_LastSpawnPos = vec2(0, 0);
		float m_Speed = 0.0f; // smoothed, world units per second
		int m_Serial = 0;
		bool m_Valid = false;
	};

	bool BuildSilhouette(SEcho &Echo);
	bool PollShape(SEcho &Echo);
	void ReleaseEcho(SEcho &Echo);
	void DrawShape(const SEcho &Echo, IGraphics::CTextureHandle Texture, vec2 Offset, float Scale, ColorRGBA Color);
	ColorRGBA EchoColor(int Preset, const SEcho &Echo, float Age) const;
	void RenderEcho(int Preset, const SEcho &Echo, float Age, float Opacity, float Glow);

	STrack m_aTracks[MAX_CLIENTS];
};

#endif
