#ifndef GAME_CLIENT_COMPONENTS_FOG_TEE_STYLE_H
#define GAME_CLIENT_COMPONENTS_FOG_TEE_STYLE_H

#include <base/color.h>

#include <engine/image.h>

#include <game/client/component.h>
#include <game/client/skin.h>

#include <map>
#include <string>
#include <vector>

class CTeeRenderInfo;
struct SFogLook;

// Fog tee studio: animated colours and patterns for your own tee (and your dummy).
// The skin PNG is kept on the CPU; about 60 times a second the body, feet and hands are
// re-coloured with the studio look and uploaded as the tee's original-colour textures, so
// every place that draws the tee (game, menus, studio preview) gets real masked patterns.
// Only this client sees it; the server still gets your normal skin and colours.
class CFogTeeStyle : public CComponent
{
public:
	enum
	{
		SLOT_PLAYER = 0,
		SLOT_DUMMY,
		NUM_SLOTS,
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnShutdown() override;

	// Whether the studio look is drawn for this slot right now.
	bool Enabled(int Slot) const;
	// Replaces the parts of Info with the studio look. Info must already hold the tee's skin
	// and game colours (mode "skin colours" keeps them). Returns false when nothing changed.
	bool Apply(CTeeRenderInfo &Info, int Slot, const char *pSkinName);
	// The same, but always on, for previews of settings that are not enabled yet.
	bool ApplyPreview(CTeeRenderInfo &Info, int Slot, const char *pSkinName);

	// forget loaded skins (after a skin file was rewritten)
	void Invalidate();
	// another Fog player's look (from the presence server)
	bool ApplyRemote(CTeeRenderInfo &Info, int ClientId, const SFogLook &Look, const char *pSkinName);
	// pattern images in fog/patterns/
	void ScanPatterns();
	const std::vector<std::string> &PatternFiles() const { return m_vPatternFiles; }

	// game colour helpers for the dummy sync
	static unsigned InvertPackedHsl(unsigned Packed);

private:
	struct SSlot
	{
		std::string m_Skin;
		bool m_Failed = false;
		// two sizes: [0] for the game (about 2x), [1] sharp for the big menu tees (about 4x)
		CImageInfo m_aBase[2]; // upscaled original skin
		CImageInfo m_aGray[2]; // the same, made colourable (grayscale with the body weighting)
		CImageInfo m_aWork[2]; // re-coloured copy that gets uploaded
		int m_LastLevel = -1;
		CSkin::CSkinTextures m_Textures; // outlines and eyes stay, body/feet/hands are baked
		float m_LastBake = -1.0f;
		float m_LastUsed = 0.0f;
	};

	bool LoadSlot(SSlot &Slot, const char *pSkinName);
	void UnloadSlot(SSlot &Slot);
	void Bake(SSlot &Slot, const CTeeRenderInfo &Game, const SFogLook &Look, float Time, int L);
	bool BakeInto(SSlot &Slot, CTeeRenderInfo &Info, const SFogLook &Look, const char *pSkinName, float Rate, bool AllowSharp = true);
	void LoadPattern();
	void SyncDummy();

	SSlot m_aSlots[NUM_SLOTS];
	std::map<int, SSlot> m_Remote;
	CImageInfo m_Pattern;
	std::string m_PatternName;
	std::vector<std::string> m_vPatternFiles;
	bool m_PatternsScanned = false;
	bool m_DummyInfoPending = false;
	float m_NextDummyInfo = 0.0f;
};

#endif
