#ifndef GAME_CLIENT_COMPONENTS_FOG_TRAILS_H
#define GAME_CLIENT_COMPONENTS_FOG_TRAILS_H

#include <base/color.h>
#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/client/animstate.h>
#include <game/client/component.h>
#include <game/client/render.h>

#include <cstdint>
#include <deque>

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
	void ReleaseEcho(SEcho &Echo);
	void DrawShape(const SEcho &Echo, IGraphics::CTextureHandle Texture, vec2 Offset, float Scale, ColorRGBA Color);
	ColorRGBA EchoColor(int Preset, const SEcho &Echo, float Age) const;
	void RenderEcho(int Preset, const SEcho &Echo, float Age, float Opacity, float Glow);

	STrack m_aTracks[MAX_CLIENTS];
};

#endif
