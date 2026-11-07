// Fog client: the "Live shaders" settings tab. Left: the pass itself, the toggle key,
// motion blur and the presets; right: every effect as a row you can switch, tune and reset.

#include "glass.h"

#include <base/color.h>
#include <base/math.h>
#include <base/str.h>

#include <engine/font_icons.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/binds.h>
#include <game/client/components/key_binder.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr ColorRGBA FROST(0.91f, 0.93f, 0.95f, 1.0f);
constexpr ColorRGBA MIST(0.64f, 0.68f, 0.74f, 1.0f);
constexpr ColorRGBA DIM(0.46f, 0.50f, 0.56f, 1.0f);

struct SFxValues
{
	int m_Bloom, m_BloomThreshold, m_BloomRadius;
	int m_Exposure, m_Contrast, m_Saturation, m_Gamma;
	int m_Temperature, m_Tint, m_Vibrance, m_Fade;
	int m_Vignette, m_Chroma, m_Grain, m_Sharpen;
	int m_Scanlines, m_Haze, m_Posterize, m_Pixelate;
	float m_FilterHue, m_FilterSat, m_FilterLight;
	int m_FilterAmount;
	int m_MotionBlur, m_MotionBlurStrength;
};

// neutral = a plain copy of the frame
constexpr SFxValues NEUTRAL = {0, 70, 18, 100, 100, 100, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.58f, 0.3f, 0.6f, 0, 0, 100};

struct SFxPreset
{
	const char *m_pName;
	const char *m_pHint;
	SFxValues m_Values;
};

SFxValues With(std::initializer_list<std::pair<int SFxValues::*, int>> Changes)
{
	SFxValues Values = NEUTRAL;
	for(const auto &[pMember, Value] : Changes)
		Values.*pMember = Value;
	return Values;
}

SFxValues WithFilter(SFxValues Values, float Hue, float Sat, float Light, int Amount)
{
	Values.m_FilterHue = Hue;
	Values.m_FilterSat = Sat;
	Values.m_FilterLight = Light;
	Values.m_FilterAmount = Amount;
	return Values;
}

const std::vector<SFxPreset> &Presets()
{
	using V = SFxValues;
	static const std::vector<SFxPreset> s_vPresets = {
		{"Clean", "No effects, the plain frame", NEUTRAL},
		{"Fog", "The client's own look: soft haze, quiet glow", With({{&V::m_Haze, 35}, {&V::m_Vignette, 35}, {&V::m_Saturation, 80}, {&V::m_Contrast, 108}, {&V::m_Bloom, 40}, {&V::m_BloomThreshold, 75}, {&V::m_Grain, 8}})},
		{"Melancholy", "Cold, faded, a little lonely", With({{&V::m_Saturation, 55}, {&V::m_Temperature, -25}, {&V::m_Fade, 20}, {&V::m_Haze, 30}, {&V::m_Vignette, 40}, {&V::m_Bloom, 30}})},
		{"Liquid glass", "Wet highlights and a clear image", With({{&V::m_Bloom, 70}, {&V::m_BloomThreshold, 65}, {&V::m_BloomRadius, 30}, {&V::m_Vibrance, 20}, {&V::m_Sharpen, 30}, {&V::m_Haze, 15}})},
		{"Competitive", "Crisp edges, readable freeze and hookables", With({{&V::m_Sharpen, 80}, {&V::m_Contrast, 112}, {&V::m_Vibrance, 30}, {&V::m_Exposure, 104}})},
		{"Streamer", "Clean and punchy for recordings", With({{&V::m_Sharpen, 60}, {&V::m_Vibrance, 25}, {&V::m_Contrast, 106}})},
		{"Vivid", "Strong colours everywhere", With({{&V::m_Saturation, 135}, {&V::m_Vibrance, 60}, {&V::m_Contrast, 110}})},
		{"High contrast", "Deep shadows, hard light", With({{&V::m_Contrast, 145}, {&V::m_Sharpen, 50}})},
		{"Soft glow", "Light bleeds gently around bright parts", With({{&V::m_Bloom, 90}, {&V::m_BloomThreshold, 60}, {&V::m_BloomRadius, 24}, {&V::m_Contrast, 95}})},
		{"Dreamy", "Big halos and lifted blacks", With({{&V::m_Bloom, 120}, {&V::m_BloomThreshold, 50}, {&V::m_BloomRadius, 40}, {&V::m_Fade, 25}, {&V::m_Saturation, 90}, {&V::m_Haze, 20}})},
		{"Neon nights", "Glowing colour on a dark screen", With({{&V::m_Bloom, 160}, {&V::m_BloomThreshold, 55}, {&V::m_BloomRadius, 20}, {&V::m_Saturation, 140}, {&V::m_Contrast, 118}, {&V::m_Vignette, 40}, {&V::m_Temperature, -20}})},
		{"Cinematic", "Film contrast, warm grade, dark corners", With({{&V::m_Contrast, 115}, {&V::m_Fade, 20}, {&V::m_Vignette, 55}, {&V::m_Grain, 15}, {&V::m_Temperature, 10}, {&V::m_Saturation, 90}})},
		{"Noir", "Black and white with grit", With({{&V::m_Saturation, 0}, {&V::m_Contrast, 135}, {&V::m_Vignette, 50}, {&V::m_Grain, 25}})},
		{"Sepia film", "An old print", WithFilter(With({{&V::m_Saturation, 20}, {&V::m_Grain, 30}, {&V::m_Vignette, 45}, {&V::m_Fade, 15}}), 0.08f, 0.55f, 0.6f, 60)},
		{"Warm sunset", "Golden light", With({{&V::m_Temperature, 60}, {&V::m_Tint, 10}, {&V::m_Bloom, 60}, {&V::m_Vibrance, 30}})},
		{"Arctic", "Cold and bright", With({{&V::m_Temperature, -60}, {&V::m_Saturation, 85}, {&V::m_Exposure, 108}, {&V::m_Haze, 20}})},
		{"Pastel", "Soft, light and gentle", With({{&V::m_Saturation, 70}, {&V::m_Fade, 35}, {&V::m_Exposure, 108}, {&V::m_Contrast, 90}})},
		{"Retro CRT", "Scanlines and a little colour bleed", With({{&V::m_Scanlines, 45}, {&V::m_Chroma, 25}, {&V::m_Vignette, 45}, {&V::m_Bloom, 50}, {&V::m_Saturation, 110}})},
		{"VHS", "A worn tape", With({{&V::m_Chroma, 50}, {&V::m_Grain, 40}, {&V::m_Fade, 30}, {&V::m_Saturation, 85}, {&V::m_Scanlines, 20}})},
		{"Glitch", "Split colour and noise", With({{&V::m_Chroma, 90}, {&V::m_Scanlines, 30}, {&V::m_Grain, 20}, {&V::m_Contrast, 120}})},
		{"Night vision", "Green, grainy, glowing", WithFilter(With({{&V::m_Saturation, 0}, {&V::m_Grain, 35}, {&V::m_Vignette, 60}, {&V::m_Bloom, 60}, {&V::m_Scanlines, 15}}), 0.33f, 0.7f, 0.5f, 70)},
		{"Pixel", "Big pixels, fewer colours", With({{&V::m_Pixelate, 4}, {&V::m_Posterize, 12}})},
		{"Handheld", "A four-shade pocket console", WithFilter(With({{&V::m_Pixelate, 3}, {&V::m_Posterize, 4}, {&V::m_Saturation, 0}}), 0.28f, 0.5f, 0.45f, 70)},
		{"Comic", "Flat colour, inked edges", With({{&V::m_Posterize, 6}, {&V::m_Sharpen, 120}, {&V::m_Saturation, 125}})},
		{"Smooth motion", "A light motion blur over everything", With({{&V::m_MotionBlur, 1}, {&V::m_MotionBlurStrength, 120}, {&V::m_Bloom, 30}})},
		{"Speed trails", "Long trails behind every movement", With({{&V::m_MotionBlur, 1}, {&V::m_MotionBlurStrength, 350}, {&V::m_Bloom, 60}, {&V::m_Chroma, 20}})},
	};
	return s_vPresets;
}

void ApplyFx(const SFxValues &Values)
{
	g_Config.m_FogFxBloom = Values.m_Bloom;
	g_Config.m_FogFxBloomThreshold = Values.m_BloomThreshold;
	g_Config.m_FogFxBloomRadius = Values.m_BloomRadius;
	g_Config.m_FogFxExposure = Values.m_Exposure;
	g_Config.m_FogFxContrast = Values.m_Contrast;
	g_Config.m_FogFxSaturation = Values.m_Saturation;
	g_Config.m_FogFxGamma = Values.m_Gamma;
	g_Config.m_FogFxTemperature = Values.m_Temperature;
	g_Config.m_FogFxTint = Values.m_Tint;
	g_Config.m_FogFxVibrance = Values.m_Vibrance;
	g_Config.m_FogFxFade = Values.m_Fade;
	g_Config.m_FogFxVignette = Values.m_Vignette;
	g_Config.m_FogFxChroma = Values.m_Chroma;
	g_Config.m_FogFxGrain = Values.m_Grain;
	g_Config.m_FogFxSharpen = Values.m_Sharpen;
	g_Config.m_FogFxScanlines = Values.m_Scanlines;
	g_Config.m_FogFxHaze = Values.m_Haze;
	g_Config.m_FogFxPosterize = Values.m_Posterize;
	g_Config.m_FogFxPixelate = Values.m_Pixelate;
	g_Config.m_FogFxFilter = ColorHSLA(Values.m_FilterHue, Values.m_FilterSat, Values.m_FilterLight).Pack(false);
	g_Config.m_FogFxFilterAmount = Values.m_FilterAmount;
	g_Config.m_FogMotionBlur = Values.m_MotionBlur;
	g_Config.m_FogMotionBlurStrength = Values.m_MotionBlurStrength;
}

} // namespace

// console: fog_fx_apply <index> (0 = Clean)
bool FogApplyFxPreset(int Index)
{
	if(Index < 0 || Index >= (int)Presets().size())
		return false;
	ApplyFx(Presets()[Index].m_Values);
	g_Config.m_FogFxPreset = Index;
	return true;
}

void CMenus::RenderSettingsShaders(CUIRect MainView)
{
	CUIRect Left, Right;
	MainView.VSplitMid(&Left, &Right, 20.0f);

	const auto Headline = [&](CUIRect &Column, const char *pText, const char *pBadge = nullptr) {
		CUIRect Line;
		Column.HSplitTop(26.0f, &Line, &Column);
		FogLabel(Ui(), TextRender(), Line, pText, 17.0f, TEXTALIGN_ML, FROST, "Manrope", 800);
		if(pBadge)
		{
			TextRender()->SetDisplayFont("Manrope", 800);
			TextRender()->SetFontPreset(EFontPreset::DISPLAY_FONT);
			const float TextW = TextRender()->TextWidth(17.0f, pText);
			const float BadgeW = TextRender()->TextWidth(9.0f, pBadge) + 12.0f;
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			CUIRect Badge = {Line.x + TextW + 10.0f, Line.y + 6.0f, BadgeW, 14.0f};
			Graphics()->TextureClear();
			Graphics()->DrawRect(Badge.x, Badge.y, Badge.w, Badge.h, ColorRGBA(0.62f, 0.72f, 0.80f, 0.22f), IGraphics::CORNER_ALL, 7.0f);
			FogLabel(Ui(), TextRender(), Badge, pBadge, 9.0f, TEXTALIGN_MC, ColorRGBA(0.80f, 0.86f, 0.92f, 1.0f), "Manrope", 800);
		}
		Column.HSplitTop(6.0f, nullptr, &Column);
	};

	// ---- left: the pass, the toggle key, motion blur ----
	Headline(Left, Localize("Live shaders"), "VULKAN");
	{
		CUIRect Row;
		Left.HSplitTop(20.0f, &Row, &Left);
		if(DoButton_CheckBox(&g_Config.m_FogFx, Localize("Build the effects pass on startup (restart required)"), g_Config.m_FogFx, &Row))
			g_Config.m_FogFx ^= 1;
		Left.HSplitTop(2.0f, nullptr, &Left);
		Left.HSplitTop(20.0f, &Row, &Left);
		if(DoButton_CheckBox(&g_Config.m_FogFxLive, Localize("Effects on"), g_Config.m_FogFxLive, &Row))
			g_Config.m_FogFxLive ^= 1;
		Left.HSplitTop(4.0f, nullptr, &Left);

		// a key that switches the effects on and off in game
		Left.HSplitTop(22.0f, &Row, &Left);
		CUIRect Label, Reader;
		Row.VSplitMid(&Label, &Reader, 10.0f);
		Ui()->DoLabel(&Label, Localize("Toggle key"), 13.0f, TEXTALIGN_ML);
		CBindSlot Current = EMPTY_BIND_SLOT;
		for(int Modifiers = 0; Modifiers < KeyModifier::COMBINATION_COUNT && Current == EMPTY_BIND_SLOT; Modifiers++)
			for(int Key = 0; Key < KEY_LAST; Key++)
			{
				const char *pBind = GameClient()->m_Binds.Get(Key, Modifiers);
				if(pBind && str_comp(pBind, "fog_fx_toggle") == 0)
				{
					Current = CBindSlot(Key, Modifiers);
					break;
				}
			}
		static CButtonContainer s_ReaderButton, s_ClearButton;
		const CKeyBinder::CKeyReaderResult Result = GameClient()->m_KeyBinder.DoKeyReader(&s_ReaderButton, &s_ClearButton, &Reader, Current, false);
		if(!Result.m_Aborted && Result.m_Bind != Current)
		{
			if(Current.m_Key != KEY_UNKNOWN)
				GameClient()->m_Binds.Bind(Current.m_Key, "", false, Current.m_ModifierMask);
			if(Result.m_Bind.m_Key != KEY_UNKNOWN)
				GameClient()->m_Binds.Bind(Result.m_Bind.m_Key, "fog_fx_toggle", false, Result.m_Bind.m_ModifierMask);
		}
		Left.HSplitTop(10.0f, nullptr, &Left);

		Left.HSplitTop(20.0f, &Row, &Left);
		if(DoButton_CheckBox(&g_Config.m_FogMotionBlur, Localize("Motion blur: the world, HUD, cursor and menus"), g_Config.m_FogMotionBlur, &Row))
			g_Config.m_FogMotionBlur ^= 1;
		Left.HSplitTop(2.0f, nullptr, &Left);
		Left.HSplitTop(20.0f, &Row, &Left);
		Ui()->DoScrollbarOption(&g_Config.m_FogMotionBlurStrength, &g_Config.m_FogMotionBlurStrength, &Row, Localize("Strength"), 0, 500, &CUi::ms_LinearScrollbarScale, 0, "%");
		Left.HSplitTop(14.0f, nullptr, &Left);
	}

	// ---- left: presets ----
	{
		const auto &vPresets = Presets();
		char aTitle[64];
		str_format(aTitle, sizeof(aTitle), "%s (%d)", Localize("Presets"), (int)vPresets.size());
		Headline(Left, aTitle);

		CUIRect Search;
		Left.HSplitTop(20.0f, &Search, &Left);
		Left.HSplitTop(6.0f, nullptr, &Left);
		static CLineInputBuffered<64> s_SearchInput;
		s_SearchInput.SetEmptyText(Localize("Search presets"));
		Ui()->DoClearableEditBox(&s_SearchInput, &Search, 12.0f);

		static CScrollRegion s_PresetScroll;
		CScrollRegionParams Params;
		Params.m_ScrollUnit = 60.0f;
		s_PresetScroll.Begin(&Left, &Params);
		CUIRect List = Left;

		static std::vector<CButtonContainer> s_vButtons(vPresets.size());
		const float CardH = 40.0f, Gap = 6.0f;
		const float CardW = (List.w - Gap) / 2.0f;
		int Shown = 0;
		for(size_t i = 0; i < vPresets.size(); i++)
		{
			const SFxPreset &Preset = vPresets[i];
			const char *pName = Localize(Preset.m_pName);
			const char *pHint = Localize(Preset.m_pHint);
			if(s_SearchInput.GetString()[0] && !str_utf8_find_nocase(pName, s_SearchInput.GetString()) && !str_utf8_find_nocase(Preset.m_pName, s_SearchInput.GetString()))
				continue;
			CUIRect Card = {List.x + (Shown % 2) * (CardW + Gap), List.y + (Shown / 2) * (CardH + Gap), CardW, CardH};
			Shown++;
			if(!s_PresetScroll.AddRect(Card))
				continue;
			const bool Active = g_Config.m_FogFxPreset == (int)i + 1;
			if(Ui()->DoButtonLogic(&s_vButtons[i], Active, &Card, BUTTONFLAG_LEFT))
			{
				ApplyFx(Preset.m_Values);
				g_Config.m_FogFxPreset = i + 1;
				g_Config.m_FogFxLive = 1;
			}
			const bool Hot = Ui()->HotItem() == &s_vButtons[i];
			const ColorRGBA Top(1.0f, 1.0f, 1.0f, Active ? 0.16f : (Hot ? 0.09f : 0.045f)), Bottom(1.0f, 1.0f, 1.0f, Active ? 0.09f : (Hot ? 0.05f : 0.02f));
			Graphics()->TextureClear();
			Graphics()->DrawRect4(Card.x, Card.y, Card.w, Card.h, Top, Top, Bottom, Bottom, IGraphics::CORNER_ALL, 10.0f);
			if(Active)
				Graphics()->DrawRect(Card.x + 3.0f, Card.y + 9.0f, 3.0f, Card.h - 18.0f, FROST, IGraphics::CORNER_ALL, 1.5f);
			CUIRect Name, Hint;
			Card.Margin(vec2(12.0f, 5.0f), &Card);
			Card.HSplitMid(&Name, &Hint);
			FogLabel(Ui(), TextRender(), Name, pName, 12.0f, TEXTALIGN_BL, Active || Hot ? FROST : ColorRGBA(0.84f, 0.87f, 0.91f, 1.0f), "Manrope", 700);
			FogLabel(Ui(), TextRender(), Hint, pHint, 9.0f, TEXTALIGN_TL, DIM, "Manrope", 500);
		}
		CUIRect End = {List.x, List.y + ((Shown + 1) / 2) * (CardH + Gap), List.w, 1.0f};
		s_PresetScroll.AddRect(End);
		s_PresetScroll.End();
	}

	// ---- right: every effect ----
	struct SEffect
	{
		const char *m_pName;
		int *m_pValue;
		int m_Min, m_Max, m_Neutral, m_On;
		const char *m_pSuffix;
	};
	const SEffect aEffects[] = {
		{Localize("Bloom"), &g_Config.m_FogFxBloom, 0, 300, 0, 80, "%"},
		{Localize("Bloom threshold"), &g_Config.m_FogFxBloomThreshold, 0, 100, 70, 70, "%"},
		{Localize("Bloom radius"), &g_Config.m_FogFxBloomRadius, 2, 64, 18, 18, "px"},
		{Localize("Exposure"), &g_Config.m_FogFxExposure, 20, 300, 100, 110, "%"},
		{Localize("Contrast"), &g_Config.m_FogFxContrast, 20, 300, 100, 115, "%"},
		{Localize("Saturation"), &g_Config.m_FogFxSaturation, 0, 300, 100, 125, "%"},
		{Localize("Vibrance"), &g_Config.m_FogFxVibrance, -100, 200, 0, 40, "%"},
		{Localize("Gamma"), &g_Config.m_FogFxGamma, 20, 300, 100, 110, "%"},
		{Localize("Temperature"), &g_Config.m_FogFxTemperature, -100, 100, 0, 30, ""},
		{Localize("Tint"), &g_Config.m_FogFxTint, -100, 100, 0, 15, ""},
		{Localize("Faded blacks"), &g_Config.m_FogFxFade, 0, 100, 0, 20, "%"},
		{Localize("Colour filter"), &g_Config.m_FogFxFilterAmount, 0, 100, 0, 40, "%"},
		{Localize("Fog haze"), &g_Config.m_FogFxHaze, 0, 100, 0, 30, "%"},
		{Localize("Vignette"), &g_Config.m_FogFxVignette, 0, 100, 0, 40, "%"},
		{Localize("Chromatic aberration"), &g_Config.m_FogFxChroma, 0, 100, 0, 25, ""},
		{Localize("Film grain"), &g_Config.m_FogFxGrain, 0, 100, 0, 15, "%"},
		{Localize("Sharpen"), &g_Config.m_FogFxSharpen, 0, 300, 0, 60, "%"},
		{Localize("Scanlines"), &g_Config.m_FogFxScanlines, 0, 100, 0, 35, "%"},
		{Localize("Posterize"), &g_Config.m_FogFxPosterize, 0, 32, 0, 8, ""},
		{Localize("Pixelate"), &g_Config.m_FogFxPixelate, 0, 16, 0, 3, "px"},
	};
	int NumActive = 0;
	for(const SEffect &Effect : aEffects)
		if(*Effect.m_pValue != Effect.m_Neutral)
			NumActive++;
	char aTitle[64];
	str_format(aTitle, sizeof(aTitle), "%s (%d)", Localize("Active effects"), NumActive);
	Headline(Right, aTitle);

	static CScrollRegion s_EffectScroll;
	CScrollRegionParams Params;
	Params.m_ScrollUnit = 60.0f;
	s_EffectScroll.Begin(&Right, &Params);
	CUIRect List = Right;

	static CButtonContainer s_aToggles[std::size(aEffects)], s_aResets[std::size(aEffects)];
	static int s_aSliderIds[std::size(aEffects)];
	const float RowH = 30.0f, Gap = 4.0f;
	for(size_t i = 0; i < std::size(aEffects); i++)
	{
		const SEffect &Effect = aEffects[i];
		CUIRect Row;
		List.HSplitTop(RowH, &Row, &List);
		List.HSplitTop(Gap, nullptr, &List);
		if(!s_EffectScroll.AddRect(Row))
			continue;
		const bool On = *Effect.m_pValue != Effect.m_Neutral;
		Graphics()->TextureClear();
		Graphics()->DrawRect(Row.x, Row.y, Row.w, Row.h, ColorRGBA(1.0f, 1.0f, 1.0f, On ? 0.07f : 0.03f), IGraphics::CORNER_ALL, 9.0f);

		CUIRect Toggle, Name, Slider, Value, Reset;
		Row.Margin(vec2(8.0f, 5.0f), &Row);
		const bool SubOption = Effect.m_Neutral == Effect.m_On; // tunes another effect, no switch of its own
		Row.VSplitLeft(36.0f, &Toggle, &Row);
		Row.VSplitRight(20.0f, &Row, &Reset);
		Row.VSplitRight(6.0f, &Row, nullptr);
		Row.VSplitRight(44.0f, &Row, &Value);
		Row.VSplitMid(&Name, &Slider, 8.0f);
		if(!SubOption && DoButton_CheckBox(&s_aToggles[i], "", On, &Toggle))
		{
			*Effect.m_pValue = On ? Effect.m_Neutral : Effect.m_On;
			g_Config.m_FogFxPreset = 0;
		}
		if(SubOption)
			Name.VSplitLeft(10.0f, nullptr, &Name);
		FogLabel(Ui(), TextRender(), Name, Effect.m_pName, SubOption ? 11.0f : 12.0f, TEXTALIGN_ML, On || SubOption ? FROST : ColorRGBA(0.78f, 0.81f, 0.86f, 1.0f), "Manrope", SubOption ? 500 : 600);

		const int Before = *Effect.m_pValue;
		const float Relative = (Before - Effect.m_Min) / (float)(Effect.m_Max - Effect.m_Min);
		const float NewRelative = Ui()->DoScrollbarH(&s_aSliderIds[i], &Slider, Relative);
		*Effect.m_pValue = std::clamp((int)std::round(Effect.m_Min + NewRelative * (Effect.m_Max - Effect.m_Min)), Effect.m_Min, Effect.m_Max);
		char aValue[32];
		str_format(aValue, sizeof(aValue), "%d%s", *Effect.m_pValue, Effect.m_pSuffix);
		FogLabel(Ui(), TextRender(), Value, aValue, 11.0f, TEXTALIGN_MR, On ? FROST : MIST, "Manrope", 700);

		if(Ui()->DoButton_FontIcon(&s_aResets[i], FontIcon::ARROW_ROTATE_LEFT, 0, &Reset, BUTTONFLAG_LEFT))
			*Effect.m_pValue = Effect.m_Neutral;
		if(*Effect.m_pValue != Before)
			g_Config.m_FogFxPreset = 0;
	}
	s_EffectScroll.End();
}
