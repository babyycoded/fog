// Fog client: the in-game (Esc) menu. A glass rail on the left, the game stays visible.
// The first page is a pause card with large actions; the other pages sit on a glass sheet.

#include "brand.h"
#include "glass.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/demo.h>
#include <engine/font_icons.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr ColorRGBA FROST(0.91f, 0.93f, 0.95f, 1.0f);
constexpr ColorRGBA MIST(0.64f, 0.68f, 0.74f, 1.0f);
constexpr ColorRGBA DIM(0.46f, 0.50f, 0.56f, 1.0f);
constexpr ColorRGBA ROSE(0.92f, 0.60f, 0.64f, 1.0f);

void Icon(ITextRender *pTextRender, CUi *pUi, const CUIRect &Rect, const char *pIcon, float Size, ColorRGBA Color)
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

void CMenus::RenderFogIngame(CUIRect Screen)
{
	const float Time = Client()->GlobalTime();
	// the menu "opens" again whenever it was not drawn for a moment
	if(Time - m_FogIngameLastRender > 0.25f)
		m_FogIngameOpenTime = Time;
	m_FogIngameLastRender = Time;

	CUIRect Rail, Content;
	Screen.VSplitLeft(200.0f, &Rail, &Content);
	Content.VSplitLeft(14.0f, nullptr, &Content);

	if(m_GamePage == PAGE_GAME)
		RenderFogIngameHome(Content);
	else
	{
		FogDrawGlass(Graphics(), Content, 20.0f, 1.0f);
		CUIRect Page;
		Content.Margin(10.0f, &Page);
		if(m_GamePage == PAGE_PLAYERS)
			RenderPlayers(Page);
		else if(m_GamePage == PAGE_SERVER_INFO)
			RenderServerInfo(Page);
		else if(m_GamePage == PAGE_NETWORK)
		{
			// Internet, LAN and favourites as a small segmented control above the browser
			CUIRect Tabs;
			Page.HSplitTop(22.0f, &Tabs, &Page);
			Page.HSplitTop(6.0f, nullptr, &Page);
			Tabs.VSplitLeft(std::min(300.0f, Tabs.w), &Tabs, nullptr);
			const int aPages[] = {PAGE_INTERNET, PAGE_LAN, PAGE_FAVORITES};
			const char *apLabels[] = {Localize("Internet"), Localize("LAN"), Localize("Favorites")};
			static CButtonContainer s_aTabs[3];
			Graphics()->TextureClear();
			Graphics()->DrawRect(Tabs.x, Tabs.y, Tabs.w, Tabs.h, ColorRGBA(0.0f, 0.01f, 0.03f, 0.22f), IGraphics::CORNER_ALL, Tabs.h / 2.0f);
			const float w = (Tabs.w - 4.0f) / 3.0f;
			for(int i = 0; i < 3; i++)
			{
				const CUIRect R = {Tabs.x + 2.0f + i * w, Tabs.y + 2.0f, w, Tabs.h - 4.0f};
				const bool Active = g_Config.m_UiPage == aPages[i];
				if(Active)
					Graphics()->DrawRect(R.x, R.y, R.w, R.h, ColorRGBA(1.0f, 1.0f, 1.0f, 0.15f), IGraphics::CORNER_ALL, R.h / 2.0f);
				if(Ui()->DoButtonLogic(&s_aTabs[i], 0, &R, BUTTONFLAG_LEFT) && !Active)
				{
					g_Config.m_UiPage = aPages[i];
					RefreshBrowserTab(false);
				}
				FogLabel(Ui(), TextRender(), R, apLabels[i], 10.0f, TEXTALIGN_MC, Active ? FROST : MIST, "Manrope", Active ? 700 : 600);
			}
			if(g_Config.m_UiPage < PAGE_INTERNET || g_Config.m_UiPage > PAGE_FAVORITE_COMMUNITY_5)
				g_Config.m_UiPage = PAGE_INTERNET;
			RenderFogServerbrowser(Page);
		}
		else if(m_GamePage == PAGE_GHOST)
			RenderGhost(Page);
		else if(m_GamePage == PAGE_CALLVOTE)
			RenderServerControl(Page);
		else if(m_GamePage == PAGE_DEMOS)
			RenderDemoBrowser(Page);
		else if(m_GamePage == PAGE_SETTINGS)
			RenderSettings(Page);
		else
			m_GamePage = PAGE_GAME;
	}

	RenderFogIngameRail(Rail);
}

void CMenus::RenderFogIngameRail(CUIRect Rail)
{
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	const float Speed = g_Config.m_FogAnim ? std::clamp(g_Config.m_FogAnimSpeed / 100.0f, 0.1f, 10.0f) : 1000.0f;
	const float Follow = 1.0f - std::exp(-Dt * 18.0f * std::min(Speed, 20.0f));
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
		int m_Page; // -1 disconnect
	};
	static CButtonContainer s_Game, s_Players, s_Server, s_Browser, s_Ghost, s_Vote, s_Demos, s_Settings, s_Disconnect;
	std::vector<SItem> vPlay = {
		{&s_Game, FontIcon::PLAY, Localize("Game"), PAGE_GAME},
		{&s_Players, FontIcon::USER, Localize("Players"), PAGE_PLAYERS},
		{&s_Server, FontIcon::INFO, Localize("Server info"), PAGE_SERVER_INFO},
		{&s_Vote, FontIcon::LIST_UL, Localize("Call vote"), PAGE_CALLVOTE},
	};
	if(GameClient()->m_GameInfo.m_Race)
		vPlay.push_back({&s_Ghost, FontIcon::EYE, Localize("Ghost"), PAGE_GHOST});
	const SItem aMore[] = {
		{&s_Browser, FontIcon::EARTH_AMERICAS, Localize("Browser"), PAGE_NETWORK},
		{&s_Demos, FontIcon::FILM, Localize("Demos"), PAGE_DEMOS},
		{&s_Settings, FontIcon::GEAR, Localize("Settings"), PAGE_SETTINGS},
	};

	const float ItemH = 32.0f, Gap = 3.0f;
	float HoverY = -1.0f, ActiveY = -1.0f;
	int NewPage = -1;
	const auto Pill = [&](float Y, float Alpha, bool Accent) {
		if(Y < 0.0f || Alpha <= 0.01f)
			return;
		const ColorRGBA Top(1, 1, 1, 0.10f * Alpha), Bottom(1, 1, 1, 0.05f * Alpha);
		Graphics()->TextureClear();
		Graphics()->DrawRect4(Content.x - 6.0f, Y, Content.w + 12.0f, ItemH, Top, Top, Bottom, Bottom, IGraphics::CORNER_ALL, 10.0f);
		if(Accent)
			Graphics()->DrawRect(Content.x - 4.0f, Y + 8.0f, 3.0f, ItemH - 16.0f, FROST.WithAlpha(0.9f * Alpha), IGraphics::CORNER_ALL, 1.5f);
	};
	Pill(m_FogIngameRailY, 1.0f, true);
	Pill(m_FogIngameHoverY, m_FogIngameHoverAlpha * 0.7f, false);

	const auto Section = [&](CUIRect &Column, const char *pTitle) {
		CUIRect Title;
		Column.HSplitTop(18.0f, &Title, &Column);
		FogLabel(Ui(), TextRender(), Title, pTitle, 10.0f, TEXTALIGN_ML, DIM, "Manrope", 700);
		Column.HSplitTop(4.0f, nullptr, &Column);
	};
	const auto DoItem = [&](CUIRect &Column, const SItem &Item, ColorRGBA Tint) {
		CUIRect Row;
		Column.HSplitTop(ItemH, &Row, &Column);
		Column.HSplitTop(Gap, nullptr, &Column);
		const bool Active = Item.m_Page >= 0 && m_GamePage == Item.m_Page;
		if(Ui()->DoButtonLogic(Item.m_pId, Active, &Row, BUTTONFLAG_LEFT))
			NewPage = Item.m_Page >= 0 ? Item.m_Page : -2;
		const bool Hot = Ui()->HotItem() == Item.m_pId;
		if(Hot)
			HoverY = Row.y;
		if(Active)
			ActiveY = Row.y;
		CUIRect IconRect, Label;
		Row.VSplitLeft(30.0f, &IconRect, &Label);
		Icon(TextRender(), Ui(), IconRect, Item.m_pIcon, 12.0f, Active || Hot ? Tint : Tint.WithMultipliedAlpha(0.75f));
		FogLabel(Ui(), TextRender(), Label, Item.m_pLabel, 13.5f, TEXTALIGN_ML, Active || Hot ? Tint : ColorRGBA(Tint.r * 0.88f, Tint.g * 0.89f, Tint.b * 0.91f, 1.0f), "Manrope", Active ? 700 : 600);
	};

	CUIRect Bottom;
	Content.HSplitBottom(ItemH, &Content, &Bottom);
	Section(Content, Localize("Play"));
	for(const SItem &Item : vPlay)
		DoItem(Content, Item, FROST);
	Content.HSplitTop(12.0f, nullptr, &Content);
	Section(Content, Localize("More"));
	for(const SItem &Item : aMore)
		DoItem(Content, Item, FROST);
	DoItem(Bottom, {&s_Disconnect, FontIcon::RIGHT_FROM_BRACKET, Localize("Disconnect"), -1}, ROSE);

	if(ActiveY < 0.0f)
		m_FogIngameRailY = -1.0f;
	else if(m_FogIngameRailY < 0.0f)
		m_FogIngameRailY = ActiveY, m_FogIngameRailV = 0.0f;
	else
		FogSpring(m_FogIngameRailY, m_FogIngameRailV, ActiveY, Dt, 4.5f, 0.45f);
	if(HoverY >= 0.0f)
	{
		if(m_FogIngameHoverY < 0.0f || m_FogIngameHoverAlpha < 0.02f)
			m_FogIngameHoverY = HoverY, m_FogIngameHoverV = 0.0f;
		else
			FogSpring(m_FogIngameHoverY, m_FogIngameHoverV, HoverY, Dt, 6.0f, 0.40f);
		m_FogIngameHoverAlpha = mix(m_FogIngameHoverAlpha, 1.0f, Follow);
	}
	else
		m_FogIngameHoverAlpha = mix(m_FogIngameHoverAlpha, 0.0f, Follow);

	if(NewPage == -2)
		FogIngameDisconnect();
	else if(NewPage >= 0)
	{
		if(NewPage == PAGE_CALLVOTE && m_GamePage != PAGE_CALLVOTE)
			m_ControlPageOpening = true;
		m_GamePage = NewPage;
	}
}

void CMenus::FogIngameDisconnect()
{
	if((GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmDisconnectTime && g_Config.m_ClConfirmDisconnectTime >= 0) ||
		GameClient()->m_TouchControls.HasEditingChanges() || GameClient()->m_Menus.m_MenusIngameTouchControls.UnsavedChanges())
	{
		char aBuf[256] = {'\0'};
		if(GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmDisconnectTime && g_Config.m_ClConfirmDisconnectTime >= 0)
			str_copy(aBuf, Localize("Are you sure that you want to disconnect?"));
		if(GameClient()->m_TouchControls.HasEditingChanges() || GameClient()->m_Menus.m_MenusIngameTouchControls.UnsavedChanges())
		{
			if(aBuf[0] != '\0')
				str_append(aBuf, "\n\n");
			str_append(aBuf, Localize("There's an unsaved change in the touch controls editor, you might want to save it."));
		}
		PopupConfirm(Localize("Disconnect"), aBuf, Localize("Yes"), Localize("No"), &CMenus::PopupConfirmDisconnect);
	}
	else
	{
		Client()->Disconnect();
		RefreshBrowserTab(true);
	}
}

// The pause card: where you are, how long you have been at it, and the things you
// usually open this menu for, as large tiles.
void CMenus::RenderFogIngameHome(CUIRect Content)
{
	const float Time = Client()->GlobalTime();
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	const float Speed = g_Config.m_FogAnim ? std::clamp(g_Config.m_FogAnimSpeed / 100.0f, 0.1f, 10.0f) : 1000.0f;
	const auto Stage = [&](float Delay) {
		return std::clamp(((Time - m_FogIngameOpenTime) * Speed - Delay) * 3.5f, 0.0f, 1.0f);
	};

	const float CardW = std::min(700.0f, Content.w * 0.78f);
	CUIRect Card = {Content.x, Content.y, CardW, 0.0f};
	const float CardH = 312.0f;
	Card.h = CardH;
	Card.y = Content.y + std::max(0.0f, (Content.h - CardH) * 0.42f);
	const float CardIn = Stage(0.0f);
	Card.x -= (1.0f - FogEaseOutBack(CardIn)) * 24.0f;
	FogDrawGlass(Graphics(), Card, 22.0f, std::min(CardIn * 1.6f, 1.0f));

	CUIRect Inner;
	Card.Margin(vec2(22.0f, 18.0f), &Inner);

	// where you are
	const CServerInfo &Info = Client()->ServerInfo();
	CUIRect Eyebrow, Map, Server, Chips;
	Inner.HSplitTop(14.0f, &Eyebrow, &Inner);
	Inner.HSplitTop(36.0f, &Map, &Inner);
	Inner.HSplitTop(16.0f, &Server, &Inner);
	Inner.HSplitTop(10.0f, nullptr, &Inner);
	Inner.HSplitTop(18.0f, &Chips, &Inner);
	Inner.HSplitTop(16.0f, nullptr, &Inner);
	{
		const float a = Stage(0.05f);
		Map.y += (1.0f - FogEaseOutBack(a)) * 10.0f;
		const float Alpha = std::min(a * 1.6f, 1.0f);
		FogLabel(Ui(), TextRender(), Eyebrow, Localize("Paused, the game keeps running"), 10.0f, TEXTALIGN_ML, DIM.WithAlpha(Alpha), "Manrope", 700);
		FogLabel(Ui(), TextRender(), Map, GameClient()->Map()->BaseName(), 30.0f, TEXTALIGN_ML, FROST.WithAlpha(Alpha), "Manrope", 800);
		SLabelProperties Props;
		Props.m_MaxWidth = Server.w;
		Props.m_EllipsisAtEnd = true;
		TextRender()->TextColor(MIST.WithAlpha(Alpha));
		const ColorRGBA OldOutline = TextRender()->GetTextOutlineColor();
		TextRender()->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.0f);
		Ui()->DoLabel(&Server, Info.m_aName, 12.0f, TEXTALIGN_ML, Props);
		TextRender()->TextOutlineColor(OldOutline);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		float x = Chips.x;
		const auto Chip = [&](const char *pIcon, const char *pText) {
			TextRender()->SetDisplayFont("Manrope", 700);
			TextRender()->SetFontPreset(EFontPreset::DISPLAY_FONT);
			const float w = TextRender()->TextWidth(10.0f, pText) + 30.0f;
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			Graphics()->TextureClear();
			Graphics()->DrawRect(x, Chips.y, w, Chips.h, ColorRGBA(1.0f, 1.0f, 1.0f, 0.07f * Alpha), IGraphics::CORNER_ALL, Chips.h / 2.0f);
			Icon(TextRender(), Ui(), {x + 4.0f, Chips.y, 16.0f, Chips.h}, pIcon, 8.0f, MIST.WithAlpha(Alpha));
			FogLabel(Ui(), TextRender(), {x + 21.0f, Chips.y, w - 25.0f, Chips.h}, pText, 10.0f, TEXTALIGN_ML, FROST.WithAlpha(Alpha), "Manrope", 700);
			x += w + 6.0f;
		};
		char aBuf[64];
		const int RaceTime = GameClient()->CurrentRaceTime();
		if(RaceTime > 0)
		{
			str_format(aBuf, sizeof(aBuf), "%d:%02d", RaceTime / 60, RaceTime % 60);
			Chip(FontIcon::FLAG_CHECKERED, aBuf);
		}
		str_format(aBuf, sizeof(aBuf), "%d/%d", Info.m_NumClients, Info.m_MaxClients);
		Chip(FontIcon::USER, aBuf);
		if(Info.m_aGameType[0])
			Chip(FontIcon::MAP, Info.m_aGameType);
	}

	// actions
	struct STile
	{
		CButtonContainer *m_pButton;
		const char *m_pIcon;
		const char *m_pLabel;
		const char *m_pHint;
		bool m_Enabled;
		int m_Action;
	};
	enum
	{
		ACTION_RESUME = 0,
		ACTION_KILL,
		ACTION_PAUSE,
		ACTION_SPECTATE,
		ACTION_DUMMY,
		ACTION_DEMO,
	};
	bool Paused = false, Spec = false;
	if(GameClient()->m_Snap.m_LocalClientId >= 0)
	{
		Paused = GameClient()->m_aClients[GameClient()->m_Snap.m_LocalClientId].m_Paused;
		Spec = GameClient()->m_aClients[GameClient()->m_Snap.m_LocalClientId].m_Spec;
	}
	const bool HaveInfo = GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pGameInfoObj;
	const bool InGame = HaveInfo && GameClient()->m_Snap.m_pLocalInfo->m_Team != TEAM_SPECTATORS;
	const bool Recording = DemoRecorder(RECORDER_MANUAL)->IsRecording();
	static CButtonContainer s_aTiles[6];
	const char *pDummyLabel = Client()->DummyConnecting() ? Localize("Connecting dummy") : (Client()->DummyConnected() ? Localize("Disconnect Dummy") : Localize("Connect Dummy"));
	const STile aTiles[] = {
		{&s_aTiles[0], FontIcon::PLAY, Localize("Resume"), Localize("Back to the map"), true, ACTION_RESUME},
		{&s_aTiles[1], FontIcon::HEART_CRACK, Localize("Kill"), Localize("Start over from the spawn"), InGame && !Paused && !Spec, ACTION_KILL},
		{&s_aTiles[2], FontIcon::PAUSE, (!Paused && !Spec) ? Localize("Pause") : Localize("Join game"), Localize("Freeze your tee in place"), GameClient()->m_ReceivedDDNetPlayer && HaveInfo && (InGame || Paused || Spec), ACTION_PAUSE},
		{&s_aTiles[3], InGame ? FontIcon::EYE : FontIcon::PLAY, InGame ? Localize("Spectate") : Localize("Join game"), InGame ? Localize("Watch the others") : Localize("Get back into the round"), HaveInfo && !Paused && !Spec && !Client()->DummyConnecting(), ACTION_SPECTATE},
		{&s_aTiles[4], FontIcon::USER, pDummyLabel, Client()->DummyAllowed() ? Localize("A second tee you can switch to") : Localize("Dummy is not allowed on this server"), Client()->DummyAllowed() && !Client()->DummyConnectingDelayed() && !Client()->DummyConnecting(), ACTION_DUMMY},
		{&s_aTiles[5], Recording ? FontIcon::STOP : FontIcon::CIRCLE, Recording ? Localize("Stop record") : Localize("Record demo"), Recording ? Localize("Recording now") : Localize("Save this run as a demo"), true, ACTION_DEMO},
	};

	const int Columns = 3;
	const float TileGap = 8.0f;
	const float TileW = (Inner.w - TileGap * (Columns - 1)) / Columns, TileH = 62.0f;
	int Clicked = -1;
	for(int i = 0; i < (int)std::size(aTiles); i++)
	{
		const STile &Tile = aTiles[i];
		const int Col = i % Columns, Row = i / Columns;
		CUIRect R = {Inner.x + Col * (TileW + TileGap), Inner.y + Row * (TileH + TileGap), TileW, TileH};
		const float a = Stage(0.12f + i * 0.04f);
		const float Alpha = std::min(a * 1.6f, 1.0f) * (Tile.m_Enabled ? 1.0f : 0.4f);
		R.y += (1.0f - FogEaseOutBack(a)) * 14.0f;

		const bool Hot = Tile.m_Enabled && Ui()->HotItem() == Tile.m_pButton;
		const bool Down = Ui()->CheckActiveItem(Tile.m_pButton);
		FogSpring(m_aFogTileLift[i], m_aFogTileLiftV[i], Down ? -0.6f : (Hot ? 1.0f : 0.0f), Dt, 5.5f, 0.5f);
		R.y -= m_aFogTileLift[i] * 2.0f;
		if(Tile.m_Enabled && Ui()->DoButtonLogic(Tile.m_pButton, 0, &R, BUTTONFLAG_LEFT))
			Clicked = Tile.m_Action;

		const bool Primary = Tile.m_Action == ACTION_RESUME;
		const float Lift = std::max(m_aFogTileLift[i], 0.0f);
		const ColorRGBA Top = Primary ? ColorRGBA(0.80f, 0.86f, 0.92f, (0.80f + 0.10f * Lift) * Alpha) : ColorRGBA(1.0f, 1.0f, 1.0f, (0.07f + 0.06f * Lift) * Alpha);
		const ColorRGBA Bottom = Primary ? ColorRGBA(0.62f, 0.70f, 0.78f, (0.80f + 0.10f * Lift) * Alpha) : ColorRGBA(1.0f, 1.0f, 1.0f, (0.035f + 0.04f * Lift) * Alpha);
		Graphics()->TextureClear();
		Graphics()->DrawRect(R.x, R.y + 3.0f + Lift * 2.0f, R.w, R.h, ColorRGBA(0.0f, 0.02f, 0.05f, (0.12f + 0.08f * Lift) * Alpha), IGraphics::CORNER_ALL, 14.0f);
		Graphics()->DrawRect4(R.x, R.y, R.w, R.h, Top, Top, Bottom, Bottom, IGraphics::CORNER_ALL, 14.0f);
		Graphics()->DrawRect(R.x + 14.0f, R.y + 0.5f, R.w - 28.0f, 1.0f, ColorRGBA(1.0f, 1.0f, 1.0f, (Primary ? 0.45f : 0.14f) * Alpha), IGraphics::CORNER_NONE, 0.0f);

		const ColorRGBA Ink = Primary ? ColorRGBA(0.08f, 0.10f, 0.13f, Alpha) : FROST.WithAlpha(Alpha);
		const ColorRGBA SubInk = Primary ? ColorRGBA(0.16f, 0.19f, 0.24f, Alpha) : DIM.WithAlpha(Alpha);
		CUIRect IconRect, Text, Label, Hint;
		R.Margin(vec2(12.0f, 10.0f), &Text);
		Text.VSplitLeft(30.0f, &IconRect, &Text);
		Graphics()->TextureClear();
		Graphics()->DrawRect(IconRect.x, IconRect.y + IconRect.h / 2.0f - 13.0f, 26.0f, 26.0f, Primary ? ColorRGBA(0.08f, 0.10f, 0.13f, 0.14f * Alpha) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f * Alpha), IGraphics::CORNER_ALL, 13.0f);
		Icon(TextRender(), Ui(), {IconRect.x, IconRect.y, 26.0f, IconRect.h}, Tile.m_pIcon, 10.0f, Tile.m_Action == ACTION_DEMO && Recording ? ROSE.WithAlpha(Alpha) : Ink);
		Text.VSplitLeft(6.0f, nullptr, &Text);
		Text.HSplitMid(&Label, &Hint);
		FogLabel(Ui(), TextRender(), Label, Tile.m_pLabel, 13.0f, TEXTALIGN_BL, Ink, "Manrope", 700);
		FogLabel(Ui(), TextRender(), Hint, Tile.m_pHint, 9.5f, TEXTALIGN_TL, SubInk, "Manrope", 500);
	}

	CUIRect Footer = {Inner.x, Inner.y + 2 * (TileH + TileGap) + 8.0f, Inner.w, 14.0f};
	FogLabel(Ui(), TextRender(), Footer, Localize("Esc to go back to the game"), 9.5f, TEXTALIGN_ML, DIM.WithAlpha(Stage(0.4f)), "Manrope", 600);

	switch(Clicked)
	{
	case ACTION_RESUME:
		SetActive(false);
		break;
	case ACTION_KILL:
		GameClient()->SendKill();
		SetActive(false);
		break;
	case ACTION_PAUSE:
		Console()->ExecuteLine("say /pause", IConsole::CLIENT_ID_UNSPECIFIED);
		SetActive(false);
		break;
	case ACTION_SPECTATE:
		if(InGame)
		{
			if(g_Config.m_ClDummy == 0 || Client()->DummyConnected())
				GameClient()->SendSwitchTeam(TEAM_SPECTATORS);
		}
		else
			GameClient()->SendSwitchTeam(GameClient()->IsTeamPlay() ? (int)TEAM_RED : (int)TEAM_GAME);
		SetActive(false);
		break;
	case ACTION_DUMMY:
		if(!Client()->DummyConnected())
			Client()->DummyConnect();
		else if(GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmDisconnectTime && g_Config.m_ClConfirmDisconnectTime >= 0)
			PopupConfirm(Localize("Disconnect Dummy"), Localize("Are you sure that you want to disconnect your dummy?"), Localize("Yes"), Localize("No"), &CMenus::PopupConfirmDisconnectDummy);
		else
		{
			Client()->DummyDisconnect(nullptr);
			SetActive(false);
		}
		break;
	case ACTION_DEMO:
		if(!Recording)
			Client()->DemoRecorder_Start(GameClient()->Map()->BaseName(), true, RECORDER_MANUAL);
		else
			Client()->DemoRecorder(RECORDER_MANUAL)->Stop(IDemoRecorder::EStopMode::KEEP_FILE);
		break;
	default:
		break;
	}
}
