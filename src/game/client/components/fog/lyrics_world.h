#ifndef GAME_CLIENT_COMPONENTS_FOG_LYRICS_WORLD_H
#define GAME_CLIENT_COMPONENTS_FOG_LYRICS_WORLD_H

#include "glow_atlas.h"
#include "lyrics.h"

#include <base/vmath.h>

#include <engine/textrender.h>

#include <game/client/component.h>

#include <vector>

// Synced lyrics rendered in the game world next to the player: bent along an arc with fake
// perspective, extruded, glowing as the letters are sung, and dissolving into fog.
class CFogLyricsWorld : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnRender() override; // in-map pass (fog_lyrics_layer 0)
	void OnReset() override;
	void OnWindowResize() override;
	void OnShutdown() override;

	void RenderFrontPass(); // above-everything pass (fog_lyrics_layer 1)

private:
	struct SWordCache
	{
		STextContainerIndex m_Container;
		std::vector<STextGlyph> m_vGlyphs;
		float m_Width = 0.0f;
	};

	struct SLineCache
	{
		int m_Key = -100; // lyric line index, or TITLE_KEY for the now playing title card
		float m_FontSize = 0.0f;
		float m_ScreenWidth = 0.0f; // world-to-screen scale the glyphs were rasterized at
		float m_SpaceWidth = 0.0f;
		float m_Width = 0.0f;
		float m_CenterY = 0.0f; // vertical middle of the inked glyphs, font independent
		std::vector<SWordCache> m_vWords;
		CFogGlowAtlas m_Glow; // blurred glyphs, in the same order as the glyphs of m_vWords
		int m_LastUsedFrame = 0;
	};

	static constexpr int TITLE_KEY = -2;
	static constexpr int MAX_CACHED_LINES = 12;
	static constexpr int MAX_TRAIL_LINES = 8;
	// glyphs are rasterized this much larger than drawn, so pops and zoom stay sharp
	static constexpr float OVERSAMPLE = 1.3f;

	void Render(bool FrontPass);
	void RenderTrail(int Current, int64_t TimeMs, int64_t NowNs, float FontSize);
	void UpdateAnchor(float Dt);
	SLineCache *GetLine(int Key, const SLyricLine &Line, float FontSize);
	void ClearCache();
	void ReleaseLine(SLineCache &Line);
	// Slot: 0 = being sung, 1 = upcoming (in the fog below), -1 = gone (dissolved upward)
	void RenderLine(const SLineCache &Cache, const SLyricLine &Line, float Slot, float Fade, int64_t TimeMs, vec2 Origin, float FontSize);
	void RenderVeil(vec2 Center, float Width, float Height, float Alpha);

	float AnimSpeed() const;
	float AnimIntensity() const;

	std::vector<SLineCache> m_vLineCache;
	int m_Frame = 0;
	int m_CachedRevision = -1;
	int m_CachedFontKey = -1; // font and weight the cached glyphs were made with

	vec2 m_Anchor = vec2(0.0f, 0.0f);
	bool m_AnchorValid = false;

	int m_CurrentLine = -1;
	int64_t m_LineChangeTimeNs = 0;
	int64_t m_TrackChangeTimeNs = 0;
	float m_Presence = 0.0f; // fades the whole effect in and out

	std::vector<STextGlyphDraw> m_vDraws;
	IGraphics::CTextureHandle m_VeilTexture; // soft radial falloff for the fog patch
	SLyricLine m_TitleLine;

	// trail mode: lines stamped into the map where they started
	struct STrailLine
	{
		int m_Line;
		vec2 m_Pos;
		int64_t m_StampNs;
	};
	std::vector<STrailLine> m_vTrail;
};

class CFogLyricsFront : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
};

#endif
