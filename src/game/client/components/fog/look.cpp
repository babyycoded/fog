#include "look.h"

#include <base/str.h>

#include <engine/shared/config.h>

#include <algorithm>
#include <cstdio>
#include <vector>

SFogLook SFogLook::FromConfig(bool Dummy)
{
	SFogLook L;
	const bool Synced = !Dummy || g_Config.m_FogDummySync != 0;
	L.m_Style = Synced ? g_Config.m_FogTeeStyle : 0;
	L.m_ColorMode = g_Config.m_FogTeeColorMode;
	L.m_Color1 = g_Config.m_FogTeeColor1;
	L.m_Color2 = g_Config.m_FogTeeColor2;
	L.m_ColorSpeed = g_Config.m_FogTeeColorSpeed;
	L.m_FeetMode = g_Config.m_FogTeeFeetMode;
	L.m_Pattern = g_Config.m_FogTeePattern;
	L.m_PatternColor = g_Config.m_FogTeePatternColor;
	L.m_PatternOpacity = g_Config.m_FogTeePatternOpacity;
	L.m_PatternScale = g_Config.m_FogTeePatternScale;
	L.m_PatternAngle = g_Config.m_FogTeePatternAngle;
	L.m_PatternSpeed = g_Config.m_FogTeePatternSpeed;
	L.m_PatternSoft = g_Config.m_FogTeePatternSoft;
	L.m_Aura = Synced ? g_Config.m_FogTeeAura : 0;
	L.m_AuraColor = g_Config.m_FogTeeAuraColor;
	L.m_AuraStrength = g_Config.m_FogTeeAuraStrength;
	L.m_AuraSize = g_Config.m_FogTeeAuraSize;
	L.m_AuraSpeed = g_Config.m_FogTeeAuraSpeed;
	L.m_AuraRainbow = g_Config.m_FogTeeAuraRainbow;
	L.m_Outline = Synced ? g_Config.m_FogTeeOutline : 0;
	L.m_OutlineColor = g_Config.m_FogTeeOutlineColor;
	L.m_OutlineWidth = g_Config.m_FogTeeOutlineWidth;
	L.m_OutlineMode = g_Config.m_FogTeeOutlineMode;
	L.m_Accessories = Dummy && !g_Config.m_FogDummySync ? g_Config.m_FogDummyAccessories : g_Config.m_FogTeeAccessories;
	if(!g_Config.m_FogTeeAccessoriesOn)
	{
		L.m_Accessories.clear();
		L.m_Aura = 0;
		L.m_Outline = 0;
	}
	L.m_Invert = Dummy && g_Config.m_FogDummySync == 2;
	return L;
}

// 1|style...|aura...|accessories   (numbers only, plus the accessory ids)
std::string SFogLook::Serialize() const
{
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "1|%d,%d,%u,%u,%d,%d,%d,%u,%d,%d,%d,%d,%d|%d,%u,%d,%d,%d,%d,%d,%u,%d,%d|",
		m_Style, m_ColorMode, m_Color1, m_Color2, m_ColorSpeed, m_FeetMode, m_Pattern == 9 ? 0 : m_Pattern, m_PatternColor,
		m_PatternOpacity, m_PatternScale, m_PatternAngle, m_PatternSpeed, m_PatternSoft,
		m_Aura, m_AuraColor, m_AuraStrength, m_AuraSize, m_AuraSpeed, m_AuraRainbow, m_Outline, m_OutlineColor, m_OutlineWidth, m_OutlineMode);
	return std::string(aBuf) + m_Accessories;
}

bool SFogLook::Parse(const char *pText)
{
	*this = SFogLook();
	if(!pText || str_comp_num(pText, "1|", 2) != 0)
		return false;
	const char *pStyle = pText + 2;
	const char *pAura = str_find(pStyle, "|");
	if(!pAura)
		return false;
	pAura++;
	const char *pAcc = str_find(pAura, "|");
	if(!pAcc)
		return false;
	pAcc++;
	if(std::sscanf(pStyle, "%d,%d,%u,%u,%d,%d,%d,%u,%d,%d,%d,%d,%d", &m_Style, &m_ColorMode, &m_Color1, &m_Color2, &m_ColorSpeed, &m_FeetMode, &m_Pattern, &m_PatternColor,
		   &m_PatternOpacity, &m_PatternScale, &m_PatternAngle, &m_PatternSpeed, &m_PatternSoft) != 13)
		return false;
	if(std::sscanf(pAura, "%d,%u,%d,%d,%d,%d,%d,%u,%d,%d", &m_Aura, &m_AuraColor, &m_AuraStrength, &m_AuraSize, &m_AuraSpeed, &m_AuraRainbow, &m_Outline, &m_OutlineColor, &m_OutlineWidth, &m_OutlineMode) != 10)
		return false;
	// keep everything inside the ranges the client itself allows
	m_Style = std::clamp(m_Style, 0, 1);
	m_ColorMode = std::clamp(m_ColorMode, 0, 5);
	m_ColorSpeed = std::clamp(m_ColorSpeed, 0, 500);
	m_FeetMode = std::clamp(m_FeetMode, 0, 3);
	m_Pattern = std::clamp(m_Pattern, 0, 8);
	m_PatternOpacity = std::clamp(m_PatternOpacity, 0, 100);
	m_PatternScale = std::clamp(m_PatternScale, 20, 400);
	m_PatternAngle = std::clamp(m_PatternAngle, 0, 359);
	m_PatternSpeed = std::clamp(m_PatternSpeed, -300, 300);
	m_PatternSoft = std::clamp(m_PatternSoft, 0, 100);
	m_Aura = std::clamp(m_Aura, 0, 6);
	m_AuraStrength = std::clamp(m_AuraStrength, 0, 200);
	m_AuraSize = std::clamp(m_AuraSize, 50, 200);
	m_AuraSpeed = std::clamp(m_AuraSpeed, 0, 300);
	m_AuraRainbow = std::clamp(m_AuraRainbow, 0, 1);
	m_Outline = std::clamp(m_Outline, 0, 1);
	m_OutlineWidth = std::clamp(m_OutlineWidth, 1, 10);
	m_OutlineMode = std::clamp(m_OutlineMode, 0, 3);
	m_Accessories = std::string(pAcc).substr(0, 511);
	return true;
}

bool SFogLook::operator==(const SFogLook &o) const
{
	return Serialize() == o.Serialize() && m_Invert == o.m_Invert;
}
