// Fog client: the navigation rail that frames every offline menu page (servers, demos,
// settings, news). It replaces the stock top tab bar while the Fog interface is enabled.

#include "brand.h"
#include "glass.h"

#include <base/dbg.h>
#include <base/math.h>

#include <engine/font_icons.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr ColorRGBA FROST(0.91f, 0.93f, 0.95f, 1.0f);
constexpr ColorRGBA MIST(0.64f, 0.68f, 0.74f, 1.0f);
constexpr ColorRGBA DIM(0.46f, 0.50f, 0.56f, 1.0f);

void RailIcon(ITextRender *pTextRender, CUi *pUi, const CUIRect &Rect, const char *pIcon, float Size, ColorRGBA Color)
{
	pTextRender->SetFontPreset(EFontPreset::ICON_FONT);
	pTextRender->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
	pTextRender->TextColor(Color);
	pUi->DoLabel(&Rect, pIcon, Size, TEXTALIGN_MC);
	pTextRender->TextColor(pTextRender->DefaultTextColor());
	pTextRender->SetRenderFlags(0);
	pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
}

} // namespace

void CMenus::RenderFogPage(CUIRect Screen)
{
	CUIRect Rail, Content;
	Screen.VSplitLeft(200.0f, &Rail, &Content);
	Content.VSplitLeft(14.0f, nullptr, &Content);

	// the page sits on one large sheet of glass
	FogDrawGlass(Graphics(), Content, 20.0f, 1.0f);
	CUIRect Page;
	Content.Margin(10.0f, &Page);
	if(m_MenuPage == PAGE_NEWS)
		RenderNews(Page);
	else if(m_MenuPage >= PAGE_INTERNET && m_MenuPage <= PAGE_FAVORITE_COMMUNITY_5)
		g_Config.m_FogUi && g_Config.m_FogUiMenus ? RenderFogServerbrowser(Page) : RenderServerbrowser(Page);
	else if(m_MenuPage == PAGE_DEMOS)
		RenderDemoBrowser(Page);
	else if(m_MenuPage == PAGE_FOG_CLIPS)
		RenderFogClips(Page);
	else if(m_MenuPage == PAGE_SETTINGS)
		RenderSettings(Page);
	else
		dbg_assert_failed("Invalid m_MenuPage: %d", m_MenuPage);

	RenderFogRail(Rail);
}

void CMenus::RenderFogRail(CUIRect Rail)
{
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	const float Speed = g_Config.m_FogAnim ? g_Config.m_FogAnimSpeed / 100.0f : 1000.0f;
	FogDrawGlass(Graphics(), Rail, 20.0f, 1.0f);

	CUIRect Content, Wordmark;
	Rail.Margin(vec2(14.0f, 16.0f), &Content);
	Content.HSplitTop(34.0f, &Wordmark, &Content);
	FogBrand::DrawLockup(Graphics(), TextRender(), Ui(), Wordmark, 28.0f);
	Content.HSplitTop(18.0f, nullptr, &Content);

	struct SItem
	{
		const void *m_pId;
		const char *m_pIcon;
		const char *m_pLabel;
		int m_Page; // -1: action
	};
	static CButtonContainer s_Internet, s_Lan, s_Favorites, s_aCommunities[5], s_Demos, s_Clips, s_Settings, s_News, s_Home, s_Quit;
	std::vector<SItem> vServers = {
		{&s_Internet, FontIcon::EARTH_AMERICAS, Localize("Internet"), PAGE_INTERNET},
		{&s_Lan, FontIcon::NETWORK_WIRED, Localize("LAN"), PAGE_LAN},
		{&s_Favorites, FontIcon::STAR, Localize("Favorites"), PAGE_FAVORITES},
	};
	const std::vector<const CCommunity *> vpCommunities = ServerBrowser()->FavoriteCommunities();
	for(size_t i = 0; i < vpCommunities.size() && i < std::size(s_aCommunities); i++)
		vServers.push_back({&s_aCommunities[i], FontIcon::ELLIPSIS, vpCommunities[i]->Name(), PAGE_FAVORITE_COMMUNITY_1 + (int)i});
	const SItem aMore[] = {
		{&s_Demos, FontIcon::FILM, Localize("Demos"), PAGE_DEMOS},
		{&s_Clips, FontIcon::VIDEO, Localize("Clips"), PAGE_FOG_CLIPS},
		{&s_Settings, FontIcon::GEAR, Localize("Settings"), PAGE_SETTINGS},
		{&s_News, FontIcon::NEWSPAPER, Localize("News"), PAGE_NEWS},
	};
	const SItem aBottom[] = {
		{&s_Home, FontIcon::HOUSE, Localize("Main menu"), -1},
		{&s_Quit, FontIcon::POWER_OFF, Localize("Quit"), -2},
	};

	const float ItemH = 32.0f, Gap = 3.0f;
	int NewPage = -1;
	float HoverY = -1.0f, ActiveY = -1.0f;
	const auto Section = [&](CUIRect &Column, const char *pTitle) {
		CUIRect Title;
		Column.HSplitTop(18.0f, &Title, &Column);
		FogLabel(Ui(), TextRender(), Title, pTitle, 10.0f, TEXTALIGN_ML, DIM, "Manrope", 700);
		Column.HSplitTop(4.0f, nullptr, &Column);
	};
	const auto DoItem = [&](CUIRect &Column, const SItem &Item) {
		CUIRect Row;
		Column.HSplitTop(ItemH, &Row, &Column);
		Column.HSplitTop(Gap, nullptr, &Column);
		const bool Active = Item.m_Page >= 0 && m_MenuPage == Item.m_Page;
		if(Ui()->DoButtonLogic(Item.m_pId, Active, &Row, BUTTONFLAG_LEFT))
		{
			if(Item.m_Page >= 0)
				NewPage = Item.m_Page;
			else if(Item.m_Page == -1)
				m_ShowStart = true;
			else if(GameClient()->Editor()->HasUnsavedData() || (GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0))
				m_Popup = POPUP_QUIT;
			else
				Client()->Quit();
		}
		const bool Hot = Ui()->HotItem() == Item.m_pId;
		if(Hot)
			HoverY = Row.y;
		if(Active)
			ActiveY = Row.y;
		CUIRect Icon, Label;
		Row.VSplitLeft(30.0f, &Icon, &Label);
		RailIcon(TextRender(), Ui(), Icon, Item.m_pIcon, 12.0f, Active || Hot ? FROST : MIST);
		FogLabel(Ui(), TextRender(), Label, Item.m_pLabel, 13.5f, TEXTALIGN_ML, Active || Hot ? FROST : ColorRGBA(0.80f, 0.83f, 0.87f, 1.0f), "Manrope", Active ? 700 : 600);
	};

	// pills are drawn from last frame's positions so they sit behind this frame's labels
	const auto Pill = [&](float Y, float Alpha, bool Accent) {
		if(Y < 0.0f || Alpha <= 0.01f)
			return;
		const CUIRect R = {Content.x - 6.0f, Y, Content.w + 12.0f, ItemH};
		const ColorRGBA Top(1, 1, 1, 0.10f * Alpha), Bottom(1, 1, 1, 0.05f * Alpha);
		Graphics()->TextureClear();
		Graphics()->DrawRect4(R.x, R.y, R.w, R.h, Top, Top, Bottom, Bottom, IGraphics::CORNER_ALL, 10.0f);
		if(Accent)
			Graphics()->DrawRect(R.x + 2.0f, R.y + 8.0f, 3.0f, R.h - 16.0f, FROST.WithAlpha(0.9f * Alpha), IGraphics::CORNER_ALL, 1.5f);
	};
	Pill(m_FogRailActiveY, 1.0f, true);
	Pill(m_FogRailHoverY, m_FogRailHoverAlpha * 0.7f, false);

	CUIRect Bottom;
	Content.HSplitBottom(ItemH * 2 + Gap, &Content, &Bottom);
	Section(Content, Localize("Servers"));
	for(const SItem &Item : vServers)
		DoItem(Content, Item);
	Content.HSplitTop(12.0f, nullptr, &Content);
	Section(Content, Localize("More"));
	for(const SItem &Item : aMore)
		DoItem(Content, Item);
	for(const SItem &Item : aBottom)
		DoItem(Bottom, Item);

	const float Follow = 1.0f - std::exp(-Dt * 18.0f * std::min(Speed, 20.0f));
	// the pills travel on springs: they overshoot a touch and settle
	if(ActiveY < 0.0f)
		m_FogRailActiveY = -1.0f;
	else if(m_FogRailActiveY < 0.0f)
		m_FogRailActiveY = ActiveY, m_FogRailActiveV = 0.0f;
	else
		FogSpring(m_FogRailActiveY, m_FogRailActiveV, ActiveY, Dt, 4.5f, 0.45f);
	if(HoverY >= 0.0f)
	{
		if(m_FogRailHoverY < 0.0f || m_FogRailHoverAlpha < 0.02f)
			m_FogRailHoverY = HoverY, m_FogRailHoverV = 0.0f;
		else
			FogSpring(m_FogRailHoverY, m_FogRailHoverV, HoverY, Dt, 6.0f, 0.40f);
		m_FogRailHoverAlpha = mix(m_FogRailHoverAlpha, 1.0f, Follow);
	}
	else
		m_FogRailHoverAlpha = mix(m_FogRailHoverAlpha, 0.0f, Follow);

	// arrow keys still walk through the server tabs
	const int MaxPage = PAGE_FAVORITES + (int)std::min<size_t>(vpCommunities.size(), 5);
	if(!Ui()->IsPopupOpen() && CLineInput::GetActiveInput() == nullptr && m_MenuPage >= PAGE_INTERNET && m_MenuPage <= MaxPage)
	{
		if(Input()->KeyPress(KEY_RIGHT))
			NewPage = m_MenuPage + 1 > MaxPage ? PAGE_INTERNET : m_MenuPage + 1;
		if(Input()->KeyPress(KEY_LEFT))
			NewPage = m_MenuPage - 1 < PAGE_INTERNET ? MaxPage : m_MenuPage - 1;
	}
	if(NewPage != -1)
		SetMenuPage(NewPage);
}

// Settings frame: the stock vertical tab strip becomes a grouped section list on the left
// of the page, and every section gets a header with its name and what lives there.
void CMenus::RenderFogSettingsNav(CUIRect &MainView, CUIRect &RestartBar, bool NeedRestart)
{
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	const float Speed = g_Config.m_FogAnim ? g_Config.m_FogAnimSpeed / 100.0f : 1000.0f;
	const float Follow = 1.0f - std::exp(-Dt * 18.0f * std::min(Speed, 20.0f));

	struct SEntry
	{
		int m_Page;
		const char *m_pIcon; // nullptr: the fog mark
		const char *m_pLabel;
		const char *m_pHint;
	};
	const SEntry aEntries[SETTINGS_LENGTH] = {
		{SETTINGS_LANGUAGE, FontIcon::EARTH_AMERICAS, Localize("Language"), Localize("The language of menus, chat hints and messages.")},
		{SETTINGS_GENERAL, FontIcon::GEAR, Localize("General"), Localize("Demos, screenshots, updates and how the client behaves.")},
		{SETTINGS_PLAYER, FontIcon::USER, Localize("Player"), Localize("Your name, clan and flag, for you and your dummy.")},
		{SETTINGS_TEE, FontIcon::CIRCLE, Client()->IsSixup() ? "Tee 0.7" : Localize("Tee"), Localize("Skin, body and feet colours, eyes.")},
		{SETTINGS_APPEARANCE, FontIcon::EYE, Localize("Appearance"), Localize("HUD, chat, name plates, hook and laser colours.")},
		{SETTINGS_CONTROLS, FontIcon::KEYBOARD, Localize("Controls"), Localize("Key bindings, mouse and joystick.")},
		{SETTINGS_GRAPHICS, FontIcon::IMAGE, Localize("Graphics"), Localize("Screen, renderer, frame rate and quality.")},
		{SETTINGS_SOUND, FontIcon::MUSIC, Localize("Sound"), Localize("Volume, sound device and game sounds.")},
		{SETTINGS_DDNET, FontIcon::FLAG_CHECKERED, Localize("DDNet"), Localize("Race, ghost, dummy, background and other DDNet options.")},
		{SETTINGS_FOG, nullptr, "Fog", Localize("Everything of this client: music and lyrics, shaders, the world, trails, players, interface.")},
		{SETTINGS_SHADERS, FontIcon::LAYER_GROUP, Localize("Visuals"), Localize("Shaders, map fog and colours, live backgrounds, trails, player plates.")},
		{SETTINGS_ASSETS, FontIcon::FOLDER, Localize("Assets"), Localize("Entities, game, emoticons, particles and HUD skins.")},
		{SETTINGS_CREDITS, FontIcon::HEART, Localize("Credits"), Localize("The people who made DDNet.")},
	};
	struct SGroup
	{
		const char *m_pTitle;
		std::vector<int> m_vPages;
	};
	const SGroup aGroups[] = {
		{Localize("You"), {SETTINGS_PLAYER, SETTINGS_TEE}},
		{Localize("Game"), {SETTINGS_GENERAL, SETTINGS_CONTROLS, SETTINGS_DDNET}},
		{Localize("Look and sound"), {SETTINGS_FOG, SETTINGS_APPEARANCE, SETTINGS_GRAPHICS, SETTINGS_SOUND, SETTINGS_ASSETS}},
		{Localize("Other"), {SETTINGS_LANGUAGE, SETTINGS_CREDITS}},
	};

	CUIRect Nav;
	MainView.VSplitLeft(184.0f, &Nav, &MainView);
	// the section list sits on a slightly deeper well of the page glass
	Graphics()->TextureClear();
	Graphics()->DrawRect(Nav.x, Nav.y, Nav.w, Nav.h, ColorRGBA(0.0f, 0.01f, 0.03f, 0.16f), IGraphics::CORNER_ALL, 14.0f);
	Graphics()->DrawRect(Nav.x + Nav.w + 10.0f, Nav.y + 16.0f, 1.0f, Nav.h - 32.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.06f), IGraphics::CORNER_NONE, 0.0f);
	MainView.VSplitLeft(24.0f, nullptr, &MainView);
	MainView.VSplitRight(8.0f, &MainView, nullptr);

	CUIRect Column;
	Nav.Margin(vec2(10.0f, 14.0f), &Column);
	const float ItemH = 28.0f, Gap = 2.0f;

	const auto Pill = [&](float Y, float Alpha, bool Accent) {
		if(Y < 0.0f || Alpha <= 0.01f)
			return;
		const CUIRect R = {Column.x - 4.0f, Y, Column.w + 8.0f, ItemH};
		const ColorRGBA Top(1, 1, 1, 0.10f * Alpha), Bottom(1, 1, 1, 0.05f * Alpha);
		Graphics()->TextureClear();
		Graphics()->DrawRect4(R.x, R.y, R.w, R.h, Top, Top, Bottom, Bottom, IGraphics::CORNER_ALL, 9.0f);
		if(Accent)
			Graphics()->DrawRect(R.x + 2.0f, R.y + 7.0f, 3.0f, R.h - 14.0f, FROST.WithAlpha(0.9f * Alpha), IGraphics::CORNER_ALL, 1.5f);
	};
	Pill(m_FogSettingsActiveY, 1.0f, true);
	Pill(m_FogSettingsHoverY, m_FogSettingsHoverAlpha * 0.7f, false);

	static CButtonContainer s_aButtons[SETTINGS_LENGTH];
	float HoverY = -1.0f, ActiveY = -1.0f;
	for(const SGroup &Group : aGroups)
	{
		CUIRect Title;
		Column.HSplitTop(18.0f, &Title, &Column);
		Title.VSplitLeft(4.0f, nullptr, &Title);
		FogLabel(Ui(), TextRender(), Title, Group.m_pTitle, 10.0f, TEXTALIGN_ML, DIM, "Manrope", 700);
		Column.HSplitTop(3.0f, nullptr, &Column);
		for(int Page : Group.m_vPages)
		{
			const SEntry &Entry = aEntries[Page];
			CUIRect Row;
			Column.HSplitTop(ItemH, &Row, &Column);
			Column.HSplitTop(Gap, nullptr, &Column);
			const bool Active = g_Config.m_UiSettingsPage == Page;
			if(Ui()->DoButtonLogic(&s_aButtons[Page], Active, &Row, BUTTONFLAG_LEFT))
				g_Config.m_UiSettingsPage = Page;
			const bool Hot = Ui()->HotItem() == &s_aButtons[Page];
			if(Hot)
				HoverY = Row.y;
			if(Active)
				ActiveY = Row.y;
			CUIRect Icon, Label;
			Row.VSplitLeft(28.0f, &Icon, &Label);
			const ColorRGBA IconColor = Active || Hot ? FROST : MIST;
			if(Entry.m_pIcon)
				RailIcon(TextRender(), Ui(), Icon, Entry.m_pIcon, 11.0f, IconColor);
			else
				FogBrand::DrawMark(Graphics(), Icon.Center(), 17.0f, IconColor, Active ? 0.6f : 0.0f);
			FogLabel(Ui(), TextRender(), Label, Entry.m_pLabel, 13.0f, TEXTALIGN_ML, Active || Hot ? FROST : ColorRGBA(0.80f, 0.83f, 0.87f, 1.0f), "Manrope", Active ? 700 : 600);
		}
		Column.HSplitTop(10.0f, nullptr, &Column);
	}

	if(ActiveY < 0.0f)
		m_FogSettingsActiveY = -1.0f;
	else if(m_FogSettingsActiveY < 0.0f)
		m_FogSettingsActiveY = ActiveY, m_FogSettingsActiveV = 0.0f;
	else
		FogSpring(m_FogSettingsActiveY, m_FogSettingsActiveV, ActiveY, Dt, 4.5f, 0.45f);
	if(HoverY >= 0.0f)
	{
		if(m_FogSettingsHoverY < 0.0f || m_FogSettingsHoverAlpha < 0.02f)
			m_FogSettingsHoverY = HoverY, m_FogSettingsHoverV = 0.0f;
		else
			FogSpring(m_FogSettingsHoverY, m_FogSettingsHoverV, HoverY, Dt, 6.0f, 0.40f);
		m_FogSettingsHoverAlpha = mix(m_FogSettingsHoverAlpha, 1.0f, Follow);
	}
	else
		m_FogSettingsHoverAlpha = mix(m_FogSettingsHoverAlpha, 0.0f, Follow);

	// header: the section name in the display face and one line on what it holds;
	// it settles in from a few pixels below whenever the section changes
	if(g_Config.m_UiSettingsPage == SETTINGS_SHADERS)
		g_Config.m_UiSettingsPage = SETTINGS_FOG; // everything Fog lives inside the Fog tab now
	const int Page = std::clamp(g_Config.m_UiSettingsPage, 0, (int)SETTINGS_LENGTH - 1);
	if(Page != m_FogSettingsLastPage)
	{
		m_FogSettingsLastPage = Page;
		m_FogSettingsHeaderIn = g_Config.m_FogAnim ? 0.0f : 1.0f;
	}
	m_FogSettingsHeaderIn = std::min(1.0f, m_FogSettingsHeaderIn + Dt * 3.0f * std::min(Speed, 20.0f));
	const float In = FogEaseOutBack(m_FogSettingsHeaderIn);
	const float Fade = std::min(1.0f, m_FogSettingsHeaderIn * 2.0f);

	CUIRect Header, Title, Hint;
	MainView.HSplitTop(62.0f, &Header, &MainView);
	Header.HSplitTop(8.0f, nullptr, &Header);
	Header.HSplitTop(32.0f, &Title, &Hint);
	Hint.HSplitTop(18.0f, &Hint, nullptr);
	Title.y += (1.0f - In) * 10.0f;
	Hint.y += (1.0f - FogEaseOutBack(std::max(0.0f, m_FogSettingsHeaderIn * 1.25f - 0.25f))) * 14.0f;
	FogLabel(Ui(), TextRender(), Title, aEntries[Page].m_pLabel, 25.0f, TEXTALIGN_ML, FROST.WithAlpha(Fade), "Manrope", 800);
	FogLabel(Ui(), TextRender(), Hint, aEntries[Page].m_pHint, 12.5f, TEXTALIGN_ML, MIST.WithAlpha(0.9f * Fade * Fade), "Manrope", 500);
	MainView.HSplitTop(8.0f, nullptr, &MainView);

	if(NeedRestart)
	{
		MainView.HSplitBottom(34.0f, &MainView, &RestartBar);
		MainView.HSplitBottom(10.0f, &MainView, nullptr);
		const ColorRGBA Warm(0.86f, 0.72f, 0.58f, 0.10f);
		Graphics()->TextureClear();
		Graphics()->DrawRect(RestartBar.x, RestartBar.y, RestartBar.w, RestartBar.h, Warm, IGraphics::CORNER_ALL, 12.0f);
		Graphics()->DrawRect(RestartBar.x + 6.0f, RestartBar.y + 9.0f, 3.0f, RestartBar.h - 18.0f, ColorRGBA(0.92f, 0.80f, 0.66f, 0.9f), IGraphics::CORNER_ALL, 1.5f);
		RestartBar.VMargin(16.0f, &RestartBar);
		RestartBar.HMargin(5.0f, &RestartBar);
		RestartBar.VSplitRight(4.0f, &RestartBar, nullptr);
	}
}
