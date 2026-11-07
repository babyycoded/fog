// Fog client start screen: a glass navigation rail on the left, your tee standing in the mist,
// and cards for the track, the last server and friends online. Replaces the stock start menu
// while the Fog interface is enabled; every action and hotkey of the stock menu is kept.

#include "brand.h"
#include "glass.h"
#include "hires_tee.h"
#include "music.h"

#include <base/math.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/font_icons.h>
#include <engine/friends.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/animstate.h>
#include <game/client/components/menus_start.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>
#include <game/version.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr ColorRGBA FROST(0.91f, 0.93f, 0.95f, 1.0f);
constexpr ColorRGBA MIST(0.64f, 0.68f, 0.74f, 1.0f);
constexpr ColorRGBA DIM(0.46f, 0.50f, 0.56f, 1.0f);

float EaseOut(float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

void IconLabel(ITextRender *pTextRender, CUi *pUi, const CUIRect &Rect, const char *pIcon, float Size, ColorRGBA Color, int Align = TEXTALIGN_MC)
{
	pTextRender->SetFontPreset(EFontPreset::ICON_FONT);
	pTextRender->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
	pTextRender->TextColor(Color);
	pUi->DoLabel(&Rect, pIcon, Size, Align);
	pTextRender->TextColor(pTextRender->DefaultTextColor());
	pTextRender->SetRenderFlags(0);
	pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
}

} // namespace

void CMenusStart::RenderFogStart(CUIRect MainView)
{
	GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_START);

	const float Now = Client()->GlobalTime();
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	const bool Animate = g_Config.m_FogAnim != 0;
	const float Speed = Animate ? g_Config.m_FogAnimSpeed / 100.0f : 1000.0f;
	// the entrance plays whenever the start screen comes back after being away
	if(Now - m_LastFogRender > 0.4f)
		m_FogIntroStart = Now;
	m_LastFogRender = Now;
	const float Intro = (Now - m_FogIntroStart) * Speed;

	const float Margin = 18.0f;
	CUIRect Rail, Scene;
	MainView.Margin(Margin, &MainView);
	MainView.VSplitLeft(230.0f, &Rail, &Scene);
	Scene.VSplitLeft(Margin, nullptr, &Scene);

	// ---------------------------------------------------------------- navigation rail
	const float RailIn = EaseOut(Intro / 0.55f);
	Rail.x -= (1.0f - RailIn) * 40.0f;
	FogDrawGlass(Graphics(), Rail, 20.0f, RailIn);

	CUIRect Content;
	Rail.Margin(vec2(18.0f, 20.0f), &Content);

	CUIRect Wordmark, Tagline;
	Content.HSplitTop(50.0f, &Wordmark, &Content);
	Content.HSplitTop(16.0f, &Tagline, &Content);
	FogBrand::DrawLockup(Graphics(), TextRender(), Ui(), Wordmark, 42.0f, RailIn);
	FogLabel(Ui(), TextRender(), Tagline, Localize("a quiet client for DDNet"), 11.0f, TEXTALIGN_ML, DIM.WithAlpha(RailIn), "Manrope", 500);
	Content.HSplitTop(34.0f, nullptr, &Content);

	struct SNavItem
	{
		const char *m_pIcon;
		const char *m_pLabel;
		const char *m_pKey;
		CButtonContainer *m_pButton;
	};
	static CButtonContainer s_Play, s_Demos, s_Editor, s_Server, s_Settings, s_News, s_Quit;
	const bool LocalServerRunning = GameClient()->m_LocalServer.IsServerRunning();
	const SNavItem aTop[] = {
		{FontIcon::CIRCLE_PLAY, Localize("Play", "Start menu"), "P", &s_Play},
		{FontIcon::FILM, Localize("Demos"), "D", &s_Demos},
		{FontIcon::PEN_TO_SQUARE, Localize("Editor"), "E", &s_Editor},
		{FontIcon::NETWORK_WIRED, LocalServerRunning ? Localize("Join") : Localize("Run server"), "R", &s_Server},
		{FontIcon::GEAR, Localize("Settings"), "S", &s_Settings},
	};
	const SNavItem aBottom[] = {
		{FontIcon::NEWSPAPER, Localize("News"), "N", &s_News},
		{FontIcon::POWER_OFF, Localize("Quit"), "Q", &s_Quit},
	};

	const float ItemH = 38.0f, ItemGap = 4.0f;
	const void *pClicked = nullptr;
	float HoverY = -1.0f;
	const auto DoItem = [&](const SNavItem &Item, CUIRect Row, int Index) {
		// items arrive one after another
		const float Appear = EaseOut((Intro - 0.12f - Index * 0.05f) / 0.4f);
		Row.x -= (1.0f - Appear) * 16.0f;
		const int Clicked = Ui()->DoButtonLogic(Item.m_pButton, 0, &Row, BUTTONFLAG_LEFT);
		if(Ui()->HotItem() == Item.m_pButton)
			HoverY = Row.y;
		if(Clicked)
			pClicked = Item.m_pButton;

		const bool Hot = Ui()->HotItem() == Item.m_pButton;
		CUIRect Icon, Label, Key;
		Row.VSplitLeft(34.0f, &Icon, &Label);
		Label.VSplitRight(22.0f, &Label, &Key);
		IconLabel(TextRender(), Ui(), Icon, Item.m_pIcon, 14.0f, (Hot ? FROST : MIST).WithAlpha(Appear));
		FogLabel(Ui(), TextRender(), Label, Item.m_pLabel, 15.0f, TEXTALIGN_ML, (Hot ? FROST : ColorRGBA(0.82f, 0.85f, 0.89f, 1.0f)).WithAlpha(Appear), "Manrope", Hot ? 700 : 600);
		FogLabel(Ui(), TextRender(), Key, Item.m_pKey, 10.0f, TEXTALIGN_MR, DIM.WithAlpha(Appear * (Hot ? 1.0f : 0.6f)), "Manrope", 600);
	};

	// the hover pill glides between items instead of jumping
	CUIRect NavArea = Content;
	{
		CUIRect Measure = NavArea;
		for(size_t i = 0; i < std::size(aTop); i++)
		{
			CUIRect Row;
			Measure.HSplitTop(ItemH, &Row, &Measure);
			Measure.HSplitTop(ItemGap, nullptr, &Measure);
		}
	}
	CUIRect BottomArea;
	Content.HSplitBottom(ItemH * 2 + ItemGap + 22.0f, &Content, &BottomArea);

	// draw the pill first (behind items), using last frame's target
	if(m_FogHoverY >= 0.0f)
	{
		CUIRect Pill = {Content.x - 8.0f, m_FogHoverY, Content.w + 16.0f, ItemH};
		const ColorRGBA PillTop(1.0f, 1.0f, 1.0f, 0.10f * m_FogHoverAlpha), PillBottom(1.0f, 1.0f, 1.0f, 0.05f * m_FogHoverAlpha);
		Graphics()->TextureClear();
		Graphics()->DrawRect4(Pill.x, Pill.y, Pill.w, Pill.h, PillTop, PillTop, PillBottom, PillBottom, IGraphics::CORNER_ALL, 12.0f);
		Graphics()->DrawRect(Pill.x + 3.0f, Pill.y + 10.0f, 3.0f, Pill.h - 20.0f, FROST.WithAlpha(0.85f * m_FogHoverAlpha), IGraphics::CORNER_ALL, 1.5f);
	}

	int Index = 0;
	for(const SNavItem &Item : aTop)
	{
		CUIRect Row;
		NavArea.HSplitTop(ItemH, &Row, &NavArea);
		NavArea.HSplitTop(ItemGap, nullptr, &NavArea);
		DoItem(Item, Row, Index++);
	}
	CUIRect Version;
	BottomArea.HSplitBottom(16.0f, &BottomArea, &Version);
	for(const SNavItem &Item : aBottom)
	{
		CUIRect Row;
		BottomArea.HSplitTop(ItemH, &Row, &BottomArea);
		BottomArea.HSplitTop(ItemGap, nullptr, &BottomArea);
		DoItem(Item, Row, Index++);
		if(Item.m_pButton == &s_News && g_Config.m_UiUnreadNews)
		{
			// unread news: a small pulsing dot
			const float Pulse = 0.6f + 0.4f * std::sin(Now * 3.0f);
			Graphics()->TextureClear();
			Graphics()->DrawRect(Row.x + 20.0f, Row.y + 9.0f, 6.0f, 6.0f, ColorRGBA(0.62f, 0.86f, 0.72f, Pulse), IGraphics::CORNER_ALL, 3.0f);
		}
	}
	FogLabel(Ui(), TextRender(), Version, "DDNet " GAME_RELEASE_VERSION, 10.0f, TEXTALIGN_ML, DIM.WithAlpha(RailIn), "Manrope", 500);

	// spring the pill toward the hovered row
	const float Follow = 1.0f - std::exp(-Dt * 18.0f * std::min(Speed, 20.0f));
	if(HoverY >= 0.0f)
	{
		if(m_FogHoverY < 0.0f)
			m_FogHoverY = HoverY, m_FogHoverV = 0.0f;
		else
			FogSpring(m_FogHoverY, m_FogHoverV, HoverY, Dt, 5.5f, 0.42f);
		m_FogHoverAlpha = mix(m_FogHoverAlpha, 1.0f, Follow);
	}
	else
	{
		m_FogHoverAlpha = mix(m_FogHoverAlpha, 0.0f, Follow);
		if(m_FogHoverAlpha < 0.01f)
			m_FogHoverY = -1.0f;
	}

	// ---------------------------------------------------------------- scene: clock
	CUIRect TopBar, Cards;
	Scene.HSplitTop(56.0f, &TopBar, &Scene);
	Scene.HSplitBottom(150.0f, &Scene, &Cards);
	{
		CUIRect Clock, Date, Console;
		TopBar.VSplitRight(34.0f, &TopBar, &Console);
		TopBar.VSplitRight(10.0f, &TopBar, nullptr);
		TopBar.HSplitTop(32.0f, &Clock, &Date);
		char aClock[16], aDate[64];
		str_timestamp_format(aClock, sizeof(aClock), "%H:%M");
		str_timestamp_format(aDate, sizeof(aDate), "%A, %d %B");
		FogLabel(Ui(), TextRender(), Clock, aClock, 28.0f, TEXTALIGN_MR, FROST.WithAlpha(RailIn), "Manrope", 300);
		FogLabel(Ui(), TextRender(), Date, aDate, 11.0f, TEXTALIGN_TR, MIST.WithAlpha(RailIn), "Manrope", 500);

		static CButtonContainer s_ConsoleButton;
		Console.HSplitTop(34.0f, &Console, nullptr);
		if(Ui()->DoButtonLogic(&s_ConsoleButton, 0, &Console, BUTTONFLAG_LEFT))
			GameClient()->m_GameConsole.Toggle(CGameConsole::CONSOLETYPE_LOCAL);
		FogDrawGlass(Graphics(), Console, 10.0f, RailIn * (Ui()->HotItem() == &s_ConsoleButton ? 1.0f : 0.7f));
		IconLabel(TextRender(), Ui(), Console, FontIcon::TERMINAL, 12.0f, MIST.WithAlpha(RailIn));
	}

	// ---------------------------------------------------------------- scene: your tishka in the mist
	{
		const char *pSkinName = g_Config.m_ClPlayerSkin[0] ? g_Config.m_ClPlayerSkin : "default";
		const CSkins::CSkinContainer *pContainer = GameClient()->m_Skins.FindContainerOrNullptr(pSkinName);
		CTeeRenderInfo TeeInfo;
		TeeInfo.Apply(pContainer && pContainer->Skin() ? pContainer->Skin().get() : GameClient()->m_Skins.Find("default"));
		TeeInfo.ApplyColors(g_Config.m_ClPlayerUseCustomColor, g_Config.m_ClPlayerColorBody, g_Config.m_ClPlayerColorFeet);
		// sharp, upscaled parts instead of the 96 px game textures
		static CFogHiResTee s_HiRes;
		CSkin::CSkinTextures HiOriginal, HiColorable;
		if(s_HiRes.Get(Graphics(), pContainer && pContainer->Skin() ? pSkinName : "default", HiOriginal, HiColorable))
		{
			TeeInfo.m_OriginalRenderSkin = HiOriginal;
			TeeInfo.m_ColorableRenderSkin = HiColorable;
		}
		const float TeeSize = std::min(Scene.h * 0.55f, 170.0f);
		TeeInfo.m_Size = TeeSize;

		const float SceneIn = EaseOut((Intro - 0.2f) / 0.7f);
		const float Intensity = Animate ? g_Config.m_FogAnimIntensity / 100.0f : 0.0f;
		const float Bob = Animate ? std::sin(Now * 1.4f) * 3.0f * Intensity : 0.0f;
		const float GroundY = Scene.y + Scene.h * 0.48f + TeeSize * 0.27f;
		const vec2 TeePos(Scene.x + Scene.w * 0.5f, Scene.y + Scene.h * 0.48f + Bob + (1.0f - SceneIn) * 18.0f);

		// soft contact shadow right under the feet
		Graphics()->TextureClear();
		for(int i = 0; i < 4; i++)
		{
			const float w = TeeSize * (0.50f + 0.12f * i), h = TeeSize * (0.06f + 0.025f * i);
			Graphics()->DrawRect(TeePos.x - w / 2, GroundY - h / 2, w, h, ColorRGBA(0.0f, 0.02f, 0.05f, 0.10f * SceneIn * (1.0f - Bob / 12.0f)), IGraphics::CORNER_ALL, h / 2);
		}

		vec2 Look = Ui()->MousePos() - TeePos;
		Look = length(Look) > 0.001f ? normalize(Look) : vec2(1.0f, 0.0f);
		static CButtonContainer s_TeeButton;
		const CUIRect TeeRect = {TeePos.x - TeeSize * 0.5f, TeePos.y - TeeSize * 0.5f, TeeSize, TeeSize};
		if(Ui()->DoButtonLogic(&s_TeeButton, 0, &TeeRect, BUTTONFLAG_LEFT))
			GameClient()->m_Menus.OpenFogStudio();
		const int Emote = Ui()->HotItem() == &s_TeeButton ? EMOTE_HAPPY : EMOTE_NORMAL;
		GameClient()->m_FogTeeStyle.Apply(TeeInfo, CFogTeeStyle::SLOT_PLAYER, pContainer && pContainer->Skin() ? pSkinName : "default");
		TeeInfo.m_FogAccessorySlot = GameClient()->m_FogAccessories.SlotFor(0);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, Emote, Look, TeePos, SceneIn);

		// the pencil next to the tee opens the tee studio
		{
			static CButtonContainer s_PencilButton;
			static float s_PencilHover = 0.0f, s_PencilV = 0.0f;
			const bool Hot = Ui()->HotItem() == &s_PencilButton;
			FogSpring(s_PencilHover, s_PencilV, Hot || Ui()->HotItem() == &s_TeeButton ? 1.0f : 0.0f, std::min(Client()->RenderFrameTime(), 0.1f), 6.0f, 0.4f);
			const float r = 15.0f + s_PencilHover * 2.0f;
			const CUIRect Pencil = {TeePos.x + TeeSize * 0.42f - r, TeePos.y - TeeSize * 0.42f - r, r * 2.0f, r * 2.0f};
			FogDrawGlass(Graphics(), Pencil, r, SceneIn * (0.7f + 0.3f * s_PencilHover));
			IconLabel(TextRender(), Ui(), Pencil, FontIcon::PENCIL, 10.0f + s_PencilHover, FROST.WithAlpha(SceneIn));
			if(Ui()->DoButtonLogic(&s_PencilButton, 0, &Pencil, BUTTONFLAG_LEFT))
				GameClient()->m_Menus.OpenFogStudio();
			Ui()->DoToolTip(&s_PencilButton, &Pencil, Localize("Tee studio"));
		}

		CUIRect Name = {Scene.x, GroundY + TeeSize * 0.18f, Scene.w, 32.0f};
		const char *pName = g_Config.m_PlayerName[0] ? g_Config.m_PlayerName : Client()->PlayerName();
		FogLabel(Ui(), TextRender(), Name, pName, 26.0f, TEXTALIGN_MC, FROST.WithAlpha(SceneIn), "Nunito", 800);
		if(g_Config.m_PlayerClan[0])
		{
			CUIRect Clan = {Scene.x, Name.y + 30.0f, Scene.w, 16.0f};
			FogLabel(Ui(), TextRender(), Clan, g_Config.m_PlayerClan, 12.0f, TEXTALIGN_MC, MIST.WithAlpha(SceneIn), "Manrope", 600);
		}
	}

	// ---------------------------------------------------------------- cards
	CUIRect aCard[3];
	{
		CUIRect Row = Cards;
		const float Gap = 14.0f, W = (Row.w - Gap * 2) / 3.0f;
		for(int i = 0; i < 3; i++)
		{
			Row.VSplitLeft(W, &aCard[i], &Row);
			Row.VSplitLeft(Gap, nullptr, &Row);
		}
	}
	const auto CardIn = [&](int i) { return EaseOut((Intro - 0.35f - i * 0.08f) / 0.5f); };
	const auto CardFrame = [&](CUIRect &Card, int i, const char *pIcon, const char *pTitle) {
		const float In = CardIn(i);
		Card.y += (1.0f - In) * 22.0f;
		FogDrawGlass(Graphics(), Card, 16.0f, In);
		CUIRect Inner, Head, HeadIcon;
		Card.Margin(vec2(16.0f, 14.0f), &Inner);
		Inner.HSplitTop(16.0f, &Head, &Inner);
		Head.VSplitLeft(18.0f, &HeadIcon, &Head);
		IconLabel(TextRender(), Ui(), HeadIcon, pIcon, 10.0f, DIM.WithAlpha(In), TEXTALIGN_ML);
		FogLabel(Ui(), TextRender(), Head, pTitle, 11.0f, TEXTALIGN_ML, MIST.WithAlpha(In), "Manrope", 700);
		Inner.HSplitTop(8.0f, nullptr, &Inner);
		return std::make_pair(Inner, In);
	};

	// now playing, with real controls for Spotify
	{
		auto [Inner, In] = CardFrame(aCard[0], 0, FontIcon::MUSIC, Localize("Now playing"));
		const CFogMusic &Music = GameClient()->m_FogMusic;
		CUIRect Title, Artist, Bar, Controls, Top;
		Inner.HSplitTop(46.0f, &Top, &Inner);
		if(Music.HasTrack() && Music.HasAlbumArt())
		{
			CUIRect Art;
			Top.VSplitLeft(46.0f, &Art, &Top);
			Top.VSplitLeft(12.0f, nullptr, &Top);
			FogDrawAlbumArt(Graphics(), Music.AlbumArt(), Art, In);
		}
		Top.HMargin(5.0f, &Top);
		Top.HSplitTop(20.0f, &Title, &Artist);
		Inner.HSplitBottom(28.0f, &Inner, &Controls);
		Inner.HSplitBottom(10.0f, &Inner, &Bar);
		if(Music.HasTrack())
		{
			FogLabel(Ui(), TextRender(), Title, Music.Media().m_Title.c_str(), 15.0f, TEXTALIGN_ML, FROST.WithAlpha(In), "Manrope", 700);
			FogLabel(Ui(), TextRender(), Artist, Music.Media().m_Artist.c_str(), 12.0f, TEXTALIGN_ML, MIST.WithAlpha(In), "Manrope", 500);
			const int64_t Len = Music.Media().m_LengthUs / 1000;
			const float Fraction = Len > 0 ? std::clamp((float)Music.PositionMs() / Len, 0.0f, 1.0f) : 0.0f;
			Graphics()->TextureClear();
			Graphics()->DrawRect(Bar.x, Bar.y + 4.0f, Bar.w, 2.0f, ColorRGBA(1, 1, 1, 0.12f * In), IGraphics::CORNER_ALL, 1.0f);
			Graphics()->DrawRect(Bar.x, Bar.y + 4.0f, Bar.w * Fraction, 2.0f, FROST.WithAlpha(0.85f * In), IGraphics::CORNER_ALL, 1.0f);

			static CButtonContainer s_Prev, s_PlayPause, s_Next;
			const struct
			{
				CButtonContainer *m_pButton;
				const char *m_pIcon;
				CMprisClient::ECommand m_Command;
			} aControls[] = {
				{&s_Prev, FontIcon::BACKWARD_STEP, CMprisClient::ECommand::PREVIOUS},
				{&s_PlayPause, Music.Media().m_Playing ? FontIcon::PAUSE : FontIcon::PLAY, CMprisClient::ECommand::PLAY_PAUSE},
				{&s_Next, FontIcon::FORWARD_STEP, CMprisClient::ECommand::NEXT},
			};
			CUIRect Row = Controls;
			for(const auto &Control : aControls)
			{
				CUIRect Button;
				Row.VSplitLeft(36.0f, &Button, &Row);
				Row.VSplitLeft(6.0f, nullptr, &Row);
				if(Ui()->DoButtonLogic(Control.m_pButton, 0, &Button, BUTTONFLAG_LEFT))
					GameClient()->m_FogMusic.Command(Control.m_Command);
				const bool Hot = Ui()->HotItem() == Control.m_pButton;
				if(Hot)
				{
					Graphics()->TextureClear();
					Graphics()->DrawRect(Button.x, Button.y, Button.w, Button.h, ColorRGBA(1, 1, 1, 0.08f * In), IGraphics::CORNER_ALL, 10.0f);
				}
				IconLabel(TextRender(), Ui(), Button, Control.m_pIcon, 12.0f, (Hot ? FROST : MIST).WithAlpha(In));
			}
		}
		else
		{
			FogLabel(Ui(), TextRender(), Title, Localize("Silence"), 15.0f, TEXTALIGN_ML, FROST.WithAlpha(In), "Manrope", 700);
			FogLabel(Ui(), TextRender(), Artist, Localize("Start Spotify and the track shows up here"), 12.0f, TEXTALIGN_ML, MIST.WithAlpha(In), "Manrope", 500);
		}
	}

	// back to the last server
	{
		auto [Inner, In] = CardFrame(aCard[1], 1, FontIcon::RIGHT_TO_BRACKET, Localize("Continue"));
		const char *pAddress = g_Config.m_UiServerAddress;
		const CServerInfo *pFound = nullptr;
		for(int i = 0; i < ServerBrowser()->NumSortedServers() && pAddress[0]; i++)
		{
			const CServerInfo *pInfo = ServerBrowser()->SortedGet(i);
			if(str_find(pInfo->m_aAddress, pAddress))
			{
				pFound = pInfo;
				break;
			}
		}
		CUIRect Name, Detail, Button;
		Inner.HSplitTop(20.0f, &Name, &Inner);
		Inner.HSplitTop(16.0f, &Detail, &Inner);
		Inner.HSplitBottom(28.0f, &Inner, &Button);
		char aDetail[128];
		if(pFound)
			str_format(aDetail, sizeof(aDetail), "%s, %d/%d", pFound->m_aMap, pFound->m_NumClients, pFound->m_MaxClients);
		else
			str_copy(aDetail, pAddress);
		FogLabel(Ui(), TextRender(), Name, pFound ? pFound->m_aName : Localize("Last server"), 15.0f, TEXTALIGN_ML, FROST.WithAlpha(In), "Manrope", 700);
		FogLabel(Ui(), TextRender(), Detail, aDetail, 12.0f, TEXTALIGN_ML, MIST.WithAlpha(In), "Manrope", 500);

		static CButtonContainer s_Connect;
		Button.VSplitLeft(std::min(Button.w, 130.0f), &Button, nullptr);
		if(Ui()->DoButtonLogic(&s_Connect, 0, &Button, BUTTONFLAG_LEFT) && pAddress[0])
			Client()->Connect(pAddress);
		const bool Hot = Ui()->HotItem() == &s_Connect;
		Graphics()->TextureClear();
		Graphics()->DrawRect(Button.x, Button.y, Button.w, Button.h, ColorRGBA(1, 1, 1, (Hot ? 0.16f : 0.09f) * In), IGraphics::CORNER_ALL, 10.0f);
		FogLabel(Ui(), TextRender(), Button, Localize("Connect"), 12.0f, TEXTALIGN_MC, FROST.WithAlpha(In), "Manrope", 700);
	}

	// friends online, found in the server list
	{
		auto [Inner, In] = CardFrame(aCard[2], 2, FontIcon::USER, Localize("Friends online"));
		struct SFriendRow
		{
			const char *m_pName;
			const char *m_pServer;
		};
		SFriendRow aRows[3];
		int NumRows = 0, Total = 0;
		for(int i = 0; i < ServerBrowser()->NumSortedServers(); i++)
		{
			const CServerInfo *pInfo = ServerBrowser()->SortedGet(i);
			if(pInfo->m_FriendState == IFriends::FRIEND_NO)
				continue;
			for(const CServerInfo::CClient &Player : pInfo->m_vClients)
			{
				if(Player.m_FriendState != IFriends::FRIEND_PLAYER)
					continue;
				Total++;
				if(NumRows < 3)
					aRows[NumRows++] = {Player.m_aName, pInfo->m_aName};
			}
		}
		if(Total == 0)
		{
			CUIRect Empty;
			Inner.HSplitTop(20.0f, &Empty, &Inner);
			FogLabel(Ui(), TextRender(), Empty, Localize("Nobody yet"), 15.0f, TEXTALIGN_ML, FROST.WithAlpha(In), "Manrope", 700);
			Inner.HSplitTop(16.0f, &Empty, &Inner);
			FogLabel(Ui(), TextRender(), Empty, Localize("Add friends in the server browser"), 12.0f, TEXTALIGN_ML, MIST.WithAlpha(In), "Manrope", 500);
		}
		for(int i = 0; i < NumRows; i++)
		{
			CUIRect Row, Dot, Name, Server;
			Inner.HSplitTop(22.0f, &Row, &Inner);
			Row.VSplitLeft(12.0f, &Dot, &Row);
			Row.VSplitMid(&Name, &Server, 6.0f);
			Graphics()->TextureClear();
			Graphics()->DrawRect(Dot.x, Dot.y + 8.0f, 6.0f, 6.0f, ColorRGBA(0.62f, 0.86f, 0.72f, In), IGraphics::CORNER_ALL, 3.0f);
			FogLabel(Ui(), TextRender(), Name, aRows[i].m_pName, 13.0f, TEXTALIGN_ML, FROST.WithAlpha(In), "Manrope", 700);
			FogLabel(Ui(), TextRender(), Server, aRows[i].m_pServer, 11.0f, TEXTALIGN_MR, MIST.WithAlpha(In), "Manrope", 500);
		}
	}

	// ---------------------------------------------------------------- actions and hotkeys (as the stock menu)
	int NewPage = -1;
	bool UsedEscape = false;
	if(pClicked == &s_Play || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || CheckHotKey(KEY_P))
		NewPage = g_Config.m_UiPage >= CMenus::PAGE_INTERNET && g_Config.m_UiPage <= CMenus::PAGE_FAVORITE_COMMUNITY_5 ? g_Config.m_UiPage : CMenus::PAGE_INTERNET;
	else if(pClicked == &s_Demos || CheckHotKey(KEY_D))
		NewPage = CMenus::PAGE_DEMOS;
	else if(pClicked == &s_Editor || CheckHotKey(KEY_E))
		g_Config.m_ClEditor = 1;
	else if(pClicked == &s_Server || CheckHotKey(KEY_R))
	{
		if(LocalServerRunning)
			GameClient()->m_LocalServer.Connect();
		else
			GameClient()->m_LocalServer.RunServer({});
	}
	else if(pClicked == &s_Settings || CheckHotKey(KEY_S))
		NewPage = CMenus::PAGE_SETTINGS;
	else if(pClicked == &s_News || CheckHotKey(KEY_N))
		NewPage = CMenus::PAGE_NEWS;
	else if(pClicked == &s_Quit || (UsedEscape = Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE)) || CheckHotKey(KEY_Q))
	{
		if(UsedEscape || GameClient()->Editor()->HasUnsavedData() || (GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0))
			GameClient()->m_Menus.ShowQuitPopup();
		else
			Client()->Quit();
	}

	if(NewPage != -1)
	{
		GameClient()->m_Menus.SetShowStart(false);
		GameClient()->m_Menus.SetMenuPage(NewPage);
	}
}
