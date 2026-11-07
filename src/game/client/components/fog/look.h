#ifndef GAME_CLIENT_COMPONENTS_FOG_LOOK_H
#define GAME_CLIENT_COMPONENTS_FOG_LOOK_H

#include <string>

// Everything the tee studio adds to a tee, in one place: animated colours and pattern, aura,
// outline and accessories. Your own look comes from the config; other Fog players' looks
// arrive through the presence server as a short text (Serialize / Parse).
struct SFogLook
{
	int m_Style = 0;
	int m_ColorMode = 1;
	unsigned m_Color1 = 0, m_Color2 = 0;
	int m_ColorSpeed = 100;
	int m_FeetMode = 0;
	int m_Pattern = 0;
	unsigned m_PatternColor = 0;
	int m_PatternOpacity = 60, m_PatternScale = 100, m_PatternAngle = 30, m_PatternSpeed = 40, m_PatternSoft = 30;

	int m_Aura = 0;
	unsigned m_AuraColor = 0;
	int m_AuraStrength = 100, m_AuraSize = 100, m_AuraSpeed = 100, m_AuraRainbow = 0;
	int m_Outline = 0;
	unsigned m_OutlineColor = 0;
	int m_OutlineWidth = 3, m_OutlineMode = 0;

	std::string m_Accessories; // the fog_tee_accessories format
	bool m_Invert = false; // a synced dummy with inverted colours

	// Dummy = your dummy (follows fog_dummy_sync)
	static SFogLook FromConfig(bool Dummy);
	bool Empty() const { return !m_Style && !m_Aura && !m_Outline && m_Accessories.empty(); }
	std::string Serialize() const;
	bool Parse(const char *pText);
	bool operator==(const SFogLook &Other) const;
};

#endif
