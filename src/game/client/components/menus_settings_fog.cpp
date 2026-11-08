#include "menus.h"

#include <base/str.h>

#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/fog/cursor.h>
#include <game/client/components/fog/glass.h>
#include <game/client/components/fog/music.h>
#include <game/client/components/fog/trails.h>
#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <string>
#include <vector>

// Fog client settings: lyrics, their look and motion, music and glass panels.
void CMenus::RenderSettingsFogMusic(CUIRect MainView)
{
	constexpr float LINE = 20.0f;
	constexpr float SPACING = 3.0f;
	constexpr float HEADLINE = 18.0f;
	constexpr float FONT = 13.0f;
	constexpr float SECTION_GAP = 14.0f;
	constexpr float COLOR_LINE = 25.0f;
	constexpr float COLOR_LABEL = 13.0f;
	constexpr float COLOR_SPACING = 5.0f;

	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollParams);

	CUIRect LeftColumn, RightColumn;
	MainView.VSplitMid(&LeftColumn, &RightColumn, 20.0f);

	// every widget takes a row from the top of a column and registers it with the scroll region
	const auto Row = [&](CUIRect &Column, float Height) {
		CUIRect Rect;
		Column.HSplitTop(Height, &Rect, &Column);
		Column.HSplitTop(SPACING, nullptr, &Column);
		s_ScrollRegion.AddRect(Rect);
		return Rect;
	};
	const auto Headline = [&](CUIRect &Column, const char *pText) {
		CUIRect Rect = Row(Column, HEADLINE + 6.0f);
		Ui()->DoLabel(&Rect, pText, HEADLINE, TEXTALIGN_ML);
		Column.HSplitTop(2.0f, nullptr, &Column);
	};
	const auto Gap = [&](CUIRect &Column) { Column.HSplitTop(SECTION_GAP, nullptr, &Column); };
	const auto Slider = [&](CUIRect &Column, int *pValue, const char *pLabel, int Min, int Max, const char *pSuffix = "") {
		CUIRect Rect = Row(Column, LINE);
		Ui()->DoScrollbarOption(pValue, pValue, &Rect, pLabel, Min, Max, &CUi::ms_LinearScrollbarScale, 0u, pSuffix);
	};
	const auto CheckBox = [&](CUIRect &Column, int *pValue, const char *pLabel) {
		CUIRect Rect = Row(Column, LINE);
		if(DoButton_CheckBox(pValue, pLabel, *pValue, &Rect))
			*pValue ^= 1;
	};
	const auto DropDown = [&](CUIRect &Column, const char *pLabel, int Current, const char **ppItems, int Num, CUi::SDropDownState &State, CScrollRegion &Region) {
		CUIRect Rect = Row(Column, LINE);
		CUIRect Label, Box;
		Rect.VSplitMid(&Label, &Box, 10.0f);
		Ui()->DoLabel(&Label, pLabel, FONT, TEXTALIGN_ML);
		State.m_SelectionPopupContext.m_pScrollRegion = &Region;
		return Ui()->DoDropDown(&Box, Current, ppItems, Num, State);
	};
	const auto ColorLine = [&](CUIRect &Column, CButtonContainer *pReset, const char *pLabel, unsigned *pValue, unsigned Default, bool Alpha = false) {
		const float Top = Column.y;
		DoLine_ColorPicker(pReset, COLOR_LINE, COLOR_LABEL, COLOR_SPACING, &Column, pLabel, pValue, color_cast<ColorRGBA>(ColorHSLA(Default, Alpha)), false, nullptr, Alpha);
		CUIRect Used = {Column.x, Top, Column.w, Column.y - Top};
		s_ScrollRegion.AddRect(Used);
	};

	// ---------------------------------------------------------------- left: lyrics
	Headline(LeftColumn, Localize("Lyrics"));
	CheckBox(LeftColumn, &g_Config.m_FogLyrics, Localize("Show synced lyrics while music plays"));
	CheckBox(LeftColumn, &g_Config.m_FogLyricsOnline, Localize("Download lyrics from LRCLIB"));
	CheckBox(LeftColumn, &g_Config.m_FogLyricsNextLine, Localize("Show the next line in the fog"));
	{
		CUIRect Rect = Row(LeftColumn, LINE);
		static CButtonContainer s_PreviewButton;
		if(DoButton_Menu(&s_PreviewButton, Localize("Play the lyrics preview in game"), 0, &Rect))
			Console()->ExecuteLine("fog_lyrics_preview", IConsole::CLIENT_ID_UNSPECIFIED);
	}

	Gap(LeftColumn);
	Headline(LeftColumn, Localize("Font"));
	{
		static std::vector<std::string> s_vFamilies;
		static std::vector<const char *> s_vpNames;
		if(s_vFamilies.empty())
		{
			s_vFamilies.emplace_back(Localize("Game font"));
			for(const std::string &Family : TextRender()->DisplayFontFamilies())
				s_vFamilies.push_back(Family);
			for(const std::string &Family : s_vFamilies)
				s_vpNames.push_back(Family.c_str());
		}
		int Current = 0;
		for(size_t i = 1; i < s_vFamilies.size(); i++)
			if(str_comp_nocase(s_vFamilies[i].c_str(), g_Config.m_FogLyricsFont) == 0)
				Current = (int)i;
		static CUi::SDropDownState s_FontState;
		static CScrollRegion s_FontRegion;
		const int New = DropDown(LeftColumn, Localize("Lyrics font"), Current, s_vpNames.data(), (int)s_vpNames.size(), s_FontState, s_FontRegion);
		if(New != Current)
			str_copy(g_Config.m_FogLyricsFont, New == 0 ? "" : s_vFamilies[New].c_str());
	}
	Slider(LeftColumn, &g_Config.m_FogLyricsWeight, Localize("Weight"), 200, 1000);
	Slider(LeftColumn, &g_Config.m_FogLyricsSize, Localize("Size"), 25, 400, "%");

	Gap(LeftColumn);
	Headline(LeftColumn, Localize("Placement and motion"));
	{
		const char *apModes[] = {Localize("Next to you"), Localize("Trail left in the map"), Localize("Screen karaoke")};
		static CUi::SDropDownState s_ModeState;
		static CScrollRegion s_ModeRegion;
		g_Config.m_FogLyricsMode = DropDown(LeftColumn, Localize("Where lyrics appear"), g_Config.m_FogLyricsMode, apModes, std::size(apModes), s_ModeState, s_ModeRegion);
	}
	if(g_Config.m_FogLyricsMode == 1)
		Slider(LeftColumn, &g_Config.m_FogLyricsTrailTime, Localize("Lines stay for"), 2, 60, " s");
	{
		const char *apStyles[] = {Localize("Glide"), Localize("Wave"), Localize("Typewriter"), Localize("Bounce"), Localize("Calm")};
		static CUi::SDropDownState s_StyleState;
		static CScrollRegion s_StyleRegion;
		g_Config.m_FogLyricsStyle = DropDown(LeftColumn, Localize("Letter animation"), g_Config.m_FogLyricsStyle, apStyles, std::size(apStyles), s_StyleState, s_StyleRegion);
	}
	{
		const char *apHighlight[] = {Localize("Whole words"), Localize("Letter by letter"), Localize("Smooth sweep")};
		static CUi::SDropDownState s_HighlightState;
		static CScrollRegion s_HighlightRegion;
		g_Config.m_FogLyricsHighlight = DropDown(LeftColumn, Localize("Highlight"), g_Config.m_FogLyricsHighlight, apHighlight, std::size(apHighlight), s_HighlightState, s_HighlightRegion);
	}
	if(g_Config.m_FogLyricsMode != 2)
	{
		const char *apLayers[] = {Localize("Inside the map"), Localize("Above everything")};
		static CUi::SDropDownState s_LayerState;
		static CScrollRegion s_LayerRegion;
		g_Config.m_FogLyricsLayer = DropDown(LeftColumn, Localize("Depth"), g_Config.m_FogLyricsLayer, apLayers, std::size(apLayers), s_LayerState, s_LayerRegion);
	}
	if(g_Config.m_FogLyricsMode == 0)
	{
		Slider(LeftColumn, &g_Config.m_FogLyricsFollow, Localize("Floatiness"), 0, 100, "%");
		Slider(LeftColumn, &g_Config.m_FogLyricsParallax, Localize("Distance (parallax)"), 0, 90, "%");
	}
	if(g_Config.m_FogLyricsMode != 2)
	{
		Slider(LeftColumn, &g_Config.m_FogLyricsOffsetX, Localize("Offset sideways"), -1000, 1000);
		Slider(LeftColumn, &g_Config.m_FogLyricsOffsetY, Localize("Offset up and down"), -1000, 1000);
	}
	Slider(LeftColumn, &g_Config.m_FogLyricsCurve, Localize("Arc"), -100, 100);
	Slider(LeftColumn, &g_Config.m_FogLyricsTilt, Localize("Tilt"), -45, 45, "°");
	Slider(LeftColumn, &g_Config.m_FogLyricsPerspective, Localize("Perspective"), -100, 100);
	Slider(LeftColumn, &g_Config.m_FogLyricsDelay, Localize("Timing offset"), -5000, 5000, " ms");

	// ---------------------------------------------------------------- right: light, color, music
	Headline(RightColumn, Localize("Light and depth"));
	Slider(RightColumn, &g_Config.m_FogLyricsGlow, Localize("Glow"), 0, 100, "%");
	Slider(RightColumn, &g_Config.m_FogLyricsGlowSize, Localize("Glow spread"), 20, 300, "%");
	Slider(RightColumn, &g_Config.m_FogLyricsShadow, Localize("Shadow"), 0, 100, "%");
	Slider(RightColumn, &g_Config.m_FogLyricsExtrude, Localize("3D depth"), 0, 16);
	Slider(RightColumn, &g_Config.m_FogLyricsVeil, Localize("Mist behind the words"), 0, 100, "%");

	Gap(RightColumn);
	Headline(RightColumn, Localize("Colors"));
	static CButtonContainer s_aColorReset[6];
	ColorLine(RightColumn, &s_aColorReset[0], Localize("Sung letters"), &g_Config.m_FogLyricsColor, DefaultConfig::FogLyricsColor);
	ColorLine(RightColumn, &s_aColorReset[1], Localize("Glow"), &g_Config.m_FogLyricsGlowColor, DefaultConfig::FogLyricsGlowColor);
	ColorLine(RightColumn, &s_aColorReset[2], Localize("Letters still to come"), &g_Config.m_FogLyricsDimColor, DefaultConfig::FogLyricsDimColor);
	ColorLine(RightColumn, &s_aColorReset[3], Localize("3D depth"), &g_Config.m_FogLyricsExtrudeColor, DefaultConfig::FogLyricsExtrudeColor);
	ColorLine(RightColumn, &s_aColorReset[4], Localize("Mist"), &g_Config.m_FogLyricsVeilColor, DefaultConfig::FogLyricsVeilColor);

	Gap(RightColumn);
	Headline(RightColumn, Localize("Music"));
	CheckBox(RightColumn, &g_Config.m_FogMusic, Localize("Read the current track from Spotify and other players"));
	CheckBox(RightColumn, &g_Config.m_FogMusicWidget, Localize("Show the now playing card in game"));
	if(g_Config.m_FogMusicWidget)
	{
		const char *apCorners[] = {Localize("Top left"), Localize("Top right"), Localize("Bottom left"), Localize("Bottom right")};
		static CUi::SDropDownState s_CornerState;
		static CScrollRegion s_CornerRegion;
		g_Config.m_FogMusicWidgetCorner = DropDown(RightColumn, Localize("Card position"), g_Config.m_FogMusicWidgetCorner, apCorners, std::size(apCorners), s_CornerState, s_CornerRegion);
	}
	{
		// what the client sees right now, so problems explain themselves
		const CFogMusic &Music = GameClient()->m_FogMusic;
		char aStatus[512];
		if(!g_Config.m_FogMusic)
			str_copy(aStatus, Localize("Music is off."));
		else if(!Music.HasTrack())
			str_copy(aStatus, Localize("No player is playing. Start Spotify, or any app that supports MPRIS."));
		else
		{
			const char *pLyrics = "";
			switch(Music.LyricsStatus())
			{
			case CFogMusic::ELyricsStatus::READY: pLyrics = Localize("synced lyrics found"); break;
			case CFogMusic::ELyricsStatus::LOADING: pLyrics = Localize("looking for lyrics…"); break;
			case CFogMusic::ELyricsStatus::INSTRUMENTAL: pLyrics = Localize("instrumental"); break;
			case CFogMusic::ELyricsStatus::NOT_FOUND: pLyrics = Localize("no synced lyrics for this track"); break;
			case CFogMusic::ELyricsStatus::NONE: pLyrics = Localize("lyrics are off"); break;
			}
			str_format(aStatus, sizeof(aStatus), "%s: %s, %s (%s)", Localize("Now playing"), Music.Media().m_Artist.c_str(), Music.Media().m_Title.c_str(), pLyrics);
		}
		CUIRect Rect = Row(RightColumn, LINE * 2.0f);
		SLabelProperties Props;
		Props.m_MaxWidth = Rect.w;
		TextRender()->TextColor(ColorRGBA(0.75f, 0.78f, 0.84f, 1.0f));
		Ui()->DoLabel(&Rect, aStatus, 11.0f, TEXTALIGN_TL, Props);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	s_ScrollRegion.End();
}

// Fog visuals that are not shaders: the world (map fog, colours, tiles, live background),
// trails and player plates. Section: 0 world, 1 trails, 2 players.
void CMenus::RenderSettingsFogVisuals(CUIRect MainView, int Section)
{
	constexpr float LINE = 20.0f;
	constexpr float SPACING = 3.0f;
	constexpr float HEADLINE = 18.0f;
	constexpr float FONT = 13.0f;
	constexpr float SECTION_GAP = 14.0f;
	constexpr float COLOR_LINE = 25.0f;
	constexpr float COLOR_LABEL = 13.0f;
	constexpr float COLOR_SPACING = 5.0f;

	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollParams);

	CUIRect LeftColumn, RightColumn;
	MainView.VSplitMid(&LeftColumn, &RightColumn, 20.0f);

	// every widget takes a row from the top of a column and registers it with the scroll region
	const auto Row = [&](CUIRect &Column, float Height) {
		CUIRect Rect;
		Column.HSplitTop(Height, &Rect, &Column);
		Column.HSplitTop(SPACING, nullptr, &Column);
		s_ScrollRegion.AddRect(Rect);
		return Rect;
	};
	const auto Headline = [&](CUIRect &Column, const char *pText) {
		CUIRect Rect = Row(Column, HEADLINE + 6.0f);
		Ui()->DoLabel(&Rect, pText, HEADLINE, TEXTALIGN_ML);
		Column.HSplitTop(2.0f, nullptr, &Column);
	};
	const auto Gap = [&](CUIRect &Column) { Column.HSplitTop(SECTION_GAP, nullptr, &Column); };
	const auto Slider = [&](CUIRect &Column, int *pValue, const char *pLabel, int Min, int Max, const char *pSuffix = "") {
		CUIRect Rect = Row(Column, LINE);
		Ui()->DoScrollbarOption(pValue, pValue, &Rect, pLabel, Min, Max, &CUi::ms_LinearScrollbarScale, 0u, pSuffix);
	};
	const auto CheckBox = [&](CUIRect &Column, int *pValue, const char *pLabel) {
		CUIRect Rect = Row(Column, LINE);
		if(DoButton_CheckBox(pValue, pLabel, *pValue, &Rect))
			*pValue ^= 1;
	};
	const auto DropDown = [&](CUIRect &Column, const char *pLabel, int Current, const char **ppItems, int Num, CUi::SDropDownState &State, CScrollRegion &Region) {
		CUIRect Rect = Row(Column, LINE);
		CUIRect Label, Box;
		Rect.VSplitMid(&Label, &Box, 10.0f);
		Ui()->DoLabel(&Label, pLabel, FONT, TEXTALIGN_ML);
		State.m_SelectionPopupContext.m_pScrollRegion = &Region;
		return Ui()->DoDropDown(&Box, Current, ppItems, Num, State);
	};
	const auto ColorLine = [&](CUIRect &Column, CButtonContainer *pReset, const char *pLabel, unsigned *pValue, unsigned Default, bool Alpha = false) {
		const float Top = Column.y;
		DoLine_ColorPicker(pReset, COLOR_LINE, COLOR_LABEL, COLOR_SPACING, &Column, pLabel, pValue, color_cast<ColorRGBA>(ColorHSLA(Default, Alpha)), false, nullptr, Alpha);
		CUIRect Used = {Column.x, Top, Column.w, Column.y - Top};
		s_ScrollRegion.AddRect(Used);
	};

	if(Section == 0)
	{
		Headline(LeftColumn, Localize("Fog over the map"));
		CheckBox(LeftColumn, &g_Config.m_FogAtmo, Localize("Real fog drifting over the map"));
		CheckBox(LeftColumn, &g_Config.m_FogAtmoFront, Localize("A thin layer in front of the tees (clears around you)"));
		Slider(LeftColumn, &g_Config.m_FogAtmoDensity, Localize("Density"), 0, 300, "%");
		Slider(LeftColumn, &g_Config.m_FogAtmoWind, Localize("Wind"), 0, 400, "%");
		Gap(LeftColumn);
		Headline(LeftColumn, Localize("Map colours"));
		CheckBox(LeftColumn, &g_Config.m_FogMapres, Localize("Fog map tiles (standard tilesets, from the next map)"));
		Slider(LeftColumn, &g_Config.m_FogMapTint, Localize("Fog over map colors (from the next map)"), 0, 100, "%");
		Slider(LeftColumn, &g_Config.m_FogMapBrightness, Localize("World brightness (from the next map)"), 10, 120, "%");
		{
			static CUi::SDropDownState s_BgState;
			static CScrollRegion s_BgScroll;
			s_BgState.m_SelectionPopupContext.m_pScrollRegion = &s_BgScroll;
			const char *apStyles[] = {Localize("Still"), Localize("Fog"), Localize("Aurora"), Localize("Rain on glass"), Localize("The deep"), Localize("Ember dusk")};
			CUIRect RowRect, Label, Box;
			LeftColumn.HSplitTop(20.0f, &RowRect, &LeftColumn);
			LeftColumn.HSplitTop(2.0f, nullptr, &LeftColumn);
			RowRect.VSplitMid(&Label, &Box, 10.0f);
			Ui()->DoLabel(&Label, Localize("Live background"), 13.0f, TEXTALIGN_ML);
			g_Config.m_FogBgStyle = Ui()->DoDropDown(&Box, g_Config.m_FogBgStyle, apStyles, std::size(apStyles), s_BgState);
			s_ScrollRegion.AddRect(RowRect);
		}
		CheckBox(LeftColumn, &g_Config.m_FogBgIngame, Localize("Live background instead of the map background in game"));
		CUIRect Hint = Row(RightColumn, LINE * 3.0f);
		SLabelProperties Props;
		Props.m_MaxWidth = Hint.w;
		TextRender()->TextColor(ColorRGBA(0.70f, 0.74f, 0.80f, 1.0f));
		Ui()->DoLabel(&Hint, Localize("Map colours, tiles and brightness apply when the next map loads. The fog and the live background change right away."), 12.0f, TEXTALIGN_TL, Props);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		Gap(RightColumn);
		Headline(RightColumn, Localize("Weather"));
		{
			static CUi::SDropDownState s_WeatherState;
			static CScrollRegion s_WeatherScroll;
			const char *apWeather[] = {Localize("None", "Tee studio"), Localize("Rain"), Localize("Snow"), Localize("Ash"), Localize("Pollen")};
			g_Config.m_FogWeather = DropDown(RightColumn, Localize("Weather"), g_Config.m_FogWeather, apWeather, std::size(apWeather), s_WeatherState, s_WeatherScroll);
		}
		Slider(RightColumn, &g_Config.m_FogWeatherAmount, Localize("Amount"), 10, 300, "%");
		Slider(RightColumn, &g_Config.m_FogWeatherWind, Localize("Wind"), -200, 200, "%");
		Gap(RightColumn);
		Headline(RightColumn, Localize("Light and reflections"));
		CheckBox(RightColumn, &g_Config.m_FogReflections, Localize("Tees are mirrored in the floor"));
		Slider(RightColumn, &g_Config.m_FogReflectionsAlpha, Localize("Reflection strength"), 5, 60, "%");
		CheckBox(RightColumn, &g_Config.m_FogShadows, Localize("Long soft shadows"));
		CheckBox(RightColumn, &g_Config.m_FogDayCycle, Localize("Time of day follows your clock"));
		Slider(RightColumn, &g_Config.m_FogDayCycleStrength, Localize("Time of day strength"), 0, 100, "%");
		CheckBox(RightColumn, &g_Config.m_FogFlashlight, Localize("Darkness with a light around you"));
		Slider(RightColumn, &g_Config.m_FogFlashlightRadius, Localize("Light radius"), 100, 900, "");
		Slider(RightColumn, &g_Config.m_FogFlashlightDark, Localize("Darkness"), 20, 95, "%");
		CheckBox(RightColumn, &g_Config.m_FogMusicPulse, Localize("Fog and auras breathe with the music"));
		Gap(RightColumn);
		Headline(RightColumn, Localize("Holidays"));
		CheckBox(RightColumn, &g_Config.m_FogHolidays, Localize("Holiday atmosphere"));
		{
			static CUi::SDropDownState s_HolidayState;
			static CScrollRegion s_HolidayScroll;
			const char *apHolidays[] = {Localize("By date"), Localize("New Year"), Localize("Halloween"), Localize("Valentine's Day"), Localize("March 8")};
			g_Config.m_FogHolidayPreview = DropDown(RightColumn, Localize("Preview"), g_Config.m_FogHolidayPreview, apHolidays, std::size(apHolidays), s_HolidayState, s_HolidayScroll);
		}
		Gap(RightColumn);
		Headline(RightColumn, Localize("Camera"));
		Slider(RightColumn, &g_Config.m_FogCameraLag, Localize("Soft camera"), 0, 100, "%");
		CheckBox(RightColumn, &g_Config.m_FogSpeedVignette, Localize("Dark edges at high speed"));
	}
	else if(Section == 6)
	{
		Headline(LeftColumn, Localize("Light and shadow"));
		CheckBox(LeftColumn, &g_Config.m_FogLight, Localize("Dynamic lights with shadows"));
		Slider(LeftColumn, &g_Config.m_FogLightAmbient, Localize("Darkness outside the light"), 0, 90, "%");
		Slider(LeftColumn, &g_Config.m_FogLightStrength, Localize("Light strength"), 10, 300, "%");
		Slider(LeftColumn, &g_Config.m_FogLightRadius, Localize("Light reach"), 30, 300, "%");
		CheckBox(LeftColumn, &g_Config.m_FogAo, Localize("Ambient occlusion"));
		Slider(LeftColumn, &g_Config.m_FogAoStrength, Localize("Occlusion strength"), 10, 300, "%");
		CheckBox(LeftColumn, &g_Config.m_FogGodRays, Localize("Light shafts from above"));
		Slider(LeftColumn, &g_Config.m_FogGodRaysStrength, Localize("Shaft strength"), 10, 300, "%");
		CheckBox(LeftColumn, &g_Config.m_FogWallShadows, Localize("Tee shadows on walls"));
		Headline(RightColumn, Localize("Glow and detail"));
		CheckBox(RightColumn, &g_Config.m_FogSourceGlow, Localize("Glow only on light sources"));
		CheckBox(RightColumn, &g_Config.m_FogRimLight, Localize("Rim light on tees"));
		CheckBox(RightColumn, &g_Config.m_FogFreezeShimmer, Localize("Shimmer over freeze and death"));
		CheckBox(RightColumn, &g_Config.m_FogDof, Localize("Blurred far background"));
		Slider(RightColumn, &g_Config.m_FogDofStrength, Localize("Background blur"), 10, 300, "%");
	}
	else if(Section == 5)
	{
		Headline(LeftColumn, Localize("Movement"));
		CheckBox(LeftColumn, &g_Config.m_FogFunFootprints, Localize("Footprints that melt away"));
		CheckBox(LeftColumn, &g_Config.m_FogFunDust, Localize("Dust when landing, jumping and stopping"));
		CheckBox(LeftColumn, &g_Config.m_FogFunWind, Localize("Wind lines at high speed"));
		CheckBox(LeftColumn, &g_Config.m_FogFunHookSparks, Localize("A flash and sparks where the hook grabs"));
		Gap(LeftColumn);
		Headline(LeftColumn, Localize("Death and freeze"));
		CheckBox(LeftColumn, &g_Config.m_FogFunDeathAsh, Localize("Tees crumble into ash when they die"));
		CheckBox(LeftColumn, &g_Config.m_FogFunFrost, Localize("Frost on frozen tees, melting into drops"));
		Headline(RightColumn, Localize("Mood"));
		CheckBox(RightColumn, &g_Config.m_FogFunThoughts, Localize("Thought bubbles above your tee"));
		CheckBox(RightColumn, &g_Config.m_FogFunFinish, Localize("Tears of relief and confetti at the finish"));
		CheckBox(RightColumn, &g_Config.m_FogFunNotes, Localize("Notes from the headphones accessory"));
	}
	else if(Section == 1)
	{
		Headline(LeftColumn, Localize("Trails"));
		CheckBox(LeftColumn, &g_Config.m_FogTrail, Localize("Leave afterimages when moving fast"));
		if(g_Config.m_FogTrail)
		{
			const char *apPresets[] = {"Sandevistan", Localize("Fog"), Localize("Glitch"), Localize("Phantom"), Localize("Custom colors")};
			static CUi::SDropDownState s_PresetState;
			static CScrollRegion s_PresetRegion;
			const int Preset = DropDown(LeftColumn, Localize("Preset"), g_Config.m_FogTrailPreset, apPresets, std::size(apPresets), s_PresetState, s_PresetRegion);
			if(Preset != g_Config.m_FogTrailPreset)
			{
				// each preset comes with its own rhythm; the sliders below can still change it
				g_Config.m_FogTrailPreset = Preset;
				CFogTrails::PresetTiming(Preset, g_Config.m_FogTrailInterval, g_Config.m_FogTrailLifetime, g_Config.m_FogTrailSpacing);
			}
			char aSpeedSuffix[32];
			str_format(aSpeedSuffix, sizeof(aSpeedSuffix), " %s", Localize("tiles/s"));
			Slider(LeftColumn, &g_Config.m_FogTrailMinSpeed, Localize("Appears from speed"), 0, 100, aSpeedSuffix);
			Slider(LeftColumn, &g_Config.m_FogTrailLifetime, Localize("Each copy stays"), 50, 3000, " ms");
			Slider(LeftColumn, &g_Config.m_FogTrailInterval, Localize("Time between copies"), 8, 300, " ms");
			Slider(LeftColumn, &g_Config.m_FogTrailSpacing, Localize("Distance between copies"), 0, 300);
			Slider(LeftColumn, &g_Config.m_FogTrailMax, Localize("Copies at most"), 1, 64);
			Slider(LeftColumn, &g_Config.m_FogTrailOpacity, Localize("Opacity"), 0, 100, "%");
			Slider(LeftColumn, &g_Config.m_FogTrailGlow, Localize("Glow"), 0, 100, "%");
			CheckBox(LeftColumn, &g_Config.m_FogTrailOthers, Localize("Show trails of other players"));
			if(g_Config.m_FogTrailPreset == CFogTrails::PRESET_CUSTOM)
			{
				static CButtonContainer s_aTrailColorReset[2];
				ColorLine(LeftColumn, &s_aTrailColorReset[0], Localize("Fresh copies"), &g_Config.m_FogTrailColor1, DefaultConfig::FogTrailColor1);
				ColorLine(LeftColumn, &s_aTrailColorReset[1], Localize("Fading copies"), &g_Config.m_FogTrailColor2, DefaultConfig::FogTrailColor2);
			}
		}
	}
	else if(Section == 4)
	{
		Headline(LeftColumn, Localize("Input"));
		CheckBox(LeftColumn, &g_Config.m_FogFastInput, Localize("Fast input: your tee reacts about one tick earlier"));
		{
			CUIRect Hint = Row(LeftColumn, LINE * 2.0f);
			SLabelProperties Props;
			Props.m_MaxWidth = Hint.w;
			TextRender()->TextColor(ColorRGBA(0.70f, 0.74f, 0.80f, 1.0f));
			Ui()->DoLabel(&Hint, Localize("Uses your newest key presses for the prediction before they are sent. Rarely the tee jumps back a little when the server disagrees."), 11.0f, TEXTALIGN_TL, Props);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
		CheckBox(LeftColumn, &g_Config.m_FogSmoothKatana, Localize("Smooth katana: the dash glides instead of moving in steps"));
		Slider(LeftColumn, &g_Config.m_ClPredictionMargin, Localize("Prediction margin"), 1, 300, " ms");
		{
			const char *apFreeze[] = {Localize("Off"), Localize("On"), Localize("Partial")};
			static CUi::SDropDownState s_FreezeState;
			static CScrollRegion s_FreezeRegion;
			g_Config.m_ClPredictFreeze = DropDown(LeftColumn, Localize("Predict freeze"), g_Config.m_ClPredictFreeze, apFreeze, std::size(apFreeze), s_FreezeState, s_FreezeRegion);
		}

		Headline(RightColumn, Localize("Antiping"));
		CheckBox(RightColumn, &g_Config.m_ClAntiPing, Localize("Antiping: predict the world ahead of the server"));
		if(g_Config.m_ClAntiPing)
		{
			const char *apPlayers[] = {Localize("Off"), Localize("Always"), Localize("Only when interacting"), Localize("Interacting and frozen tees")};
			static CUi::SDropDownState s_PlayersState;
			static CScrollRegion s_PlayersRegion;
			g_Config.m_ClAntiPingPlayers = DropDown(RightColumn, Localize("Other players"), g_Config.m_ClAntiPingPlayers, apPlayers, std::size(apPlayers), s_PlayersState, s_PlayersRegion);
			CheckBox(RightColumn, &g_Config.m_ClAntiPingGrenade, Localize("Grenades"));
			CheckBox(RightColumn, &g_Config.m_ClAntiPingWeapons, Localize("Weapon projectiles"));
			CheckBox(RightColumn, &g_Config.m_ClAntiPingGunfire, Localize("Gunfire"));
			CheckBox(RightColumn, &g_Config.m_ClAntiPingSmooth, Localize("Smoother movement of others"));
			CheckBox(RightColumn, &g_Config.m_ClAntiPingPreInput, Localize("Use other players' pre-inputs"));
			Slider(RightColumn, &g_Config.m_ClAntiPingPercent, Localize("How far ahead"), 0, 100, "%");
			Slider(RightColumn, &g_Config.m_ClAntiPingLimit, Localize("Delay limit (0 = off)"), 0, 500, " ms");
		}
	}
	else if(Section == 3)
	{
		static CButtonContainer s_GlassTintReset;
		Headline(LeftColumn, Localize("Font"));
		{
			// one family for every text in the game (missing letters fall back to the game fonts)
			static std::vector<std::string> s_vGameFamilies;
			static std::vector<const char *> s_vpGameNames;
			if(s_vGameFamilies.empty())
			{
				s_vGameFamilies.emplace_back(Localize("Game font"));
				for(const std::string &Family : TextRender()->DisplayFontFamilies())
					s_vGameFamilies.push_back(Family);
				for(const std::string &Family : s_vGameFamilies)
					s_vpGameNames.push_back(Family.c_str());
			}
			int Current = 0;
			for(size_t i = 1; i < s_vGameFamilies.size(); i++)
				if(str_comp_nocase(s_vGameFamilies[i].c_str(), g_Config.m_FogFont) == 0)
					Current = (int)i;
			static CUi::SDropDownState s_GameFontState;
			static CScrollRegion s_GameFontRegion;
			const int New = DropDown(LeftColumn, Localize("Font of the game"), Current, s_vpGameNames.data(), (int)s_vpGameNames.size(), s_GameFontState, s_GameFontRegion);
			if(New != Current)
				str_copy(g_Config.m_FogFont, New == 0 ? "" : s_vGameFamilies[New].c_str());
		}
		Gap(LeftColumn);
		Headline(LeftColumn, Localize("Animation"));
		CheckBox(LeftColumn, &g_Config.m_FogAnim, Localize("Animate the Fog interface and lyrics"));
		if(g_Config.m_FogAnim)
		{
			Slider(LeftColumn, &g_Config.m_FogAnimSpeed, Localize("Speed"), 25, 400, "%");
			Slider(LeftColumn, &g_Config.m_FogAnimIntensity, Localize("Intensity"), 0, 200, "%");
		}

		Gap(LeftColumn);
		Headline(LeftColumn, Localize("Glass panels"));
		CheckBox(LeftColumn, &g_Config.m_FogUiMenus, Localize("Fog glass for the whole menu (experimental)"));
		CheckBox(LeftColumn, &g_Config.m_FogHud, Localize("Fog HUD: timer, checkpoints and finish"));
		Slider(LeftColumn, &g_Config.m_FogUiSheen, Localize("Light on the glass"), 0, 100, "%");
		ColorLine(LeftColumn, &s_GlassTintReset, Localize("Glass tint"), &g_Config.m_FogUiTint, DefaultConfig::FogUiTint, true);

		// crosshair, with a live preview
		Headline(RightColumn, Localize("Crosshair"));
		CheckBox(RightColumn, &g_Config.m_FogCrosshair, Localize("Fog crosshair"));
		if(g_Config.m_FogCrosshair)
		{
			CheckBox(RightColumn, &g_Config.m_FogCrosshairOverride, Localize("Also replace crosshairs from game skin files"));
			{
				CUIRect Preview = Row(RightColumn, 70.0f);
				Preview.VSplitLeft(70.0f, &Preview, nullptr);
				Graphics()->TextureClear();
				Graphics()->DrawRect(Preview.x, Preview.y, Preview.w, Preview.h, ColorRGBA(0.12f, 0.14f, 0.17f, 0.9f), IGraphics::CORNER_ALL, 10.0f);
				FogDrawCrosshair(Graphics(), Preview.Center(), 1.0f, 0.0f);
			}
			const char *apShapes[] = {Localize("Cross"), Localize("Dot"), Localize("Cross and dot"), Localize("Circle"), Localize("Circle and dot"), Localize("T shape"), Localize("Chevron")};
			static CUi::SDropDownState s_ShapeState;
			static CScrollRegion s_ShapeRegion;
			g_Config.m_FogCrosshairShape = DropDown(RightColumn, Localize("Shape"), g_Config.m_FogCrosshairShape, apShapes, std::size(apShapes), s_ShapeState, s_ShapeRegion);
			Slider(RightColumn, &g_Config.m_FogCrosshairSize, Localize("Size"), 4, 64);
			Slider(RightColumn, &g_Config.m_FogCrosshairThickness, Localize("Thickness"), 1, 8);
			Slider(RightColumn, &g_Config.m_FogCrosshairGap, Localize("Gap"), 0, 24);
			Slider(RightColumn, &g_Config.m_FogCrosshairAlpha, Localize("Opacity"), 10, 100, "%");
			static CButtonContainer s_CrosshairColorReset;
			ColorLine(RightColumn, &s_CrosshairColorReset, Localize("Colour"), &g_Config.m_FogCrosshairColor, DefaultConfig::FogCrosshairColor);
			CheckBox(RightColumn, &g_Config.m_FogCrosshairOutline, Localize("Dark outline"));
			CheckBox(RightColumn, &g_Config.m_FogCrosshairDynamic, Localize("Opens up while you move fast"));
		}
		Gap(RightColumn);
		Headline(RightColumn, Localize("Menu cursor"));
		{
			const char *apCursors[] = {Localize("Game arrow"), Localize("Hook head"), Localize("Fog mark"), Localize("Dot")};
			static CUi::SDropDownState s_CursorState;
			static CScrollRegion s_CursorRegion;
			g_Config.m_FogCursor = DropDown(RightColumn, Localize("Cursor"), g_Config.m_FogCursor, apCursors, std::size(apCursors), s_CursorState, s_CursorRegion);
			Slider(RightColumn, &g_Config.m_FogCursorSize, Localize("Cursor size"), 50, 200, "%");
		}
		Gap(RightColumn);
		Headline(RightColumn, Localize("Game sounds"));
		{
			static const char *s_apPackIds[] = {"", "fog", "crisp", "retro", "deep", "arcade", "quiet"};
			const char *apPacks[] = {Localize("Game sounds"), Localize("Fog: soft, with an echo"), Localize("Crisp: bright and dry"), Localize("Retro: 8-bit"), Localize("Deep: lower"), Localize("Arcade: higher"), Localize("Quiet: for streams")};
			int Current = 0;
			for(int i = 0; i < (int)std::size(s_apPackIds); i++)
				if(str_comp(g_Config.m_FogSoundPack, s_apPackIds[i]) == 0)
					Current = i;
			static CUi::SDropDownState s_PackState;
			static CScrollRegion s_PackRegion;
			const int New = DropDown(RightColumn, Localize("Sound pack"), Current, apPacks, std::size(apPacks), s_PackState, s_PackRegion);
			if(New != Current)
			{
				str_copy(g_Config.m_FogSoundPack, s_apPackIds[New]);
				GameClient()->m_Sounds.FogReloadPack();
				GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_HOOK_ATTACH_GROUND, 1.0f);
			}
			CUIRect Rect = Row(RightColumn, LINE);
			static CButtonContainer s_ListenButton;
			if(DoButton_Menu(&s_ListenButton, Localize("Listen: hook, jump, gun"), 0, &Rect))
			{
				GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_HOOK_ATTACH_GROUND, 1.0f);
				GameClient()->m_Sounds.Enqueue(CSounds::CHN_GUI, SOUND_PLAYER_JUMP);
				GameClient()->m_Sounds.Enqueue(CSounds::CHN_GUI, SOUND_GUN_FIRE);
			}
		}
	}
	else
	{
		Headline(LeftColumn, Localize("Above the players"));
		CheckBox(LeftColumn, &g_Config.m_FogNamePlatesPing, Localize("Ping of other players"));
		CheckBox(LeftColumn, &g_Config.m_ClNamePlatesClan, Localize("Clan"));
		CheckBox(LeftColumn, &g_Config.m_ClNamePlatesIds, Localize("Client ID"));
		{
			const char *apStrong[] = {Localize("Off"), Localize("Arrow"), Localize("Arrow and ID")};
			static CUi::SDropDownState s_StrongState;
			static CScrollRegion s_StrongRegion;
			g_Config.m_ClNamePlatesStrong = DropDown(LeftColumn, Localize("Hook strength"), g_Config.m_ClNamePlatesStrong, apStrong, std::size(apStrong), s_StrongState, s_StrongRegion);
		}
		CheckBox(LeftColumn, &g_Config.m_ClNamePlatesFriendMark, Localize("Friend mark"));
		Slider(LeftColumn, &g_Config.m_ClNamePlatesSize, Localize("Name size"), -50, 100);

		// your own name, as other Fog players see it
		Headline(RightColumn, Localize("Your name for Fog players"));
		{
			const char *apStyles[] = {Localize("Plain"), Localize("Shimmer"), Localize("Gradient"), Localize("Rainbow"), Localize("Pulse"), Localize("Wave")};
			static CUi::SDropDownState s_StyleState;
			static CScrollRegion s_StyleRegion;
			g_Config.m_FogNameStyle = DropDown(RightColumn, Localize("Style"), g_Config.m_FogNameStyle, apStyles, std::size(apStyles), s_StyleState, s_StyleRegion);
		}
		static CButtonContainer s_aNameColorReset[2];
		ColorLine(RightColumn, &s_aNameColorReset[0], Localize("First colour"), &g_Config.m_FogNameColor1, DefaultConfig::FogNameColor1);
		ColorLine(RightColumn, &s_aNameColorReset[1], Localize("Second colour"), &g_Config.m_FogNameColor2, DefaultConfig::FogNameColor2);
		Slider(RightColumn, &g_Config.m_FogNameSpeed, Localize("Animation speed"), 25, 400, "%");
		CheckBox(RightColumn, &g_Config.m_FogBadge, Localize("Fog mark before your name"));
		Gap(RightColumn);
		Headline(RightColumn, Localize("Presence server"));
		CheckBox(RightColumn, &g_Config.m_FogPresence, Localize("Share your name style with Fog players"));
		CheckBox(RightColumn, &g_Config.m_FogPresenceShareLook, Localize("Share your tee studio look (aura, accessories, animation)"));
		CheckBox(RightColumn, &g_Config.m_FogShowOthersLooks, Localize("Show other Fog players' looks"));
		{
			CUIRect Rect = Row(RightColumn, LINE);
			CUIRect Label, Box;
			Rect.VSplitLeft(110.0f, &Label, &Box);
			Ui()->DoLabel(&Label, Localize("Server address"), FONT, TEXTALIGN_ML);
			static CLineInput s_UrlInput(g_Config.m_FogPresenceUrl, sizeof(g_Config.m_FogPresenceUrl));
			s_UrlInput.SetEmptyText("https://fog.example.org");
			Ui()->DoClearableEditBox(&s_UrlInput, &Box, 12.0f);
		}
		{
			char aStatus[256];
			if(!g_Config.m_FogPresenceUrl[0])
				str_copy(aStatus, Localize("No presence server set: names are plain and nothing is sent."));
			else if(Client()->State() != IClient::STATE_ONLINE)
				str_copy(aStatus, Localize("Join a server to see other Fog players."));
			else
				str_format(aStatus, sizeof(aStatus), Localize("Sends your name, this server's address and your style. Fog players here: %d"), GameClient()->m_FogPresence.NumPlayers());
			CUIRect Rect = Row(RightColumn, LINE * 2.0f);
			SLabelProperties Props;
			Props.m_MaxWidth = Rect.w;
			TextRender()->TextColor(ColorRGBA(0.70f, 0.74f, 0.80f, 1.0f));
			Ui()->DoLabel(&Rect, aStatus, 11.0f, TEXTALIGN_TL, Props);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
	}

	s_ScrollRegion.End();
}

// The Fog tab: everything that belongs to the client, as sections of one tab
void CMenus::RenderSettingsFog(CUIRect MainView)
{
	CUIRect Tabs;
	MainView.HSplitTop(24.0f, &Tabs, &MainView);
	MainView.HSplitTop(12.0f, nullptr, &MainView);
	const char *apTabs[] = {Localize("Music and lyrics", "Fog tab"), Localize("Shaders"), Localize("Graphics"), Localize("World"), Localize("Trails"), Localize("Players"), Localize("Interface", "Fog tab"), Localize("Gameplay", "Fog tab"), Localize("Fun")};
	static CButtonContainer s_aTabs[std::size(apTabs)];
	m_FogVisualsTab = std::clamp(m_FogVisualsTab, 0, (int)std::size(apTabs) - 1);
	Graphics()->TextureClear();
	Graphics()->DrawRect(Tabs.x, Tabs.y, Tabs.w, Tabs.h, ColorRGBA(0.0f, 0.01f, 0.03f, 0.22f), IGraphics::CORNER_ALL, Tabs.h / 2.0f);
	const float w = (Tabs.w - 4.0f) / std::size(apTabs);
	const float Target = Tabs.x + 2.0f + m_FogVisualsTab * w;
	if(m_FogVisualsTabX < 0.0f)
		m_FogVisualsTabX = Target;
	FogSpring(m_FogVisualsTabX, m_FogVisualsTabV, Target, std::min(Client()->RenderFrameTime(), 0.1f), 4.5f, 0.45f);
	const ColorRGBA PillTop(1.0f, 1.0f, 1.0f, 0.17f), PillBottom(1.0f, 1.0f, 1.0f, 0.10f);
	Graphics()->DrawRect4(m_FogVisualsTabX, Tabs.y + 2.0f, w, Tabs.h - 4.0f, PillTop, PillTop, PillBottom, PillBottom, IGraphics::CORNER_ALL, (Tabs.h - 4.0f) / 2.0f);
	for(int i = 0; i < (int)std::size(apTabs); i++)
	{
		const CUIRect R = {Tabs.x + 2.0f + i * w, Tabs.y + 2.0f, w, Tabs.h - 4.0f};
		if(Ui()->DoButtonLogic(&s_aTabs[i], 0, &R, BUTTONFLAG_LEFT))
			m_FogVisualsTab = i;
		const bool Active = m_FogVisualsTab == i, Hot = Ui()->HotItem() == &s_aTabs[i];
		FogLabel(Ui(), TextRender(), R, apTabs[i], 11.5f, TEXTALIGN_MC, Active ? ColorRGBA(0.91f, 0.93f, 0.95f, 1.0f) : (Hot ? ColorRGBA(0.82f, 0.85f, 0.89f, 1.0f) : ColorRGBA(0.64f, 0.68f, 0.74f, 1.0f)), "Manrope", Active ? 700 : 600);
	}

	switch(m_FogVisualsTab)
	{
	case 0: RenderSettingsFogMusic(MainView); break;
	case 1: RenderSettingsShaders(MainView); break;
	case 2: RenderSettingsFogVisuals(MainView, 6); break;
	case 3: RenderSettingsFogVisuals(MainView, 0); break;
	case 4: RenderSettingsFogVisuals(MainView, 1); break;
	case 5: RenderSettingsFogVisuals(MainView, 2); break;
	case 6: RenderSettingsFogVisuals(MainView, 3); break;
	case 7: RenderSettingsFogVisuals(MainView, 4); break;
	default: RenderSettingsFogVisuals(MainView, 5); break;
	}
}
