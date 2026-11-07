// Fog client: the tee studio. A full-screen editor that opens over any menu (the pencil on
// the start screen, the Tee settings page, the Esc menu): a big stage with your tishka on
// the left, tabs for skin, colours, pattern and dummy on the right.

#include "accessories.h"
#include "glass.h"
#include "hires_tee.h"
#include "painter.h"
#include "skin_catalog.h"
#include "tee_style.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/font_icons.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/animstate.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr ColorRGBA FROST(0.91f, 0.93f, 0.95f, 1.0f);
constexpr ColorRGBA MIST(0.64f, 0.68f, 0.74f, 1.0f);
constexpr ColorRGBA DIM(0.46f, 0.50f, 0.56f, 1.0f);

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

float EaseOut(float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

} // namespace

void CMenus::OpenFogStudio()
{
	m_FogStudioOpen = true;
	m_FogStudioOpenTime = Client()->GlobalTime();
	GameClient()->m_FogTeeStyle.ScanPatterns();
	m_SkinListScrollToSelected = true;
}

void CMenus::RenderFogStudio(CUIRect Screen)
{
	const float Now = Client()->GlobalTime();
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	const float Speed = g_Config.m_FogAnim ? g_Config.m_FogAnimSpeed / 100.0f : 1000.0f;
	const float Since = (Now - m_FogStudioOpenTime) * Speed;
	const auto In = [&](float Delay) { return EaseOut((Since - Delay) / 0.45f); };

	if(Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		m_FogStudioOpen = false;
		return;
	}

	// over the game the world stays visible behind a dark veil
	if(Client()->State() == IClient::STATE_ONLINE)
	{
		Graphics()->TextureClear();
		const CUIRect Full = *Ui()->Screen();
		Graphics()->DrawRect(Full.x, Full.y, Full.w, Full.h, ColorRGBA(0.04f, 0.05f, 0.07f, 0.62f * In(0.0f)), IGraphics::CORNER_NONE, 0.0f);
	}

	// which tee is being edited: you or your dummy
	const bool Dummy = m_Dummy;
	char *pSkinName = Dummy ? g_Config.m_ClDummySkin : g_Config.m_ClPlayerSkin;
	const size_t SkinNameSize = Dummy ? sizeof(g_Config.m_ClDummySkin) : sizeof(g_Config.m_ClPlayerSkin);
	int *pUseCustomColor = Dummy ? &g_Config.m_ClDummyUseCustomColor : &g_Config.m_ClPlayerUseCustomColor;
	unsigned *pColorBody = Dummy ? &g_Config.m_ClDummyColorBody : &g_Config.m_ClPlayerColorBody;
	unsigned *pColorFeet = Dummy ? &g_Config.m_ClDummyColorFeet : &g_Config.m_ClPlayerColorFeet;
	// a synced dummy follows you, so its skin and game colours are not edited here
	const bool DummyLocked = Dummy && g_Config.m_FogDummySync != 0;

	CUIRect Top, Body;
	Screen.HSplitTop(44.0f, &Top, &Body);
	Body.HSplitTop(8.0f, nullptr, &Body);

	// ---------------------------------------------------------------- top bar
	{
		const float a = In(0.0f);
		CUIRect Back, Title, Sub;
		Top.VSplitLeft(96.0f, &Back, &Top);
		Back.HMargin(6.0f, &Back);
		static CButtonContainer s_BackButton;
		const bool Hot = Ui()->HotItem() == &s_BackButton;
		FogDrawGlass(Graphics(), Back, Back.h / 2.0f, a * (Hot ? 1.0f : 0.75f));
		CUIRect BackIcon, BackLabel;
		Back.VSplitLeft(30.0f, &BackIcon, &BackLabel);
		Icon(TextRender(), Ui(), BackIcon, FontIcon::CHEVRON_LEFT, 10.0f, FROST.WithAlpha(a));
		FogLabel(Ui(), TextRender(), BackLabel, Localize("Back"), 12.0f, TEXTALIGN_ML, FROST.WithAlpha(a), "Manrope", 700);
		if(Ui()->DoButtonLogic(&s_BackButton, 0, &Back, BUTTONFLAG_LEFT))
		{
			m_FogStudioOpen = false;
			return;
		}
		Top.VSplitLeft(16.0f, nullptr, &Top);
		Top.HSplitTop(26.0f, &Title, &Sub);
		FogLabel(Ui(), TextRender(), Title, Localize("Tee studio"), 22.0f, TEXTALIGN_ML, FROST.WithAlpha(a), "Manrope", 800);
		FogLabel(Ui(), TextRender(), Sub, Localize("Skin and colours are seen by everyone. Animation and patterns are drawn only by your client."), 11.0f, TEXTALIGN_ML, DIM.WithAlpha(a), "Manrope", 500);
	}

	CUIRect Stage, Panel;
	Body.VSplitLeft(Body.w * 0.48f, &Stage, &Panel);
	CFogAccessories &Accessories = GameClient()->m_FogAccessories;
	const int AccSlot = Dummy ? 1 : 0;
	CUIRect StageFloor = Stage;
	vec2 MainLook(1.0f, 0.0f);
	static int s_CarryDef = -1;
	static vec2 s_CarryStart;
	static bool s_Carrying = false;
	static CButtonContainer s_AccDragId;
	static vec2 s_AccGrab;
	Panel.VSplitLeft(14.0f, nullptr, &Panel);

	// ---------------------------------------------------------------- stage
	{
		const float a = In(0.05f);
		Stage.y += (1.0f - a) * 24.0f;
		FogDrawGlass(Graphics(), Stage, 22.0f, a);

		// you / dummy switch at the bottom of the stage
		CUIRect Switch, Floor;
		Stage.HSplitBottom(44.0f, &Floor, &Switch);
		Switch.VMargin((Switch.w - 200.0f) / 2.0f, &Switch);
		Switch.HMargin(8.0f, &Switch);
		{
			static CButtonContainer s_aSlotButtons[2];
			static float s_PillX = 0.0f, s_PillV = 0.0f;
			FogSpring(s_PillX, s_PillV, Dummy ? 1.0f : 0.0f, Dt, 6.0f, 0.35f);
			Graphics()->TextureClear();
			Graphics()->DrawRect(Switch.x, Switch.y, Switch.w, Switch.h, ColorRGBA(0.0f, 0.0f, 0.0f, 0.22f * a), IGraphics::CORNER_ALL, Switch.h / 2.0f);
			const float HalfW = Switch.w / 2.0f;
			Graphics()->DrawRect(Switch.x + 2.0f + s_PillX * HalfW, Switch.y + 2.0f, HalfW - 4.0f, Switch.h - 4.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.16f * a), IGraphics::CORNER_ALL, (Switch.h - 4.0f) / 2.0f);
			const char *apLabels[] = {Localize("You", "Tee studio"), Localize("Dummy")};
			for(int i = 0; i < 2; i++)
			{
				CUIRect Half = {Switch.x + i * HalfW, Switch.y, HalfW, Switch.h};
				FogLabel(Ui(), TextRender(), Half, apLabels[i], 12.0f, TEXTALIGN_MC, ((int)Dummy == i ? FROST : MIST).WithAlpha(a), "Manrope", 700);
				if(Ui()->DoButtonLogic(&s_aSlotButtons[i], 0, &Half, BUTTONFLAG_LEFT))
				{
					m_Dummy = i == 1;
					m_SkinListScrollToSelected = true;
				}
			}
		}

		// the tee, big, in a soft pool of light
		const float TeeSize = std::min(Floor.h * 0.5f, Floor.w * 0.42f);
		const float Bob = g_Config.m_FogAnim ? std::sin(Now * 1.6f) * 3.0f * g_Config.m_FogAnimIntensity / 100.0f : 0.0f;
		const vec2 TeePos(Floor.x + Floor.w * 0.5f, Floor.y + Floor.h * 0.47f + Bob + (1.0f - a) * 20.0f);
		const float GroundY = Floor.y + Floor.h * 0.47f + TeeSize * 0.3f;
		Graphics()->TextureClear();
		for(int i = 0; i < 6; i++)
		{
			const float r = TeeSize * (0.55f + i * 0.22f);
			Graphics()->DrawRect(TeePos.x - r, TeePos.y - r * 0.9f, r * 2.0f, r * 1.8f, ColorRGBA(0.75f, 0.82f, 0.92f, 0.018f * a), IGraphics::CORNER_ALL, r * 0.9f);
		}
		for(int i = 0; i < 4; i++)
		{
			const float w = TeeSize * (0.5f + 0.12f * i), h = TeeSize * (0.06f + 0.025f * i);
			Graphics()->DrawRect(TeePos.x - w / 2, GroundY - h / 2, w, h, ColorRGBA(0.0f, 0.02f, 0.05f, 0.11f * a), IGraphics::CORNER_ALL, h / 2);
		}

		const auto BuildInfo = [&](bool ForDummy, float Size, CTeeRenderInfo &Info) {
			const char *pName = ForDummy ? g_Config.m_ClDummySkin : g_Config.m_ClPlayerSkin;
			if(!pName[0])
				pName = "default";
			const CSkins::CSkinContainer *pContainer = GameClient()->m_Skins.FindContainerOrNullptr(pName);
			const bool Valid = pContainer && pContainer->Skin() && !pContainer->IsSpecial();
			Info.Apply(Valid ? pContainer->Skin().get() : GameClient()->m_Skins.Find("default"));
			if(ForDummy)
				Info.ApplyColors(g_Config.m_ClDummyUseCustomColor, g_Config.m_ClDummyColorBody, g_Config.m_ClDummyColorFeet);
			else
				Info.ApplyColors(g_Config.m_ClPlayerUseCustomColor, g_Config.m_ClPlayerColorBody, g_Config.m_ClPlayerColorFeet);
			Info.m_Size = Size;
			Info.m_FogAccessorySlot = ForDummy ? 1 : 0;
			const char *pFile = Valid ? pName : "default";
			if(m_FogStudioTab == 5 && ForDummy == Dummy && GameClient()->m_FogPainter.Started())
			{
				Info.m_OriginalRenderSkin = GameClient()->m_FogPainter.PreviewTextures();
				Info.m_CustomColoredSkin = false;
				Info.m_ColorBody = Info.m_ColorFeet = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
				Info.m_FogAccessorySlot = -1;
				return;
			}
			if(!GameClient()->m_FogTeeStyle.Apply(Info, ForDummy ? CFogTeeStyle::SLOT_DUMMY : CFogTeeStyle::SLOT_PLAYER, pFile))
			{
				// sharp upscaled parts instead of the game textures
				static CFogHiResTee s_aHiRes[2];
				CSkin::CSkinTextures Original, Colorable;
				if(s_aHiRes[ForDummy].Get(Graphics(), pFile, Original, Colorable))
				{
					Info.m_OriginalRenderSkin = Original;
					Info.m_ColorableRenderSkin = Colorable;
				}
			}
		};

		// the other tee stands a little behind, smaller
		{
			CTeeRenderInfo Other;
			BuildInfo(!Dummy, TeeSize * 0.45f, Other);
			const vec2 OtherPos(TeePos.x + TeeSize * 0.95f, GroundY - TeeSize * 0.2f + Bob * 0.5f);
			RenderTools()->RenderTee(CAnimState::GetIdle(), &Other, EMOTE_NORMAL, normalize(TeePos - OtherPos), OtherPos, a * 0.55f);
		}

		CTeeRenderInfo Info;
		BuildInfo(Dummy, TeeSize, Info);
		vec2 Look = Ui()->MousePos() - TeePos;
		Look = length(Look) > 0.001f ? normalize(Look) : vec2(1.0f, 0.0f);
		static CButtonContainer s_TeeButton;
		const CUIRect TeeRect = {TeePos.x - TeeSize * 0.5f, TeePos.y - TeeSize * 0.5f, TeeSize, TeeSize};
		if(m_FogStudioTab < 4 && Ui()->DoButtonLogic(&s_TeeButton, 0, &TeeRect, BUTTONFLAG_LEFT))
		{
			// a poke: cycle through the emotes for a moment
			m_FogStudioEmote = 1 + (m_FogStudioEmote % (NUM_EMOTES - 1));
			m_FogStudioEmoteUntil = Now + 1.4f;
		}
		const int Emote = Now < m_FogStudioEmoteUntil ? m_FogStudioEmote : (Ui()->HotItem() == &s_TeeButton ? EMOTE_HAPPY : (Dummy ? g_Config.m_ClDummyDefaultEyes : g_Config.m_ClPlayerDefaultEyes));
		RenderTools()->RenderTee(CAnimState::GetIdle(), &Info, Emote, Look, TeePos, a);
		StageFloor = Floor;
		MainLook = Look;

		// accessories tab: grab a worn piece on the tee and move it; the wheel resizes it
		if(m_FogStudioTab == 4)
		{
			std::vector<SFogAccessory> &vWorn = Accessories.Worn(AccSlot);
			const vec2 BodyPos = Accessories.LastBodyPos(AccSlot);
			const float BaseSize = std::max(Accessories.LastBaseSize(AccSlot), 1.0f);
			const vec2 Mouse = Ui()->MousePos();
			const auto EyeShift = [&](const SFogAccessory &Acc) {
				int Num;
				return CFogAccessories::Catalogue(&Num)[Acc.m_Def].m_FollowEyes ? vec2(Look.x * 0.125f, Look.y * 0.10f) : vec2(0.0f, 0.0f);
			};
			if(m_FogStudioAccessory >= (int)vWorn.size())
				m_FogStudioAccessory = -1;
			if(Ui()->CheckActiveItem(&s_AccDragId))
			{
				if(Ui()->MouseButton(0) && m_FogStudioAccessory >= 0)
				{
					SFogAccessory &Acc = vWorn[m_FogStudioAccessory];
					const vec2 p = (Mouse - s_AccGrab - BodyPos) / BaseSize - EyeShift(Acc);
					Acc.m_X = std::clamp(p.x, -1.5f, 1.5f);
					Acc.m_Y = std::clamp(p.y, -1.5f, 1.5f);
				}
				else
				{
					Ui()->SetActiveItem(nullptr);
					Accessories.Save(AccSlot);
				}
			}
			else if(!s_Carrying && Ui()->MouseInside(&Floor) && Ui()->MouseButtonClicked(0) && !Ui()->ActiveItem())
			{
				int Hit = -1;
				for(int i = (int)vWorn.size() - 1; i >= 0 && Hit < 0; i--)
				{
					vec2 Center;
					float Size;
					if(Accessories.LastRect(AccSlot, i, Center, Size) && distance(Mouse, Center) < std::max(Size * 0.42f, 10.0f))
						Hit = i;
				}
				m_FogStudioAccessory = Hit;
				if(Hit >= 0)
				{
					vec2 Center;
					float Size;
					Accessories.LastRect(AccSlot, Hit, Center, Size);
					s_AccGrab = Mouse - Center;
					Ui()->SetActiveItem(&s_AccDragId);
				}
			}
			if(m_FogStudioAccessory >= 0 && Ui()->MouseInside(&Floor))
			{
				SFogAccessory &Acc = vWorn[m_FogStudioAccessory];
				float Wheel = 0.0f;
				if(Input()->KeyPress(KEY_MOUSE_WHEEL_UP))
					Wheel = 1.0f;
				if(Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN))
					Wheel = -1.0f;
				if(Wheel != 0.0f)
				{
					Acc.m_Size = std::clamp(Acc.m_Size * (Wheel > 0.0f ? 1.08f : 1.0f / 1.08f), 0.08f, 2.5f);
					Accessories.Save(AccSlot);
				}
			}
			// a quiet ring around the selected piece
			vec2 Center;
			float Size;
			if(m_FogStudioAccessory >= 0 && Accessories.LastRect(AccSlot, m_FogStudioAccessory, Center, Size))
			{
				const float r = std::max(Size * 0.55f, 12.0f);
				Graphics()->TextureClear();
				Graphics()->DrawRect(Center.x - r, Center.y - r, r * 2.0f, r * 2.0f, ColorRGBA(0.85f, 0.9f, 1.0f, 0.07f + 0.03f * std::sin(Now * 3.0f)), IGraphics::CORNER_ALL, r);
				Graphics()->DrawRect(Center.x - 3.0f, Center.y - 3.0f, 6.0f, 6.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.8f), IGraphics::CORNER_ALL, 3.0f);
			}
			CUIRect HintRect = {Floor.x, Floor.y + 12.0f, Floor.w, 14.0f};
			FogLabel(Ui(), TextRender(), HintRect, Localize("Drag pieces onto the tee. Drag to move, wheel to resize."), 10.0f, TEXTALIGN_MC, DIM.WithAlpha(a), "Manrope", 500);
		}

		CUIRect Name = {Floor.x, GroundY + TeeSize * 0.14f, Floor.w, 26.0f};
		FogLabel(Ui(), TextRender(), Name, pSkinName[0] ? pSkinName : "default", 18.0f, TEXTALIGN_MC, FROST.WithAlpha(a), "Manrope", 800);
		if(GameClient()->m_FogTeeStyle.Enabled(Dummy ? CFogTeeStyle::SLOT_DUMMY : CFogTeeStyle::SLOT_PLAYER))
		{
			CUIRect Note = {Floor.x, Name.y + 24.0f, Floor.w, 14.0f};
			FogLabel(Ui(), TextRender(), Note, Localize("Animated look on"), 10.0f, TEXTALIGN_MC, MIST.WithAlpha(a), "Manrope", 600);
		}
	}

	// ---------------------------------------------------------------- panel
	const float PanelIn = In(0.12f);
	Panel.y += (1.0f - PanelIn) * 24.0f;
	FogDrawGlass(Graphics(), Panel, 22.0f, PanelIn);
	CUIRect Tabs, Content;
	Panel.Margin(vec2(18.0f, 14.0f), &Content);
	Content.HSplitTop(30.0f, &Tabs, &Content);
	Content.HSplitTop(12.0f, nullptr, &Content);
	{
		static CButtonContainer s_aTabButtons[6];
		static float s_TabX = 0.0f, s_TabV = 0.0f;
		const char *apTabs[] = {Localize("Skin"), Localize("Colours"), Localize("Pattern"), Localize("Dummy"), Localize("Accessories"), Localize("Draw")};
		const char *apIcons[] = {FontIcon::USER, FontIcon::EYE_DROPPER, FontIcon::BRUSH, FontIcon::LAYER_GROUP, FontIcon::STAR, FontIcon::PENCIL};
		FogSpring(s_TabX, s_TabV, (float)m_FogStudioTab, Dt, 6.0f, 0.35f);
		const float TabW = Tabs.w / 6.0f;
		Graphics()->TextureClear();
		Graphics()->DrawRect(Tabs.x, Tabs.y, Tabs.w, Tabs.h, ColorRGBA(0.0f, 0.0f, 0.0f, 0.2f * PanelIn), IGraphics::CORNER_ALL, Tabs.h / 2.0f);
		Graphics()->DrawRect(Tabs.x + 2.0f + s_TabX * TabW, Tabs.y + 2.0f, TabW - 4.0f, Tabs.h - 4.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.15f * PanelIn), IGraphics::CORNER_ALL, (Tabs.h - 4.0f) / 2.0f);
		for(int i = 0; i < 6; i++)
		{
			CUIRect Tab = {Tabs.x + i * TabW, Tabs.y, TabW, Tabs.h}, TabIcon, TabLabel;
			const ColorRGBA c = (m_FogStudioTab == i ? FROST : MIST).WithAlpha(PanelIn);
			const float LabelW = TextRender()->TextWidth(10.0f, apTabs[i]) + 17.0f;
			Tab.VMargin(std::max(0.0f, (Tab.w - LabelW) / 2.0f), &TabLabel);
			TabLabel.VSplitLeft(16.0f, &TabIcon, &TabLabel);
			Icon(TextRender(), Ui(), TabIcon, apIcons[i], 9.0f, c);
			FogLabel(Ui(), TextRender(), TabLabel, apTabs[i], 10.0f, TEXTALIGN_ML, c, "Manrope", 700);
			if(Ui()->DoButtonLogic(&s_aTabButtons[i], 0, &Tab, BUTTONFLAG_LEFT))
				m_FogStudioTab = i;
		}
	}

	constexpr float LINE = 20.0f, SPACING = 4.0f, HEADLINE = 15.0f;
	const auto Headline = [&](CUIRect &Column, const char *pText) {
		CUIRect Rect;
		Column.HSplitTop(HEADLINE + 8.0f, &Rect, &Column);
		FogLabel(Ui(), TextRender(), Rect, pText, HEADLINE, TEXTALIGN_ML, FROST, "Manrope", 800);
	};
	const auto Hint = [&](CUIRect &Column, const char *pText) {
		CUIRect Rect;
		Column.HSplitTop(16.0f, &Rect, &Column);
		FogLabel(Ui(), TextRender(), Rect, pText, 10.0f, TEXTALIGN_ML, DIM, "Manrope", 500);
		Column.HSplitTop(SPACING, nullptr, &Column);
	};
	const auto Row = [&](CUIRect &Column, float Height) {
		CUIRect Rect;
		Column.HSplitTop(Height, &Rect, &Column);
		Column.HSplitTop(SPACING, nullptr, &Column);
		return Rect;
	};
	const auto CheckBox = [&](CUIRect &Column, int *pValue, const char *pLabel) {
		CUIRect Rect = Row(Column, LINE);
		const bool Clicked = DoButton_CheckBox(pValue, pLabel, *pValue, &Rect);
		if(Clicked)
			*pValue ^= 1;
		return Clicked;
	};
	const auto Slider = [&](CUIRect &Column, int *pValue, const char *pLabel, int Min, int Max, const char *pSuffix = "") {
		CUIRect Rect = Row(Column, LINE);
		Ui()->DoScrollbarOption(pValue, pValue, &Rect, pLabel, Min, Max, &CUi::ms_LinearScrollbarScale, 0u, pSuffix);
	};
	// a row of pills; returns the clicked index or -1
	const auto Chips = [&](CUIRect &Column, CButtonContainer *pButtons, const char **ppLabels, int Num, int Selected, int PerRow) {
		int Clicked = -1;
		const float Gap = 6.0f, H = 24.0f;
		const int Rows = (Num + PerRow - 1) / PerRow;
		CUIRect Area = Row(Column, Rows * H + (Rows - 1) * Gap);
		const float W = (Area.w - (PerRow - 1) * Gap) / PerRow;
		for(int i = 0; i < Num; i++)
		{
			const CUIRect Chip = {Area.x + (i % PerRow) * (W + Gap), Area.y + (i / PerRow) * (H + Gap), W, H};
			const bool Hot = Ui()->HotItem() == &pButtons[i];
			Graphics()->TextureClear();
			Graphics()->DrawRect(Chip.x, Chip.y, Chip.w, Chip.h, i == Selected ? ColorRGBA(0.85f, 0.9f, 0.97f, 0.24f) : ColorRGBA(1.0f, 1.0f, 1.0f, Hot ? 0.11f : 0.05f), IGraphics::CORNER_ALL, H / 2.0f);
			FogLabel(Ui(), TextRender(), Chip, ppLabels[i], 11.0f, TEXTALIGN_MC, i == Selected ? FROST : MIST, "Manrope", i == Selected ? 800 : 600);
			if(Ui()->DoButtonLogic(&pButtons[i], 0, &Chip, BUTTONFLAG_LEFT))
				Clicked = i;
		}
		return Clicked;
	};
	const auto Pill = [&](CButtonContainer *pButton, const char *pLabel, const CUIRect &Rect) {
		const bool Hot = Ui()->HotItem() == pButton;
		Graphics()->TextureClear();
		Graphics()->DrawRect(Rect.x, Rect.y, Rect.w, Rect.h, ColorRGBA(1.0f, 1.0f, 1.0f, Hot ? 0.14f : 0.07f), IGraphics::CORNER_ALL, Rect.h / 2.0f);
		FogLabel(Ui(), TextRender(), Rect, pLabel, 11.0f, TEXTALIGN_MC, Hot ? FROST : MIST, "Manrope", 700);
		return Ui()->DoButtonLogic(pButton, 0, &Rect, BUTTONFLAG_LEFT) != 0;
	};
	// a small star in the corner of a card; returns true when clicked
	const auto StarRect = [](const CUIRect &Card) { return CUIRect{Card.x + Card.w - 22.0f, Card.y + 4.0f, 18.0f, 18.0f}; };
	const auto Star = [&](const void *pId, const CUIRect &Card, bool On, bool CardHot) {
		const CUIRect Rect = StarRect(Card);
		const bool Hot = Ui()->HotItem() == pId;
		if(On || CardHot || Hot)
			Icon(TextRender(), Ui(), Rect, FontIcon::STAR, Hot ? 10.0f : 9.0f, On ? ColorRGBA(0.98f, 0.86f, 0.55f, 1.0f) : MIST.WithAlpha(Hot ? 1.0f : 0.6f));
		return Ui()->DoButtonLogic(pId, 0, &Rect, BUTTONFLAG_LEFT) != 0;
	};
	const auto ColorLine = [&](CUIRect &Column, CButtonContainer *pReset, const char *pLabel, unsigned *pValue, unsigned Default) {
		DoLine_ColorPicker(pReset, 25.0f, 13.0f, 5.0f, &Column, pLabel, pValue, color_cast<ColorRGBA>(ColorHSLA(Default)), false, nullptr, false);
	};

	CUIRect View = Content;
	if(m_FogStudioTab == 0)
	{
		// ---------------------------------------------------------- skin
		if(DummyLocked)
		{
			Hint(View, Localize("Your dummy copies your skin. Choose \"Its own look\" on the Dummy tab to pick one."));
		}
		static int s_SkinSource = 0; // 0 yours, 1 DDNet database, 2 teedata.net
		{
			static CButtonContainer s_aSourceButtons[3];
			const char *apSources[] = {Localize("Local"), "DDNet", "teedata"};
			const int Source = Chips(View, s_aSourceButtons, apSources, 3, s_SkinSource, 3);
			if(Source >= 0)
				s_SkinSource = Source;
			View.HSplitTop(4.0f, nullptr, &View);
		}
		if(s_SkinSource > 0)
		{
			CFogSkinCatalog &Catalog = GameClient()->m_FogSkinCatalog;
			const int Source = s_SkinSource == 1 ? CFogSkinCatalog::SOURCE_DDNET : CFogSkinCatalog::SOURCE_TEEDATA;
			static int s_aSort[2] = {0, 0};
			static int s_Filter = 0;
			static CButtonContainer s_aSortButtons[5], s_aFilterButtons[4];
			if(Source == CFogSkinCatalog::SOURCE_DDNET)
			{
				CUIRect SortRow = Row(View, 24.0f), SortLeft, SortRight;
				SortRow.VSplitLeft(SortRow.w * 0.36f, &SortLeft, &SortRight);
				SortRight.VSplitLeft(8.0f, nullptr, &SortRight);
				const char *apSorts[] = {Localize("Newest"), Localize("By name")};
				const char *apFilters[] = {Localize("All"), Localize("Normal"), Localize("Community"), "HD"};
				CUIRect LeftCol = SortLeft, RightCol = SortRight;
				const int Sort = Chips(LeftCol, s_aSortButtons, apSorts, 2, s_aSort[0], 2);
				if(Sort >= 0)
					s_aSort[0] = Sort;
				const int Filter = Chips(RightCol, s_aFilterButtons, apFilters, 4, s_Filter, 4);
				if(Filter >= 0)
					s_Filter = Filter;
			}
			else
			{
				const char *apSorts[] = {Localize("Popular"), Localize("Trending"), Localize("Newest"), Localize("By name"), "HD"};
				const int Sort = Chips(View, s_aSortButtons, apSorts, 5, s_aSort[1], 5);
				if(Sort >= 0)
					s_aSort[1] = Sort;
			}

			CUIRect Bottom, Search, Status;
			View.HSplitBottom(24.0f, &View, &Bottom);
			View.HSplitBottom(8.0f, &View, nullptr);
			Bottom.VSplitLeft(Bottom.w * 0.45f, &Search, &Status);
			Status.VSplitLeft(10.0f, nullptr, &Status);

			// search: typed text is sent to teedata only after a short pause
			static CLineInputBuffered<64> s_CatalogSearch;
			static char s_aCommitted[64] = "";
			static float s_LastTyped = 0.0f;
			if(Ui()->DoEditBox_Search(&s_CatalogSearch, &Search, 12.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive()))
				s_LastTyped = Now;
			if(str_comp(s_aCommitted, s_CatalogSearch.GetString()) != 0 && (Source == CFogSkinCatalog::SOURCE_DDNET || Now - s_LastTyped > 0.45f))
				str_copy(s_aCommitted, s_CatalogSearch.GetString());
			Catalog.SetQuery(Source, s_aSort[Source], s_aCommitted, s_Filter);

			std::vector<CFogSkinCatalog::SEntry> &vEntries = Catalog.Entries(Source);
			std::vector<int> &vVisible = Catalog.Visible();
			static CScrollRegion s_CatalogScroll;
			CScrollRegionParams Params;
			Params.m_ScrollUnit = 90.0f;
			s_CatalogScroll.Begin(&View, &Params);
			const int Columns = View.w > 420.0f ? 5 : 4;
			const float Gap = 8.0f, CardW = (View.w - (Columns - 1) * Gap) / Columns, CardH = CardW * 1.05f;
			static std::vector<CButtonContainer> s_vCatalogButtons;
			if(s_vCatalogButtons.size() < vVisible.size())
				s_vCatalogButtons.resize(vVisible.size() + 64);
			static char s_aCatalogStatus[160] = "";
			bool LastVisible = false;
			for(size_t v = 0; v < vVisible.size(); v++)
			{
				CFogSkinCatalog::SEntry &Entry = vEntries[vVisible[v]];
				const CUIRect Card = {View.x + (v % Columns) * (CardW + Gap), View.y + (v / Columns) * (CardH + Gap), CardW, CardH};
				if(!s_CatalogScroll.AddRect(Card))
					continue;
				if(v + 1 == vVisible.size())
					LastVisible = true;
				CButtonContainer *pButton = &s_vCatalogButtons[v];
				const bool Hot = Ui()->HotItem() == pButton;
				const bool Worn = str_comp(pSkinName, Entry.m_Name.c_str()) == 0;
				Graphics()->TextureClear();
				Graphics()->DrawRect(Card.x, Card.y, Card.w, Card.h, Worn ? ColorRGBA(0.85f, 0.9f, 0.97f, 0.2f) : ColorRGBA(1.0f, 1.0f, 1.0f, Hot ? 0.1f : 0.04f), IGraphics::CORNER_ALL, 12.0f);
				const vec2 TeeCenter(Card.x + Card.w / 2.0f, Card.y + Card.h * 0.42f);
				if(const CSkin::CSkinTextures *pTextures = Catalog.Textures(Entry))
				{
					CTeeRenderInfo TeeInfo;
					TeeInfo.m_OriginalRenderSkin = *pTextures;
					TeeInfo.m_ColorableRenderSkin = *pTextures;
					TeeInfo.m_CustomColoredSkin = false;
					TeeInfo.m_Size = CardW * (Hot ? 0.62f : 0.56f);
					TeeInfo.m_Size = CardW * (Hot ? 0.56f : 0.5f);
					RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, Hot ? EMOTE_HAPPY : EMOTE_NORMAL, vec2(1.0f, 0.15f), TeeCenter - vec2(0.0f, CardW * 0.04f));
				}
				else
				{
					// waiting for the picture: a soft breathing dot
					const float r = CardW * (0.12f + 0.03f * std::sin(Now * 4.0f + v));
					Graphics()->DrawRect(TeeCenter.x - r, TeeCenter.y - r, r * 2.0f, r * 2.0f, ColorRGBA(1.0f, 1.0f, 1.0f, Entry.m_State == CFogSkinCatalog::STATE_FAILED ? 0.04f : 0.1f), IGraphics::CORNER_ALL, r);
				}
				CUIRect Label = {Card.x + 4.0f, Card.y + Card.h - 27.0f, Card.w - 8.0f, 13.0f};
				FogLabel(Ui(), TextRender(), Label, Entry.m_Name.c_str(), 9.0f, TEXTALIGN_MC, Worn ? FROST : MIST, "Manrope", Worn ? 800 : 600);
				CUIRect Creator = {Card.x + 4.0f, Card.y + Card.h - 15.0f, Card.w - 8.0f, 11.0f};
				FogLabel(Ui(), TextRender(), Creator, Entry.m_Creator.c_str(), 8.0f, TEXTALIGN_MC, DIM, "Manrope", 500);
				if(Entry.m_Hd)
				{
					CUIRect Badge = {Card.x + 6.0f, Card.y + 6.0f, 20.0f, 11.0f};
					Graphics()->DrawRect(Badge.x, Badge.y, Badge.w, Badge.h, ColorRGBA(1.0f, 1.0f, 1.0f, 0.12f), IGraphics::CORNER_ALL, 5.0f);
					FogLabel(Ui(), TextRender(), Badge, "HD", 7.0f, TEXTALIGN_MC, MIST, "Manrope", 800);
				}
				if(Catalog.Installed(Entry))
				{
					const CUIRect Dot = {Card.x + Card.w - 12.0f, Card.y + Card.h - 12.0f, 6.0f, 6.0f};
					Graphics()->DrawRect(Dot.x, Dot.y, Dot.w, Dot.h, ColorRGBA(0.7f, 0.9f, 0.8f, 0.9f), IGraphics::CORNER_ALL, 3.5f);
				}
				static std::vector<char> s_vStarIds;
				if(s_vStarIds.size() < s_vCatalogButtons.size())
					s_vStarIds.resize(s_vCatalogButtons.size());
				char aFavName[128];
				str_copy(aFavName, Entry.m_Name.c_str());
				str_sanitize_filename(aFavName);
				const bool Favorite = GameClient()->m_Skins.IsFavorite(aFavName);
				bool StarClicked = false;
				const CUIRect FavRect = StarRect(Card);
				const bool CardClicked = !DummyLocked && !Ui()->MouseInside(&FavRect) && Ui()->DoButtonLogic(pButton, 0, &Card, BUTTONFLAG_LEFT);
				if(Star(&s_vStarIds[v], Card, Favorite, Hot))
				{
					StarClicked = true;
					char aInstalled[128], aError[128] = "";
					if(Favorite)
						GameClient()->m_Skins.RemoveFavorite(aFavName);
					else if(Catalog.Install(Entry, aInstalled, sizeof(aInstalled), aError, sizeof(aError)))
					{
						GameClient()->RefreshSkins(CSkinDescriptor::FLAG_SIX);
						GameClient()->m_Skins.AddFavorite(aInstalled);
						str_format(s_aCatalogStatus, sizeof(s_aCatalogStatus), Localize("Added %s to favourites"), aInstalled);
					}
					else
						str_copy(s_aCatalogStatus, aError);
				}
				if(CardClicked && !StarClicked)
				{
					char aInstalled[128], aError[128] = "";
					if(Catalog.Install(Entry, aInstalled, sizeof(aInstalled), aError, sizeof(aError)))
					{
						GameClient()->RefreshSkins(CSkinDescriptor::FLAG_SIX);
						str_copy(pSkinName, aInstalled, SkinNameSize);
						*pUseCustomColor = 0;
						SetNeedSendInfo();
						str_format(s_aCatalogStatus, sizeof(s_aCatalogStatus), Localize("Wearing %s"), aInstalled);
					}
					else
						str_copy(s_aCatalogStatus, aError);
				}
			}
			s_CatalogScroll.End();
			if(LastVisible && Catalog.CanLoadMore())
				Catalog.LoadMore();

			char aStatus[192];
			if(Catalog.Error()[0])
				str_copy(aStatus, Catalog.Error());
			else if(Catalog.Loading() && vVisible.empty())
				str_copy(aStatus, Localize("Loading the catalogue..."));
			else if(s_aCatalogStatus[0])
				str_copy(aStatus, s_aCatalogStatus);
			else
				str_format(aStatus, sizeof(aStatus), Localize("%d skins"), Catalog.Total());
			FogLabel(Ui(), TextRender(), Status, aStatus, 10.0f, TEXTALIGN_ML, DIM, "Manrope", 600);
		}
		else
		{
		// 0 yours (installed from the catalogue or drawn), 1 shipped with the game,
		// 2 downloaded automatically when you met someone wearing it, 3 favourites
		static int s_LocalFilter = 0;
		{
			static CButtonContainer s_aFilterButtons[4];
			const char *apFilters[] = {Localize("Mine"), Localize("Built-in"), Localize("Met in game"), Localize("Favorites")};
			const int Filter = Chips(View, s_aFilterButtons, apFilters, 4, s_LocalFilter, 4);
			if(Filter >= 0)
			{
				s_LocalFilter = Filter;
				m_SkinListScrollToSelected = true;
			}
			// skins met earlier sit in downloadedskins/ but the game only lists the ones seen this
			// session: register them all once so this filter shows the whole folder
			static bool s_MetScanned = false;
			if(s_LocalFilter == 2 && !s_MetScanned)
			{
				s_MetScanned = true;
				std::vector<std::string> vNames;
				Storage()->ListDirectory(IStorage::TYPE_SAVE, "downloadedskins", [](const char *pName, int IsDir, int, void *pUser) {
					if(!IsDir && str_endswith(pName, ".png"))
						static_cast<std::vector<std::string> *>(pUser)->emplace_back(pName, str_length(pName) - 4);
					return 0;
				},
					&vNames);
				for(const std::string &Name : vNames)
					GameClient()->m_Skins.FindContainerOrNullptr(Name.c_str());
				GameClient()->m_Skins.SkinList().ForceRefresh();
			}
		}
		const auto PassesFilter = [&](const CSkins::CSkinListEntry &Entry) {
			const CSkins::CSkinContainer *pContainer = Entry.SkinContainer();
			const bool Local = pContainer->Type() == CSkins::CSkinContainer::EType::LOCAL;
			switch(s_LocalFilter)
			{
			case 0: return Local && pContainer->StorageType() == IStorage::TYPE_SAVE;
			case 1: return Local && pContainer->StorageType() != IStorage::TYPE_SAVE;
			case 2: return !Local;
			default: return Entry.IsFavorite();
			}
		};
		CUIRect Bottom, Search, Random, Folder, Database;
		View.HSplitBottom(24.0f, &View, &Bottom);
		View.HSplitBottom(10.0f, &View, nullptr);
		Bottom.VSplitLeft(Bottom.w * 0.45f, &Search, &Bottom);
		const float ButtonW = (Bottom.w - 3 * 6.0f) / 3.0f;
		Bottom.VSplitLeft(6.0f, nullptr, &Bottom);
		Bottom.VSplitLeft(ButtonW, &Random, &Bottom);
		Bottom.VSplitLeft(6.0f, nullptr, &Bottom);
		Bottom.VSplitLeft(ButtonW, &Folder, &Bottom);
		Bottom.VSplitLeft(6.0f, nullptr, &Database);

		CSkins::CSkinList &SkinList = GameClient()->m_Skins.SkinList();
		std::vector<CSkins::CSkinListEntry> &vSkinList = SkinList.Skins();
		const CSkin *pDefaultSkin = GameClient()->m_Skins.Find("default");

		static CScrollRegion s_SkinScroll;
		CScrollRegionParams Params;
		Params.m_ScrollUnit = 90.0f;
		s_SkinScroll.Begin(&View, &Params);
		const int Columns = View.w > 420.0f ? 5 : 4;
		const float Gap = 8.0f, CardW = (View.w - (Columns - 1) * Gap) / Columns, CardH = CardW * 0.95f;
		int Shown = 0;
		static std::vector<CButtonContainer> s_vSkinButtons;
		if(s_vSkinButtons.size() < vSkinList.size())
			s_vSkinButtons.resize(vSkinList.size() + 64);
		CTeeRenderInfo Base;
		Base.ApplyColors(*pUseCustomColor, *pColorBody, *pColorFeet);
		int NewSelected = -1;
		for(size_t i = 0; i < vSkinList.size(); i++)
		{
			CSkins::CSkinListEntry &Entry = vSkinList[i];
			const CSkins::CSkinContainer *pContainer = Entry.SkinContainer();
			if(!PassesFilter(Entry))
				continue;
			const CUIRect Card = {View.x + (Shown % Columns) * (CardW + Gap), View.y + (Shown / Columns) * (CardH + Gap), CardW, CardH};
			Shown++;
			const bool Selected = !Dummy ? Entry.IsSelectedMain() : Entry.IsSelectedDummy();
			const bool Visible = s_SkinScroll.AddRect(Card);
			if(Selected && m_SkinListScrollToSelected)
			{
				s_SkinScroll.ScrollHere(CScrollRegion::SCROLLHERE_KEEP_IN_VIEW);
				m_SkinListScrollToSelected = false;
			}
			if(!Visible)
				continue;
			Entry.RequestLoad();
			const CSkin *pSkin = pContainer->State() == CSkins::CSkinContainer::EState::LOADED ? pContainer->Skin().get() : pDefaultSkin;
			CButtonContainer *pButton = &s_vSkinButtons[i];
			const bool Hot = Ui()->HotItem() == pButton;
			Graphics()->TextureClear();
			Graphics()->DrawRect(Card.x, Card.y, Card.w, Card.h, Selected ? ColorRGBA(0.85f, 0.9f, 0.97f, 0.2f) : ColorRGBA(1.0f, 1.0f, 1.0f, Hot ? 0.1f : 0.04f), IGraphics::CORNER_ALL, 12.0f);
			if(Selected)
				Graphics()->DrawRect(Card.x + 12.0f, Card.y + 0.5f, Card.w - 24.0f, 1.5f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), IGraphics::CORNER_NONE, 0.0f);
			CTeeRenderInfo TeeInfo = Base;
			TeeInfo.Apply(pSkin);
			TeeInfo.m_Size = CardW * (Hot ? 0.62f : 0.56f);
			RenderTools()->RenderIdleTeeAtCenter(&TeeInfo, vec2(Card.x + Card.w / 2.0f, Card.y + Card.h * 0.44f), Hot ? EMOTE_HAPPY : EMOTE_NORMAL);
			CUIRect Label = {Card.x + 4.0f, Card.y + Card.h - 18.0f, Card.w - 8.0f, 14.0f};
			FogLabel(Ui(), TextRender(), Label, pContainer->Name(), 9.0f, TEXTALIGN_MC, Selected ? FROST : MIST, "Manrope", Selected ? 800 : 600);
			const CUIRect FavRect = StarRect(Card);
			if(!DummyLocked && !Ui()->MouseInside(&FavRect) && Ui()->DoButtonLogic(pButton, 0, &Card, BUTTONFLAG_LEFT))
				NewSelected = (int)i;
			if(Star(Entry.FavoriteButtonId(), Card, Entry.IsFavorite(), Hot))
			{
				if(Entry.IsFavorite())
					GameClient()->m_Skins.RemoveFavorite(pContainer->Name());
				else
					GameClient()->m_Skins.AddFavorite(pContainer->Name());
				SkinList.ForceRefresh();
			}
		}
		s_SkinScroll.End();
		if(Shown == 0 && SkinList.UnfilteredCount() > 0)
		{
			const char *apEmpty[] = {
				Localize("Skins you install from DDNet or teedata, or draw yourself, appear here."),
				Localize("No built-in skins match."),
				Localize("Skins of players you meet are downloaded here automatically."),
				Localize("Tap the star on a skin to keep it here."),
			};
			CUIRect Empty = {View.x, View.y + 40.0f, View.w, 16.0f};
			FogLabel(Ui(), TextRender(), Empty, apEmpty[s_LocalFilter], 11.0f, TEXTALIGN_MC, DIM, "Manrope", 600);
		}
		if(NewSelected >= 0)
		{
			str_copy(pSkinName, vSkinList[NewSelected].SkinContainer()->Name(), SkinNameSize);
			SkinList.ForceRefresh();
			SetNeedSendInfo();
		}

		static CLineInput s_SkinFilterInput(g_Config.m_ClSkinFilterString, sizeof(g_Config.m_ClSkinFilterString));
		if(Ui()->DoEditBox_Search(&s_SkinFilterInput, &Search, 12.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive()))
			SkinList.ForceRefresh();
		static CButtonContainer s_RandomButton, s_FolderButton, s_DatabaseButton;
		if(!DummyLocked && Pill(&s_RandomButton, Localize("Random"), Random))
		{
			GameClient()->m_Skins.RandomizeSkin(m_Dummy);
			SkinList.ForceRefresh();
			m_SkinListScrollToSelected = true;
		}
		if(Pill(&s_FolderButton, Localize("Folder"), Folder))
		{
			char aBuf[IO_MAX_PATH_LENGTH];
			Storage()->CreateFolder("skins", IStorage::TYPE_SAVE);
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, "skins", aBuf, sizeof(aBuf));
			Client()->ViewFile(aBuf);
		}
		if(Pill(&s_DatabaseButton, Localize("Skin Database"), Database))
			Client()->ViewLink("https://ddnet.org/skins/");
		}
	}
	else if(m_FogStudioTab == 5)
	{
		// ---------------------------------------------------------- draw your own tee
		CFogTeePainter &Painter = GameClient()->m_FogPainter;
		Painter.Start(pSkinName);
		CUIRect Left, Right;
		View.VSplitLeft(View.w * 0.54f, &Left, &Right);
		Right.VSplitLeft(14.0f, nullptr, &Right);

		// part and tool
		static CButtonContainer s_aPartButtons[2], s_aToolButtons[4];
		const char *apParts[] = {Localize("Body"), Localize("Feet")};
		const int Part = Chips(Left, s_aPartButtons, apParts, 2, Painter.m_Part, 2);
		if(Part >= 0)
			Painter.m_Part = Part;
		const char *apTools[] = {Localize("Brush"), Localize("Eraser"), Localize("Fill"), Localize("Picker")};
		const int Tool = Chips(Left, s_aToolButtons, apTools, 4, Painter.m_Tool, 4);
		if(Tool >= 0)
			Painter.m_Tool = Tool;
		Left.HSplitTop(4.0f, nullptr, &Left);

		// the canvas
		const float PartW = (float)Painter.Width(Painter.m_Part), PartH = (float)Painter.Height(Painter.m_Part);
		CUIRect Bottom;
		Left.HSplitBottom(22.0f, &Left, &Bottom);
		Left.HSplitBottom(8.0f, &Left, nullptr);
		const float Scale = std::min(Left.w / PartW, Left.h / PartH);
		const CUIRect Canvas = {Left.x + (Left.w - PartW * Scale) / 2.0f, Left.y + (Left.h - PartH * Scale) / 2.0f, PartW * Scale, PartH * Scale};
		Graphics()->TextureClear();
		Graphics()->DrawRect(Canvas.x - 6.0f, Canvas.y - 6.0f, Canvas.w + 12.0f, Canvas.h + 12.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);
		{
			// quiet checkerboard so transparency reads
			const int Cells = 8;
			const float CellW = Canvas.w / (Cells * PartW / PartH), CellH = Canvas.h / Cells;
			Graphics()->QuadsBegin();
			for(int y = 0; y < Cells; y++)
				for(int x = 0; x * CellW < Canvas.w - 0.01f; x++)
				{
					const float Shade = (x + y) % 2 ? 0.16f : 0.2f;
					Graphics()->SetColor(Shade, Shade + 0.01f, Shade + 0.03f, 1.0f);
					IGraphics::CQuadItem Cell(Canvas.x + x * CellW, Canvas.y + y * CellH, std::min(CellW, Canvas.x + Canvas.w - (Canvas.x + x * CellW)), CellH);
					Graphics()->QuadsDrawTL(&Cell, 1);
				}
			Graphics()->QuadsEnd();
		}
		Graphics()->TextureSet(Painter.CanvasTexture(Painter.m_Part));
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
		IGraphics::CQuadItem CanvasItem(Canvas.x, Canvas.y, Canvas.w, Canvas.h);
		Graphics()->QuadsDrawTL(&CanvasItem, 1);
		Graphics()->QuadsEnd();
		if(Painter.m_Mirror)
		{
			Graphics()->TextureClear();
			Graphics()->DrawRect(Canvas.x + Canvas.w / 2.0f - 0.5f, Canvas.y, 1.0f, Canvas.h, ColorRGBA(1.0f, 1.0f, 1.0f, 0.12f), IGraphics::CORNER_NONE, 0.0f);
		}

		// painting
		static CButtonContainer s_PaintId;
		static vec2 s_LastPaint;
		const vec2 Mouse = Ui()->MousePos();
		const vec2 PaintPos((Mouse.x - Canvas.x) / Scale, (Mouse.y - Canvas.y) / Scale);
		if(Ui()->CheckActiveItem(&s_PaintId))
		{
			if(Ui()->MouseButton(0))
			{
				Painter.Stroke(s_LastPaint, PaintPos);
				s_LastPaint = PaintPos;
			}
			else
			{
				Painter.EndStroke();
				Ui()->SetActiveItem(nullptr);
			}
		}
		else if(Ui()->MouseInside(&Canvas) && Ui()->MouseButtonClicked(0) && !Ui()->ActiveItem())
		{
			if(Painter.m_Tool == CFogTeePainter::TOOL_FILL)
				Painter.Fill(PaintPos);
			else if(Painter.m_Tool == CFogTeePainter::TOOL_PICKER)
				Painter.Pick(PaintPos);
			else
			{
				Painter.BeginStroke();
				Painter.Stroke(PaintPos, PaintPos);
				s_LastPaint = PaintPos;
				Ui()->SetActiveItem(&s_PaintId);
			}
		}
		// brush outline under the cursor
		if(Ui()->MouseInside(&Canvas) && (Painter.m_Tool == CFogTeePainter::TOOL_BRUSH || Painter.m_Tool == CFogTeePainter::TOOL_ERASER))
		{
			const float r = std::max(Painter.m_Size * 0.5f * Scale, 2.0f);
			Graphics()->TextureClear();
			Graphics()->DrawRect(Mouse.x - r, Mouse.y - r, r * 2.0f, r * 2.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.14f), IGraphics::CORNER_ALL, r);
		}
		if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_Z))
			Input()->ShiftIsPressed() ? Painter.Redo() : Painter.Undo();
		if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_Y))
			Painter.Redo();

		// undo / redo / clear / start over
		{
			static CButtonContainer s_Undo, s_Redo, s_Clear, s_New, s_FromSkin;
			const float W = (Bottom.w - 4 * 6.0f) / 5.0f;
			CUIRect b = Bottom;
			CUIRect aRects[5];
			for(auto &Rect : aRects)
			{
				b.VSplitLeft(W, &Rect, &b);
				b.VSplitLeft(6.0f, nullptr, &b);
			}
			if(Pill(&s_Undo, Localize("Undo"), aRects[0]))
				Painter.Undo();
			if(Pill(&s_Redo, Localize("Redo"), aRects[1]))
				Painter.Redo();
			if(Pill(&s_Clear, Localize("Clear"), aRects[2]))
				Painter.ClearPart();
			if(Pill(&s_New, Localize("Blank"), aRects[3]))
				Painter.NewBlank();
			if(Pill(&s_FromSkin, Localize("From skin"), aRects[4]))
				Painter.Import(pSkinName[0] ? pSkinName : "default");
		}

		// brush settings
		CUIRect Column = Right;
		static CButtonContainer s_BrushColorReset;
		ColorLine(Column, &s_BrushColorReset, Localize("Colour"), &Painter.m_Color, ColorHSLA(0.58f, 0.18f, 0.78f).Pack(false));
		Slider(Column, &Painter.m_Size, Localize("Size", "Tee studio"), 1, 40, "px");
		Slider(Column, &Painter.m_Hardness, Localize("Hardness"), 0, 100, "%");
		Slider(Column, &Painter.m_Opacity, Localize("Opacity", "Tee studio"), 5, 100, "%");
		CheckBox(Column, &Painter.m_Mirror, Localize("Mirror both halves"));
		CheckBox(Column, &Painter.m_LockShape, Localize("Stay inside the tee shape"));
		if(CheckBox(Column, &Painter.m_Shading, Localize("Soft tee shading")))
			Painter.MarkDirty();
		const int OldOutline = Painter.m_Outline;
		Slider(Column, &Painter.m_Outline, Localize("Outline"), 0, 6, "px");
		if(OldOutline != Painter.m_Outline)
			Painter.MarkDirty();
		Column.HSplitTop(8.0f, nullptr, &Column);

		// save as a skin
		Headline(Column, Localize("Save as skin"));
		static CLineInputBuffered<24> s_NameInput;
		static bool s_NameInit = false;
		if(!s_NameInit)
		{
			s_NameInput.Set("my_tee");
			s_NameInit = true;
		}
		CUIRect NameRow = Row(Column, LINE);
		Ui()->DoEditBox(&s_NameInput, &NameRow, 12.0f);
		static CButtonContainer s_SaveButton;
		static char s_aSaveStatus[128] = "";
		CUIRect SaveRow = Row(Column, 24.0f);
		if(Pill(&s_SaveButton, Localize("Save and wear"), SaveRow))
		{
			char aError[128] = "";
			if(Painter.Save(s_NameInput.GetString(), aError, sizeof(aError)))
			{
				GameClient()->m_FogTeeStyle.Invalidate();
				GameClient()->RefreshSkins(CSkinDescriptor::FLAG_SIX);
				str_copy(pSkinName, s_NameInput.GetString(), SkinNameSize);
				*pUseCustomColor = 0; // show the painted colours, not one game colour
				SetNeedSendInfo();
				str_format(s_aSaveStatus, sizeof(s_aSaveStatus), Localize("Saved to skins/%s.png"), s_NameInput.GetString());
			}
			else
				str_copy(s_aSaveStatus, aError);
		}
		if(s_aSaveStatus[0])
			Hint(Column, s_aSaveStatus);
		Hint(Column, Localize("Others see it only if they have the same file."));
	}
	else
	{
		static CScrollRegion s_TabScroll;
		CScrollRegionParams Params;
		Params.m_ScrollUnit = 60.0f;
		CUIRect Column = View;
		s_TabScroll.Begin(&View, &Params);
		Column.y = View.y;
		const float StartY = Column.y;

		if(m_FogStudioTab == 1)
		{
			// ---------------------------------------------------------- colours
			Headline(Column, Localize("Colours everyone sees"));
			if(DummyLocked)
				Hint(Column, Localize("Your dummy copies your colours (Dummy tab)."));
			else
			{
				if(CheckBox(Column, pUseCustomColor, Localize("Custom colors")))
					SetNeedSendInfo();
				if(*pUseCustomColor)
				{
					CUIRect Colors = Row(Column, 78.0f), Left, Right, Label;
					Colors.VSplitMid(&Left, &Right, 16.0f);
					unsigned *apColors[] = {pColorBody, pColorFeet};
					CUIRect *apRects[] = {&Left, &Right};
					const char *apParts[] = {Localize("Body"), Localize("Feet")};
					for(int i = 0; i < 2; i++)
					{
						apRects[i]->HSplitTop(18.0f, &Label, apRects[i]);
						FogLabel(Ui(), TextRender(), Label, apParts[i], 12.0f, TEXTALIGN_ML, MIST, "Manrope", 700);
						if(RenderHslaScrollbars(apRects[i], apColors[i], false, ColorHSLA::DARKEST_LGT))
							SetNeedSendInfo();
					}
				}
			}
			Column.HSplitTop(12.0f, nullptr, &Column);

			Headline(Column, Localize("Animated colours"));
			Hint(Column, Localize("Drawn only by your client, on top of your skin."));
			CheckBox(Column, &g_Config.m_FogTeeStyle, Localize("Animated look for my tee"));
			static CButtonContainer s_aModeButtons[6];
			const char *apModes[] = {Localize("Skin colours"), Localize("Breathe"), Localize("Flow"), Localize("Rainbow"), Localize("Aurora"), Localize("Pulse")};
			const int Mode = Chips(Column, s_aModeButtons, apModes, 6, g_Config.m_FogTeeColorMode, 3);
			if(Mode >= 0)
			{
				g_Config.m_FogTeeColorMode = Mode;
				g_Config.m_FogTeeStyle = 1;
			}
			static CButtonContainer s_ResetColor1, s_ResetColor2;
			ColorLine(Column, &s_ResetColor1, Localize("First colour"), &g_Config.m_FogTeeColor1, 9985743);
			ColorLine(Column, &s_ResetColor2, Localize("Second colour"), &g_Config.m_FogTeeColor2, 16021181);
			Slider(Column, &g_Config.m_FogTeeColorSpeed, Localize("Speed", "Tee studio"), 0, 500, "%");
			CUIRect FeetLabel = Row(Column, 16.0f);
			FogLabel(Ui(), TextRender(), FeetLabel, Localize("Feet"), 12.0f, TEXTALIGN_ML, MIST, "Manrope", 700);
			static CButtonContainer s_aFeetButtons[4];
			const char *apFeet[] = {Localize("Like the body"), Localize("Game colour"), Localize("Second colour"), Localize("Inverted")};
			const int FeetMode = Chips(Column, s_aFeetButtons, apFeet, 4, g_Config.m_FogTeeFeetMode, 2);
			if(FeetMode >= 0)
				g_Config.m_FogTeeFeetMode = FeetMode;
		}
		else if(m_FogStudioTab == 2)
		{
			// ---------------------------------------------------------- pattern
			Headline(Column, Localize("Pattern"));
			Hint(Column, Localize("Moves over the body. Drawn only by your client."));
			static CButtonContainer s_aPatternButtons[10];
			const char *apPatterns[] = {Localize("None", "Tee studio"), Localize("Stripes"), Localize("Dots"), Localize("Waves"), Localize("Checks"), Localize("Zigzag"), Localize("Sparkles"), Localize("Marble"), Localize("Rings"), Localize("My image")};
			const int Pattern = Chips(Column, s_aPatternButtons, apPatterns, 10, g_Config.m_FogTeePattern, 5);
			if(Pattern >= 0)
			{
				g_Config.m_FogTeePattern = Pattern;
				if(Pattern != 0)
					g_Config.m_FogTeeStyle = 1;
			}
			Column.HSplitTop(6.0f, nullptr, &Column);
			if(g_Config.m_FogTeePattern == 9)
			{
				const std::vector<std::string> &vFiles = GameClient()->m_FogTeeStyle.PatternFiles();
				if(vFiles.empty())
					Hint(Column, Localize("Put PNG images into the fog/patterns folder, then refresh."));
				else
				{
					static std::vector<CButtonContainer> s_vFileButtons;
					s_vFileButtons.resize(vFiles.size());
					std::vector<const char *> vpNames;
					int Selected = -1;
					for(size_t i = 0; i < vFiles.size(); i++)
					{
						vpNames.push_back(vFiles[i].c_str());
						if(vFiles[i] == g_Config.m_FogTeePatternFile)
							Selected = (int)i;
					}
					const int File = Chips(Column, s_vFileButtons.data(), vpNames.data(), (int)vpNames.size(), Selected, 3);
					if(File >= 0)
						str_copy(g_Config.m_FogTeePatternFile, vFiles[File].c_str());
				}
				CUIRect Buttons = Row(Column, LINE), OpenButton, RefreshButton;
				Buttons.VSplitMid(&OpenButton, &RefreshButton, 8.0f);
				static CButtonContainer s_OpenPatterns, s_RefreshPatterns;
				if(Pill(&s_OpenPatterns, Localize("Open folder"), OpenButton))
				{
					GameClient()->m_FogTeeStyle.ScanPatterns(); // creates the folder
					char aBuf[IO_MAX_PATH_LENGTH];
					Storage()->GetCompletePath(IStorage::TYPE_SAVE, "fog/patterns", aBuf, sizeof(aBuf));
					Client()->ViewFile(aBuf);
				}
				if(Pill(&s_RefreshPatterns, Localize("Refresh"), RefreshButton))
					GameClient()->m_FogTeeStyle.ScanPatterns();
				Column.HSplitTop(6.0f, nullptr, &Column);
			}
			else
			{
				static CButtonContainer s_ResetPatternColor;
				ColorLine(Column, &s_ResetPatternColor, Localize("Pattern colour"), &g_Config.m_FogTeePatternColor, 10485754);
			}
			Slider(Column, &g_Config.m_FogTeePatternOpacity, Localize("Opacity", "Tee studio"), 0, 100, "%");
			Slider(Column, &g_Config.m_FogTeePatternScale, Localize("Size", "Tee studio"), 20, 400, "%");
			Slider(Column, &g_Config.m_FogTeePatternAngle, Localize("Angle", "Tee studio"), 0, 359, "°");
			Slider(Column, &g_Config.m_FogTeePatternSpeed, Localize("Movement", "Tee studio"), -300, 300, "%");
			Slider(Column, &g_Config.m_FogTeePatternSoft, Localize("Soft edges"), 0, 100, "%");
		}
		else if(m_FogStudioTab == 4)
		{
			// ---------------------------------------------------------- accessories
			std::vector<SFogAccessory> &vWorn = Accessories.Worn(AccSlot);
			static int s_AccSection = 0;
			static CButtonContainer s_aSectionButtons[2];
			const char *apSections[] = {Localize("Things"), Localize("Auras")};
			const int Section = Chips(Column, s_aSectionButtons, apSections, 2, s_AccSection, 2);
			if(Section >= 0)
				s_AccSection = Section;
			Column.HSplitTop(6.0f, nullptr, &Column);
			if(s_AccSection == 1)
			{
				Headline(Column, Localize("Aura"));
				Hint(Column, Localize("Light, shadow or particles around your tee. Drawn only by your client."));
				static CButtonContainer s_aAuraButtons[7];
				const char *apAuras[] = {Localize("None", "Tee studio"), Localize("Glow"), Localize("Dark aura"), Localize("Mist"), Localize("Embers"), Localize("Stardust"), Localize("Rings")};
				const int Aura = Chips(Column, s_aAuraButtons, apAuras, 7, g_Config.m_FogTeeAura, 4);
				if(Aura >= 0)
					g_Config.m_FogTeeAura = Aura;
				static CButtonContainer s_AuraColorReset;
				ColorLine(Column, &s_AuraColorReset, Localize("Colour"), &g_Config.m_FogTeeAuraColor, 10203861);
				CheckBox(Column, &g_Config.m_FogTeeAuraRainbow, Localize("Drift through the rainbow"));
				Slider(Column, &g_Config.m_FogTeeAuraStrength, Localize("Strength"), 0, 200, "%");
				Slider(Column, &g_Config.m_FogTeeAuraSize, Localize("Size", "Tee studio"), 50, 200, "%");
				Slider(Column, &g_Config.m_FogTeeAuraSpeed, Localize("Speed", "Tee studio"), 0, 300, "%");
				Column.HSplitTop(10.0f, nullptr, &Column);
				Headline(Column, Localize("Outline"));
				CheckBox(Column, &g_Config.m_FogTeeOutline, Localize("Coloured outline around the tee"));
				static CButtonContainer s_OutlineColorReset;
				ColorLine(Column, &s_OutlineColorReset, Localize("Colour"), &g_Config.m_FogTeeOutlineColor, 10321893);
				Slider(Column, &g_Config.m_FogTeeOutlineWidth, Localize("Thickness"), 1, 10, "");
				static CButtonContainer s_aOutlineModes[4];
				const char *apModes[] = {Localize("Steady"), Localize("Pulse"), Localize("Rainbow"), Localize("Flicker")};
				const int OutlineMode = Chips(Column, s_aOutlineModes, apModes, 4, g_Config.m_FogTeeOutlineMode, 4);
				if(OutlineMode >= 0)
				{
					g_Config.m_FogTeeOutlineMode = OutlineMode;
					g_Config.m_FogTeeOutline = 1;
				}
				if((g_Config.m_FogTeeAura || g_Config.m_FogTeeOutline) && !g_Config.m_FogTeeAccessoriesOn)
					g_Config.m_FogTeeAccessoriesOn = 1;
			}
			else
			{
			Headline(Column, Localize("Accessories"));
			Hint(Column, Localize("Drawn only by your client. Up to 8 at once."));
			if(Dummy && g_Config.m_FogDummySync)
				Hint(Column, Localize("Your dummy wears your accessories (Dummy tab)."));
			CheckBox(Column, &g_Config.m_FogTeeAccessoriesOn, Localize("Show accessories on my tees"));
			Slider(Column, &g_Config.m_FogTeeAccessoriesAnim, Localize("Animation"), 0, 300, "%");
			Column.HSplitTop(6.0f, nullptr, &Column);

			// catalogue: click to put on, or drag onto the tee
			int Num;
			const SFogAccessoryDef *pDefs = CFogAccessories::Catalogue(&Num);
			const int Columns = 4;
			const float Gap = 8.0f, CardW = (Column.w - (Columns - 1) * Gap) / Columns, CardH = CardW * 0.92f;
			const int Rows = (Num + Columns - 1) / Columns;
			CUIRect Grid = Row(Column, Rows * CardH + (Rows - 1) * Gap);
			static CButtonContainer s_aCardButtons[32];
			for(int d = 0; d < Num && d < 32; d++)
			{
				const CUIRect Card = {Grid.x + (d % Columns) * (CardW + Gap), Grid.y + (d / Columns) * (CardH + Gap), CardW, CardH};
				const bool Hot = Ui()->MouseHovered(&Card) && !s_Carrying;
				Graphics()->TextureClear();
				Graphics()->DrawRect(Card.x, Card.y, Card.w, Card.h, ColorRGBA(0.0f, 0.0f, 0.0f, Hot ? 0.28f : 0.18f), IGraphics::CORNER_ALL, 12.0f);
				const float IconSize = CardW * (Hot ? 0.66f : 0.6f);
				Accessories.DrawIcon(d, vec2(Card.x + Card.w / 2.0f, Card.y + Card.h * 0.43f), IconSize, 1.0f);
				CUIRect Label = {Card.x + 3.0f, Card.y + Card.h - 16.0f, Card.w - 6.0f, 12.0f};
				FogLabel(Ui(), TextRender(), Label, Localize(pDefs[d].m_pName), 9.0f, TEXTALIGN_MC, Hot ? FROST : MIST, "Manrope", 600);
				if(Hot && Ui()->MouseButtonClicked(0) && s_CarryDef < 0 && (int)vWorn.size() < CFogAccessories::MAX_WORN)
				{
					s_CarryDef = d;
					s_CarryStart = Ui()->MousePos();
					s_Carrying = false;
				}
			}
			Column.HSplitTop(8.0f, nullptr, &Column);

			// what you wear
			if(!vWorn.empty())
			{
				CUIRect WornLabel = Row(Column, 16.0f);
				FogLabel(Ui(), TextRender(), WornLabel, Localize("Wearing"), 12.0f, TEXTALIGN_ML, MIST, "Manrope", 700);
				static CButtonContainer s_aWornButtons[CFogAccessories::MAX_WORN];
				std::vector<const char *> vpNames;
				for(const SFogAccessory &Acc : vWorn)
					vpNames.push_back(Localize(pDefs[Acc.m_Def].m_pName));
				const int Picked = Chips(Column, s_aWornButtons, vpNames.data(), (int)vpNames.size(), m_FogStudioAccessory, 4);
				if(Picked >= 0)
					m_FogStudioAccessory = Picked;
			}
			if(m_FogStudioAccessory >= 0 && m_FogStudioAccessory < (int)vWorn.size())
			{
				SFogAccessory &Acc = vWorn[m_FogStudioAccessory];
				bool Changed = false;
				Column.HSplitTop(4.0f, nullptr, &Column);
				static CButtonContainer s_AccColorReset, s_AccGlowReset;
				const unsigned OldColor = Acc.m_Color, OldGlow = Acc.m_GlowColor;
				ColorLine(Column, &s_AccColorReset, Localize("Colour"), &Acc.m_Color, pDefs[Acc.m_Def].m_Color);
				ColorLine(Column, &s_AccGlowReset, Localize("Glow"), &Acc.m_GlowColor, pDefs[Acc.m_Def].m_GlowColor);
				Changed |= OldColor != Acc.m_Color || OldGlow != Acc.m_GlowColor;
				static int s_Size, s_Angle;
				s_Size = (int)std::lround(Acc.m_Size * 100.0f);
				s_Angle = (int)std::lround(Acc.m_Angle);
				const int OldSize = s_Size, OldAngle = s_Angle;
				Slider(Column, &s_Size, Localize("Size", "Tee studio"), 8, 250, "%");
				Slider(Column, &s_Angle, Localize("Angle", "Tee studio"), -180, 180, "°");
				if(s_Size != OldSize || s_Angle != OldAngle)
				{
					Acc.m_Size = s_Size / 100.0f;
					Acc.m_Angle = (float)s_Angle;
					Changed = true;
				}
				CUIRect Toggles = Row(Column, LINE), AnimToggle, MirrorToggle;
				Toggles.VSplitMid(&AnimToggle, &MirrorToggle, 10.0f);
				static int s_Animate, s_Mirror;
				s_Animate = Acc.m_Animate;
				s_Mirror = Acc.m_Mirror;
				if(DoButton_CheckBox(&s_Animate, Localize("Animated"), s_Animate, &AnimToggle))
				{
					Acc.m_Animate = !Acc.m_Animate;
					Changed = true;
				}
				if(DoButton_CheckBox(&s_Mirror, Localize("Mirror"), s_Mirror, &MirrorToggle))
				{
					Acc.m_Mirror = !Acc.m_Mirror;
					Changed = true;
				}
				CUIRect Buttons = Row(Column, 22.0f), ResetButton, RemoveButton;
				Buttons.VSplitMid(&ResetButton, &RemoveButton, 8.0f);
				static CButtonContainer s_ResetPlace, s_Remove;
				if(Pill(&s_ResetPlace, Localize("Back to its place"), ResetButton))
				{
					const SFogAccessory Fresh = Accessories.Make(Acc.m_Def, pDefs[Acc.m_Def].m_X, pDefs[Acc.m_Def].m_Y);
					Acc.m_X = Fresh.m_X;
					Acc.m_Y = Fresh.m_Y;
					Acc.m_Size = Fresh.m_Size;
					Acc.m_Angle = 0.0f;
					Changed = true;
				}
				if(Pill(&s_Remove, Localize("Take off"), RemoveButton))
				{
					vWorn.erase(vWorn.begin() + m_FogStudioAccessory);
					m_FogStudioAccessory = -1;
					Changed = true;
				}
				if(Changed)
					Accessories.Save(AccSlot);
			}
			}
		}
		else
		{
			// ---------------------------------------------------------- dummy
			Headline(Column, Localize("Your dummy"));
			Hint(Column, Localize("The dummy's skin and colours are sent to the server, so everyone sees them."));
			static CButtonContainer s_aSyncButtons[3];
			const char *apTitles[] = {Localize("Its own look"), Localize("Same as you"), Localize("Inverted colours")};
			const char *apText[] = {
				Localize("Pick its skin and colours with the You / Dummy switch under the tee."),
				Localize("Copies your skin, colours and animated look."),
				Localize("Your skin with the opposite colours, easy to tell apart."),
			};
			const char *apIcons[] = {FontIcon::USER, FontIcon::LAYER_GROUP, FontIcon::ARROWS_LEFT_RIGHT};
			for(int i = 0; i < 3; i++)
			{
				CUIRect Card = Row(Column, 52.0f), CardIcon, Text, Title, Desc;
				const bool Selected = g_Config.m_FogDummySync == i;
				const bool Hot = Ui()->HotItem() == &s_aSyncButtons[i];
				Graphics()->TextureClear();
				Graphics()->DrawRect(Card.x, Card.y, Card.w, Card.h, Selected ? ColorRGBA(0.85f, 0.9f, 0.97f, 0.2f) : ColorRGBA(1.0f, 1.0f, 1.0f, Hot ? 0.09f : 0.04f), IGraphics::CORNER_ALL, 14.0f);
				Card.VSplitLeft(46.0f, &CardIcon, &Text);
				Icon(TextRender(), Ui(), CardIcon, apIcons[i], 14.0f, Selected ? FROST : MIST);
				Text.HMargin(8.0f, &Text);
				Text.HSplitTop(20.0f, &Title, &Desc);
				FogLabel(Ui(), TextRender(), Title, apTitles[i], 13.0f, TEXTALIGN_ML, Selected ? FROST : MIST, "Manrope", 800);
				FogLabel(Ui(), TextRender(), Desc, apText[i], 10.0f, TEXTALIGN_ML, DIM, "Manrope", 500);
				if(Ui()->DoButtonLogic(&s_aSyncButtons[i], 0, &Card, BUTTONFLAG_LEFT))
				{
					g_Config.m_FogDummySync = i;
					if(i == 0)
						m_SkinListScrollToSelected = true;
				}
				Column.HSplitTop(2.0f, nullptr, &Column);
			}
		}

		CUIRect Used = {View.x, StartY, View.w, Column.y - StartY};
		s_TabScroll.AddRect(Used);
		s_TabScroll.End();
	}

	// a piece carried from the catalogue: follows the mouse, put on where it is dropped
	if(s_CarryDef >= 0)
	{
		const vec2 Mouse = Ui()->MousePos();
		if(distance(Mouse, s_CarryStart) > 6.0f)
			s_Carrying = true;
		if(Ui()->MouseButton(0))
		{
			if(s_Carrying)
				Accessories.DrawIcon(s_CarryDef, Mouse, 46.0f, 0.85f);
		}
		else
		{
			std::vector<SFogAccessory> &vWorn = Accessories.Worn(AccSlot);
			int Num;
			const SFogAccessoryDef *pDefs = CFogAccessories::Catalogue(&Num);
			if((int)vWorn.size() < CFogAccessories::MAX_WORN)
			{
				const float BaseSize = std::max(Accessories.LastBaseSize(AccSlot), 1.0f);
				if(!s_Carrying)
					vWorn.push_back(Accessories.Make(s_CarryDef, pDefs[s_CarryDef].m_X, pDefs[s_CarryDef].m_Y));
				else if(Ui()->MouseInside(&StageFloor))
				{
					vec2 p = (Mouse - Accessories.LastBodyPos(AccSlot)) / BaseSize;
					if(pDefs[s_CarryDef].m_FollowEyes)
						p -= vec2(MainLook.x * 0.125f, MainLook.y * 0.10f);
					vWorn.push_back(Accessories.Make(s_CarryDef, std::clamp(p.x, -1.5f, 1.5f), std::clamp(p.y, -1.5f, 1.5f)));
				}
				if((int)vWorn.size() > 0 && (!s_Carrying || Ui()->MouseInside(&StageFloor)))
				{
					m_FogStudioAccessory = (int)vWorn.size() - 1;
					Accessories.Save(AccSlot);
				}
			}
			s_CarryDef = -1;
			s_Carrying = false;
		}
	}
}
