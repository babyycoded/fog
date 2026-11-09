// Fog client: clips. The replay recorder keeps the last minutes of play; fog_clip (a key)
// cuts the last fog_clip_length seconds into demos/replays. This page lists them as cards
// with watch, export to video, open folder and delete.

#include "glass.h"

#include <base/detect.h>
#include <base/io.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/demo.h>
#include <engine/font_icons.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/components/binds.h>
#include <game/client/components/key_binder.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <ctime>

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

struct SListUser
{
	std::vector<SFogClip> *m_pClips;
	IStorage *m_pStorage;
};

int ListCallback(const CFsFileInfo *pInfo, int IsDir, int DirType, void *pUser)
{
	if(IsDir || !str_endswith(pInfo->m_pName, ".demo"))
		return 0;
	SListUser *pList = static_cast<SListUser *>(pUser);
	SFogClip Clip;
	Clip.m_File = pInfo->m_pName;
	Clip.m_Time = pInfo->m_TimeModified;
	// names look like fog_<map>_<YYYY-MM-DD_HH-MM-SS>.demo or <map>_<timestamp>_(replay).demo
	std::string Name = pInfo->m_pName;
	Name = Name.substr(0, Name.size() - 5);
	if(str_startswith(Name.c_str(), "fog_"))
		Name = Name.substr(4);
	const size_t Replay = Name.rfind("_(replay)");
	if(Replay != std::string::npos)
		Name = Name.substr(0, Replay);
	if(Name.size() > 20 && Name[Name.size() - 20] == '_')
		Name = Name.substr(0, Name.size() - 20);
	Clip.m_Map = Name.empty() ? std::string(pInfo->m_pName) : Name;
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "demos/replays/%s", pInfo->m_pName);
	IOHANDLE File = pList->m_pStorage->OpenFile(aPath, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(File)
	{
		Clip.m_Size = io_length(File);
		io_close(File);
	}
	pList->m_pClips->push_back(Clip);
	return 0;
}

} // namespace

void CMenus::RenderFogClips(CUIRect MainView)
{
	const float Now = Client()->GlobalTime();
	if(Now - m_FogClipsRefresh > 3.0f)
	{
		m_FogClipsRefresh = Now;
		m_vFogClips.clear();
		SListUser User = {&m_vFogClips, Storage()};
		Storage()->ListDirectoryInfo(IStorage::TYPE_SAVE, "demos/replays", ListCallback, &User);
		std::sort(m_vFogClips.begin(), m_vFogClips.end(), [](const SFogClip &a, const SFogClip &b) { return a.m_Time > b.m_Time; });
	}

	MainView.Margin(vec2(10.0f, 6.0f), &MainView);

	// header: what clips are and how to make one
	CUIRect Header, Title, Hint, Controls;
	MainView.HSplitTop(62.0f, &Header, &MainView);
	Header.HSplitTop(8.0f, nullptr, &Header);
	Header.VSplitRight(std::min(360.0f, Header.w * 0.5f), &Header, &Controls);
	Header.HSplitTop(32.0f, &Title, &Hint);
	Hint.HSplitTop(18.0f, &Hint, nullptr);
	FogLabel(Ui(), TextRender(), Title, Localize("Clips"), 25.0f, TEXTALIGN_ML, FROST, "Manrope", 800);
	char aHint[256];
	str_format(aHint, sizeof(aHint), Localize("A key saves the last %d seconds of play. Clips stay in demos/replays."), g_Config.m_FogClipLength);
	FogLabel(Ui(), TextRender(), Hint, aHint, 12.0f, TEXTALIGN_ML, MIST.WithAlpha(0.9f), "Manrope", 500);

	// controls: the clip key and the length
	{
		CUIRect KeyRow, LengthRow, Label, Reader;
		Controls.HSplitTop(4.0f, nullptr, &Controls);
		Controls.HSplitTop(22.0f, &KeyRow, &Controls);
		Controls.HSplitTop(6.0f, nullptr, &Controls);
		Controls.HSplitTop(20.0f, &LengthRow, &Controls);
		KeyRow.VSplitLeft(110.0f, &Label, &Reader);
		Ui()->DoLabel(&Label, Localize("Clip key"), 13.0f, TEXTALIGN_ML);
		CBindSlot Current = EMPTY_BIND_SLOT;
		for(int Modifiers = 0; Modifiers < KeyModifier::COMBINATION_COUNT && Current == EMPTY_BIND_SLOT; Modifiers++)
			for(int Key = 0; Key < KEY_LAST; Key++)
			{
				const char *pBind = GameClient()->m_Binds.Get(Key, Modifiers);
				if(pBind && str_comp(pBind, "fog_clip") == 0)
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
				GameClient()->m_Binds.Bind(Result.m_Bind.m_Key, "fog_clip", false, Result.m_Bind.m_ModifierMask);
		}
		Ui()->DoScrollbarOption(&g_Config.m_FogClipLength, &g_Config.m_FogClipLength, &LengthRow, Localize("Clip length"), 5, 300, &CUi::ms_LinearScrollbarScale, 0, " s");
		if(!g_Config.m_ClReplays)
		{
			Controls.HSplitTop(4.0f, nullptr, &Controls);
			CUIRect Row;
			Controls.HSplitTop(20.0f, &Row, &Controls);
			if(DoButton_CheckBox(&g_Config.m_ClReplays, Localize("Keep recording the last minutes (needed for clips)"), g_Config.m_ClReplays, &Row))
				g_Config.m_ClReplays ^= 1;
		}
	}

	if(m_aFogClipStatus[0])
	{
		CUIRect Status;
		MainView.HSplitTop(18.0f, &Status, &MainView);
		FogLabel(Ui(), TextRender(), Status, m_aFogClipStatus, 11.0f, TEXTALIGN_ML, MIST, "Manrope", 600);
	}
	MainView.HSplitTop(8.0f, nullptr, &MainView);

	if(m_vFogClips.empty())
	{
		CUIRect Empty = MainView;
		Empty.HMargin((Empty.h - 60.0f) / 2.0f, &Empty);
		Icon(TextRender(), Ui(), {Empty.x, Empty.y, Empty.w, 30.0f}, FontIcon::VIDEO, 22.0f, MIST.WithAlpha(0.5f));
		CUIRect Text = {Empty.x, Empty.y + 34.0f, Empty.w, 20.0f};
		FogLabel(Ui(), TextRender(), Text, Localize("No clips yet. Set a clip key, play, and press it after something good happens."), 13.0f, TEXTALIGN_MC, MIST, "Manrope", 500);
		return;
	}

	// the clips as cards, newest first
	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams Params;
	Params.m_ScrollUnit = 80.0f;
	s_ScrollRegion.Begin(&MainView, &Params);
	const int Columns = MainView.w > 700.0f ? 3 : 2;
	const float Gap = 10.0f, CardH = 96.0f;
	const float CardW = (MainView.w - Gap * (Columns - 1)) / Columns;
	static std::vector<CButtonContainer> s_vButtons;
	if(s_vButtons.size() < m_vFogClips.size() * 4)
		s_vButtons.resize(m_vFogClips.size() * 4 + 16);
	int Action = -1, ActionClip = -1;
	for(size_t i = 0; i < m_vFogClips.size(); i++)
	{
		const SFogClip &Clip = m_vFogClips[i];
		CUIRect Card = {MainView.x + (i % Columns) * (CardW + Gap), MainView.y + (i / Columns) * (CardH + Gap), CardW, CardH};
		if(!s_ScrollRegion.AddRect(Card))
			continue;
		const ColorRGBA Top(1.0f, 1.0f, 1.0f, 0.07f), Bottom(1.0f, 1.0f, 1.0f, 0.035f);
		Graphics()->TextureClear();
		Graphics()->DrawRect4(Card.x, Card.y, Card.w, Card.h, Top, Top, Bottom, Bottom, IGraphics::CORNER_ALL, 14.0f);
		Graphics()->DrawRect(Card.x + 14.0f, Card.y + 0.5f, Card.w - 28.0f, 1.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.14f), IGraphics::CORNER_NONE, 0.0f);

		CUIRect Inner, Map, Meta, Buttons;
		Card.Margin(vec2(14.0f, 10.0f), &Inner);
		Inner.HSplitTop(24.0f, &Map, &Inner);
		Inner.HSplitTop(16.0f, &Meta, &Inner);
		Inner.HSplitBottom(24.0f, &Inner, &Buttons);
		FogLabel(Ui(), TextRender(), Map, Clip.m_Map.c_str(), 16.0f, TEXTALIGN_ML, FROST, "Manrope", 800);
		char aDate[64], aMeta[128];
		std::tm Local{};
		const time_t Time = Clip.m_Time;
#if defined(CONF_FAMILY_WINDOWS)
		localtime_s(&Local, &Time);
#else
		localtime_r(&Time, &Local);
#endif
		std::strftime(aDate, sizeof(aDate), "%d.%m.%Y %H:%M", &Local);
		str_format(aMeta, sizeof(aMeta), "%s, %.1f MB", aDate, Clip.m_Size / (1024.0 * 1024.0));
		FogLabel(Ui(), TextRender(), Meta, aMeta, 11.0f, TEXTALIGN_ML, DIM, "Manrope", 600);

		const bool Armed = m_FogClipDeleteArmed == (int)i;
		const struct
		{
			const char *m_pIcon;
			const char *m_pLabel;
			ColorRGBA m_Color;
		} aButtons[] = {
			{FontIcon::PLAY, Localize("Watch"), FROST},
			{FontIcon::VIDEO, Localize("Video MP4"), FROST},
			{FontIcon::FOLDER, Localize("Folder"), MIST},
			{FontIcon::TRASH, Armed ? Localize("Sure?") : Localize("Delete"), ROSE},
		};
		// two labelled actions on the left, folder and delete as round icons on the right;
		// once armed, the delete pill takes the folder's place to ask for confirmation
		const float IconW = Buttons.h, Gap6 = 6.0f;
		const float WideW = std::max(0.0f, (Buttons.w - 2 * IconW - 3 * Gap6) / 2.0f);
		const float RightX = Buttons.x + 2 * (WideW + Gap6);
		const float aX[4] = {Buttons.x, Buttons.x + WideW + Gap6, RightX, Armed ? RightX : RightX + IconW + Gap6};
		const float aW[4] = {WideW, WideW, IconW, Armed ? 2 * IconW + Gap6 : IconW};
		for(int b = 0; b < 4; b++)
		{
			if(b == 2 && Armed)
				continue;
			CUIRect Button = {aX[b], Buttons.y, aW[b], Buttons.h};
			CButtonContainer *pButton = &s_vButtons[i * 4 + b];
			const bool Hot = Ui()->HotItem() == pButton;
			Graphics()->TextureClear();
			Graphics()->DrawRect(Button.x, Button.y, Button.w, Button.h, (b == 3 && Armed ? ROSE.WithAlpha(0.25f) : ColorRGBA(1.0f, 1.0f, 1.0f, Hot ? 0.12f : 0.06f)), IGraphics::CORNER_ALL, Button.h / 2.0f);
			const bool IconOnly = b == 2 || (b == 3 && !Armed);
			CUIRect IconRect, Text;
			if(b == 3 && Armed)
				FogLabel(Ui(), TextRender(), Button, aButtons[b].m_pLabel, 10.0f, TEXTALIGN_MC, aButtons[b].m_Color, "Manrope", 800);
			else if(IconOnly)
				Icon(TextRender(), Ui(), Button, aButtons[b].m_pIcon, 9.0f, aButtons[b].m_Color);
			else
			{
				Button.VSplitLeft(24.0f, &IconRect, &Text);
				Icon(TextRender(), Ui(), IconRect, aButtons[b].m_pIcon, 9.0f, aButtons[b].m_Color);
				FogLabel(Ui(), TextRender(), Text, aButtons[b].m_pLabel, 10.0f, TEXTALIGN_ML, aButtons[b].m_Color, "Manrope", 700);
			}
			if(IconOnly)
				Ui()->DoToolTip(pButton, &Button, aButtons[b].m_pLabel);
			if(Ui()->DoButtonLogic(pButton, 0, &Button, BUTTONFLAG_LEFT))
			{
				Action = b;
				ActionClip = i;
			}
		}
	}
	s_ScrollRegion.End();

	if(ActionClip < 0)
		return;
	const SFogClip Clip = m_vFogClips[ActionClip];
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "demos/replays/%s", Clip.m_File.c_str());
	if(Action != 3)
		m_FogClipDeleteArmed = -1;
	switch(Action)
	{
	case 0:
	{
		const char *pError = Client()->DemoPlayer_Play(aPath, IStorage::TYPE_SAVE);
		str_copy(m_aFogClipStatus, pError ? pError : "");
		break;
	}
	case 1:
	{
		// rendered at normal speed into videos/<name>.mp4 by the built-in video recorder
		char aVideo[IO_MAX_PATH_LENGTH];
		str_copy(aVideo, Clip.m_File.c_str());
		aVideo[str_length(aVideo) - 5] = '\0';
		const char *pError = Client()->DemoPlayer_Render(aPath, IStorage::TYPE_SAVE, aVideo, DEMO_SPEED_INDEX_DEFAULT);
		if(pError)
			str_copy(m_aFogClipStatus, pError);
		else
			str_format(m_aFogClipStatus, sizeof(m_aFogClipStatus), Localize("Rendering to videos/%s.mp4"), aVideo);
		break;
	}
	case 2:
	{
		char aFolder[IO_MAX_PATH_LENGTH];
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, "demos/replays", aFolder, sizeof(aFolder));
		Client()->ViewFile(aFolder);
		break;
	}
	case 3:
		if(m_FogClipDeleteArmed == ActionClip)
		{
			Storage()->RemoveFile(aPath, IStorage::TYPE_SAVE);
			m_FogClipDeleteArmed = -1;
			m_FogClipsRefresh = -100.0f;
			str_format(m_aFogClipStatus, sizeof(m_aFogClipStatus), Localize("Deleted %s"), Clip.m_File.c_str());
		}
		else
			m_FogClipDeleteArmed = ActionClip;
		break;
	default:
		break;
	}
}
