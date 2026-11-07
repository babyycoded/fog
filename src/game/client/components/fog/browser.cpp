// Fog client: the server browser, redesigned. Communities and map categories are chips on
// top, servers are cards (name, category, map, how full it is, ping) and the selected server
// opens in a detail panel on the right with the people playing there.

#include "brand.h"
#include "glass.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/favorites.h>
#include <engine/font_icons.h>
#include <engine/friends.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>

namespace
{

	constexpr ColorRGBA FROST(0.91f, 0.93f, 0.95f, 1.0f);
	constexpr ColorRGBA MIST(0.64f, 0.68f, 0.74f, 1.0f);
	constexpr ColorRGBA DIM(0.46f, 0.50f, 0.56f, 1.0f);

	ColorRGBA Lerp(const ColorRGBA &A, const ColorRGBA &B, float T)
	{
		return ColorRGBA(A.r + (B.r - A.r) * T, A.g + (B.g - A.g) * T, A.b + (B.b - A.b) * T, A.a + (B.a - A.a) * T);
	}

	// ping in the fog palette: sea green when close, sand in the middle, dusty rose when far
	ColorRGBA PingColor(int Latency)
	{
		const ColorRGBA Near(0.60f, 0.84f, 0.74f, 1.0f), Mid(0.88f, 0.80f, 0.58f, 1.0f), Far(0.90f, 0.58f, 0.58f, 1.0f);
		if(Latency <= 60)
			return Near;
		if(Latency <= 140)
			return Lerp(Near, Mid, (Latency - 60) / 80.0f);
		return Lerp(Mid, Far, std::min((Latency - 140) / 120.0f, 1.0f));
	}

	// a muted version of a gametype colour, so categories stay readable on the grey glass
	ColorRGBA Muted(const ColorRGBA &Color)
	{
		ColorHSLA Hsl = color_cast<ColorHSLA>(Color);
		Hsl.s *= 0.55f;
		Hsl.l = std::clamp(Hsl.l, 0.62f, 0.78f);
		return color_cast<ColorRGBA>(Hsl).WithAlpha(1.0f);
	}

	void IconLabel(ITextRender *pTextRender, CUi *pUi, const CUIRect &Rect, const char *pIcon, float Size, ColorRGBA Color, int Align = TEXTALIGN_MC)
	{
		pTextRender->SetFontPreset(EFontPreset::ICON_FONT);
		pTextRender->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		pTextRender->TextColor(Color);
		pUi->DoLabel(&Rect, pIcon, Size, Align);
		pTextRender->TextColor(pTextRender->DefaultTextColor());
		pTextRender->SetRenderFlags(0);
		pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}

	float DisplayWidth(ITextRender *pTextRender, const char *pText, float Size, int Weight)
	{
		pTextRender->SetDisplayFont("Manrope", Weight);
		pTextRender->SetFontPreset(EFontPreset::DISPLAY_FONT);
		const float w = pTextRender->TextWidth(Size, pText);
		pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
		return w;
	}

	// plain body text (user content such as server and player names may use any script,
	// so it stays in the default face with its fallbacks), no game outline
	void BodyLabel(ITextRender *pTextRender, CUi *pUi, const CUIRect &Rect, const char *pText, float Size, int Align, ColorRGBA Color)
	{
		const ColorRGBA OldOutline = pTextRender->GetTextOutlineColor();
		pTextRender->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.0f);
		pTextRender->TextColor(Color);
		SLabelProperties Props;
		Props.m_MaxWidth = Rect.w;
		Props.m_EllipsisAtEnd = true;
		pUi->DoLabel(&Rect, pText, Size, Align, Props);
		pTextRender->TextColor(pTextRender->DefaultTextColor());
		pTextRender->TextOutlineColor(OldOutline);
	}

	void Fill(IGraphics *pGraphics, const CUIRect &R, ColorRGBA Color, float Rounding, int Corners = IGraphics::CORNER_ALL)
	{
		pGraphics->TextureClear();
		pGraphics->DrawRect(R.x, R.y, R.w, R.h, Color, Corners, Rounding);
	}

	// matches the toolbox page numbers of menus_browser.cpp (ui_toolbox_page)
	enum
	{
		UI_TOOLBOX_PAGE_FILTERS = 0,
		UI_TOOLBOX_PAGE_INFO,
		UI_TOOLBOX_PAGE_FRIENDS,
		NUM_UI_TOOLBOX_PAGES,
	};

	template<size_t N>
	void FormatServerbrowserPing(char (&aBuffer)[N], const CServerInfo *pInfo)
	{
		if(!pInfo->m_LatencyIsEstimated)
		{
			str_format(aBuffer, sizeof(aBuffer), "%d", pInfo->m_Latency);
			return;
		}
		static const char *const LOCATION_NAMES[CServerInfo::NUM_LOCS] = {"", "AFR", "ASI", "AUS", "EUR", "NA", "SA", "CHN"};
		str_copy(aBuffer, Localize(LOCATION_NAMES[std::clamp(pInfo->m_Location, 0, (int)CServerInfo::NUM_LOCS - 1)]));
	}

} // namespace

// A flowing row of filter chips over an IFilterList. Left click keeps only that chip,
// right click adds or removes it from the selection, "all" clears the filter.
// With Measure set nothing is drawn and the needed height is written to pView->h.
bool CMenus::RenderFogFilterChips(CUIRect *pView, IFilterList &Filter, int Num, const std::function<const char *(int)> &GetId, const std::function<const char *(int)> &GetLabel, const std::function<int(int)> &GetPlayers, const std::function<void(int, CUIRect)> &RenderIcon, const char *pAllLabel, std::vector<unsigned char> &vIds, bool Measure)
{
	const float ChipH = 20.0f, Gap = 4.0f, FontSize = 10.5f;
	vIds.resize(Num + 1);
	const bool HasIcons = static_cast<bool>(RenderIcon);

	float x = pView->x, y = pView->y;
	const auto Place = [&](float w) {
		if(x + w > pView->x + pView->w && x > pView->x)
		{
			x = pView->x;
			y += ChipH + Gap;
		}
		CUIRect R = {x, y, w, ChipH};
		x += w + Gap;
		return R;
	};

	bool Changed = false;
	const auto IsSelectable = [&](int i) {
		return str_comp(GetId(i), IServerBrowser::COMMUNITY_TYPE_NONE) != 0;
	};
	const auto DrawChip = [&](const CUIRect &R, const void *pId, bool Active, const char *pLabel, int Players, int IconIndex) {
		const bool Hot = Ui()->HotItem() == pId;
		const ColorRGBA Top = Active ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.17f + (Hot ? 0.05f : 0.0f)) : ColorRGBA(1.0f, 1.0f, 1.0f, Hot ? 0.08f : 0.035f);
		const ColorRGBA Bottom = Top.WithMultipliedAlpha(0.55f);
		Graphics()->TextureClear();
		Graphics()->DrawRect4(R.x, R.y, R.w, R.h, Top, Top, Bottom, Bottom, IGraphics::CORNER_ALL, R.h / 2.0f);
		if(Active)
			Graphics()->DrawRect(R.x + R.h / 2.0f, R.y + 0.5f, R.w - R.h, 1.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.18f), IGraphics::CORNER_NONE, 0.0f);
		CUIRect Content;
		R.VMargin(9.0f, &Content);
		if(IconIndex >= 0 && HasIcons)
		{
			CUIRect Icon;
			Content.VSplitLeft(ChipH * 1.25f, &Icon, &Content);
			Icon.HMargin(4.0f, &Icon);
			RenderIcon(IconIndex, Icon);
			Content.VSplitLeft(4.0f, nullptr, &Content);
		}
		const ColorRGBA TextColor = Active ? FROST : (Hot ? ColorRGBA(0.82f, 0.85f, 0.89f, 1.0f) : MIST.WithAlpha(0.85f));
		if(Players >= 0)
		{
			char aBuf[16];
			str_format(aBuf, sizeof(aBuf), "%d", Players);
			CUIRect Count;
			Content.VSplitRight(DisplayWidth(TextRender(), aBuf, 9.0f, 600) + 2.0f, &Content, &Count);
			FogLabel(Ui(), TextRender(), Count, aBuf, 9.0f, TEXTALIGN_MR, (Active ? MIST : DIM).WithAlpha(0.9f), "Manrope", 600);
		}
		FogLabel(Ui(), TextRender(), Content, pLabel, FontSize, TEXTALIGN_ML, TextColor, "Manrope", Active ? 700 : 600);
	};

	// "all" chip
	{
		const float w = DisplayWidth(TextRender(), pAllLabel, FontSize, 700) + 18.0f;
		const CUIRect R = Place(w);
		if(!Measure)
		{
			const void *pId = &vIds[Num];
			if(Ui()->DoButtonLogic(pId, 0, &R, BUTTONFLAG_LEFT) && !Filter.Empty())
			{
				for(int i = 0; i < Num; i++)
					Filter.Remove(GetId(i));
				Changed = true;
			}
			DrawChip(R, pId, Filter.Empty(), pAllLabel, -1, -1);
		}
	}

	for(int i = 0; i < Num; i++)
	{
		if(!IsSelectable(i))
			continue;
		const char *pLabel = GetLabel(i);
		const int Players = GetPlayers ? GetPlayers(i) : -1;
		char aCount[16] = "";
		if(Players >= 0)
			str_format(aCount, sizeof(aCount), "%d", Players);
		const float w = DisplayWidth(TextRender(), pLabel, FontSize, 700) + 18.0f + (HasIcons ? ChipH * 1.25f + 4.0f : 0.0f) + (Players >= 0 ? DisplayWidth(TextRender(), aCount, 9.0f, 600) + 7.0f : 0.0f);
		const CUIRect R = Place(w);
		if(Measure)
			continue;

		const void *pId = &vIds[i];
		// a chip is lit when the filter is narrowed down to it (not when everything is shown)
		const bool Shown = !Filter.Filtered(GetId(i));
		const bool Lit = Shown && !Filter.Empty();
		const int Click = Ui()->DoButtonLogic(pId, 0, &R, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
		if(Click == 1)
		{
			// keep only this one; clicking the only lit chip again shows everything
			bool OnlyThis = Lit;
			for(int j = 0; j < Num && OnlyThis; j++)
				if(j != i && IsSelectable(j) && !Filter.Filtered(GetId(j)))
					OnlyThis = false;
			for(int j = 0; j < Num; j++)
				Filter.Remove(GetId(j));
			if(!OnlyThis)
				for(int j = 0; j < Num; j++)
					if(j != i && IsSelectable(j))
						Filter.Add(GetId(j));
			Changed = true;
		}
		else if(Click == 2)
		{
			// add to or take out of the selection
			if(Filter.Empty())
			{
				for(int j = 0; j < Num; j++)
					if(j != i && IsSelectable(j))
						Filter.Add(GetId(j));
			}
			else if(Lit)
			{
				Filter.Add(GetId(i));
				bool AnyLeft = false;
				for(int j = 0; j < Num; j++)
					if(IsSelectable(j) && !Filter.Filtered(GetId(j)))
						AnyLeft = true;
				if(!AnyLeft)
					for(int j = 0; j < Num; j++)
						Filter.Remove(GetId(j));
			}
			else
				Filter.Remove(GetId(i));
			Changed = true;
		}
		DrawChip(R, pId, Lit, pLabel, Players, i);
	}

	const float Height = y + ChipH - pView->y;
	if(Measure)
		pView->h = Height;
	else
		pView->HSplitTop(Height, nullptr, pView);
	return Changed;
}

void CMenus::RenderFogServerCard(CUIRect Rect, const CServerInfo *pItem, bool Selected, bool Hot, float In)
{
	const float Alpha = std::clamp(In * 1.6f, 0.0f, 1.0f);
	Rect.x += (1.0f - FogEaseOutBack(In)) * 18.0f;

	// card body: faint by default, lifted on hover, frosted with a light rail when selected
	if(Selected)
	{
		Graphics()->TextureClear();
		Graphics()->DrawRect(Rect.x - 3.0f, Rect.y - 2.0f, Rect.w + 6.0f, Rect.h + 4.0f, ColorRGBA(0.80f, 0.86f, 0.94f, 0.05f * Alpha), IGraphics::CORNER_ALL, 11.0f);
		const ColorRGBA Top(1.0f, 1.0f, 1.0f, 0.15f * Alpha), Bottom(1.0f, 1.0f, 1.0f, 0.08f * Alpha);
		Graphics()->DrawRect4(Rect.x, Rect.y, Rect.w, Rect.h, Top, Top, Bottom, Bottom, IGraphics::CORNER_ALL, 9.0f);
		Graphics()->DrawRect(Rect.x + 9.0f, Rect.y + 0.5f, Rect.w - 18.0f, 1.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.20f * Alpha), IGraphics::CORNER_NONE, 0.0f);
		Graphics()->DrawRect(Rect.x + 2.5f, Rect.y + 9.0f, 2.5f, Rect.h - 18.0f, FROST.WithAlpha(0.9f * Alpha), IGraphics::CORNER_ALL, 1.5f);
	}
	else
	{
		const ColorRGBA Top(1.0f, 1.0f, 1.0f, (Hot ? 0.085f : 0.035f) * Alpha), Bottom(1.0f, 1.0f, 1.0f, (Hot ? 0.05f : 0.018f) * Alpha);
		Graphics()->TextureClear();
		Graphics()->DrawRect4(Rect.x, Rect.y, Rect.w, Rect.h, Top, Top, Bottom, Bottom, IGraphics::CORNER_ALL, 9.0f);
	}

	CUIRect Content, Icon, NameBlock, MapBlock, PlayersBlock, PingBlock;
	Rect.Margin(vec2(10.0f, 4.0f), &Content);
	Content.VSplitLeft(30.0f, &Icon, &Content);
	Content.VSplitLeft(8.0f, nullptr, &Content);
	Content.VSplitRight(38.0f, &Content, &PingBlock);
	Content.VSplitRight(8.0f, &Content, nullptr);
	Content.VSplitRight(64.0f, &Content, &PlayersBlock);
	Content.VSplitRight(12.0f, &Content, nullptr);
	Content.VSplitRight(std::clamp(Content.w * 0.34f, 70.0f, 170.0f), &NameBlock, &MapBlock);
	NameBlock.VSplitRight(8.0f, &NameBlock, nullptr);

	// community mark (or a globe for servers outside any community)
	{
		const CCommunity *pCommunity = ServerBrowser()->Community(pItem->m_aCommunityId);
		const CCommunityIcon *pIcon = pCommunity != nullptr ? m_CommunityIcons.Find(pCommunity->Id()) : nullptr;
		CUIRect IconRect = Icon;
		IconRect.HMargin((IconRect.h - 15.0f) / 2.0f, &IconRect);
		if(pIcon != nullptr)
			m_CommunityIcons.Render(pIcon, IconRect, true);
		else
			IconLabel(TextRender(), Ui(), Icon, FontIcon::EARTH_AMERICAS, 10.0f, DIM.WithAlpha(Alpha));
	}

	// name, then a line with the category and small state marks
	{
		CUIRect NameLine, MetaLine;
		NameBlock.HSplitMid(&NameLine, &MetaLine);
		BodyLabel(TextRender(), Ui(), NameLine, pItem->m_aName, 11.0f, TEXTALIGN_ML, (Selected || Hot ? FROST : ColorRGBA(0.86f, 0.88f, 0.91f, 1.0f)).WithAlpha(Alpha));

		float x = MetaLine.x;
		const float Cy = MetaLine.y + MetaLine.h / 2.0f;
		if(pItem->m_aGameType[0])
		{
			const ColorRGBA TypeColor = g_Config.m_UiColorizeGametype ? Muted(pItem->m_GametypeColor) : MIST;
			const float w = DisplayWidth(TextRender(), pItem->m_aGameType, 8.0f, 700) + 10.0f;
			Fill(Graphics(), {x, Cy - 5.5f, w, 11.0f}, TypeColor.WithAlpha(0.14f * Alpha), 5.5f);
			FogLabel(Ui(), TextRender(), {x, Cy - 5.5f, w, 11.0f}, pItem->m_aGameType, 8.0f, TEXTALIGN_MC, TypeColor.WithAlpha(Alpha), "Manrope", 700);
			x += w + 5.0f;
		}
		const auto Mark = [&](const char *pIcon, ColorRGBA Color, const char *pText = nullptr) {
			IconLabel(TextRender(), Ui(), {x, MetaLine.y, 10.0f, MetaLine.h}, pIcon, 7.0f, Color.WithAlpha(Alpha));
			x += 11.0f;
			if(pText)
			{
				const float w = DisplayWidth(TextRender(), pText, 8.0f, 600) + 3.0f;
				FogLabel(Ui(), TextRender(), {x, MetaLine.y, w, MetaLine.h}, pText, 8.0f, TEXTALIGN_ML, Color.WithAlpha(Alpha), "Manrope", 600);
				x += w + 4.0f;
			}
		};
		if(pItem->m_FriendState != IFriends::FRIEND_NO)
		{
			char aNum[8];
			str_format(aNum, sizeof(aNum), "%d", pItem->m_FriendNum);
			Mark(FontIcon::HEART, ColorRGBA(0.92f, 0.64f, 0.68f, 1.0f), pItem->m_FriendNum > 1 ? aNum : nullptr);
		}
		if(pItem->m_Favorite != TRISTATE::NONE)
			Mark(FontIcon::STAR, ColorRGBA(0.92f, 0.84f, 0.60f, 1.0f));
		if(pItem->m_Flags & SERVER_FLAG_PASSWORD)
			Mark(FontIcon::LOCK, MIST);
		if(pItem->m_RequiresLogin)
			Mark(FontIcon::KEY, MIST);
		if(pItem->m_aCommunityCountry[0] && str_comp(pItem->m_aCommunityCountry, IServerBrowser::COMMUNITY_COUNTRY_NONE) != 0)
		{
			const float w = DisplayWidth(TextRender(), pItem->m_aCommunityCountry, 8.0f, 700) + 3.0f;
			FogLabel(Ui(), TextRender(), {x, MetaLine.y, w, MetaLine.h}, pItem->m_aCommunityCountry, 8.0f, TEXTALIGN_ML, DIM.WithAlpha(Alpha), "Manrope", 700);
		}
	}

	// the map is the headline of a card: that is what people pick a server for
	{
		CUIRect MapName = MapBlock;
		const bool Finished = g_Config.m_BrIndicateFinished && pItem->m_HasRank == CServerInfo::RANK_RANKED;
		if(Finished)
		{
			CUIRect Flag;
			MapName.VSplitRight(14.0f, &MapName, &Flag);
			IconLabel(TextRender(), Ui(), Flag, FontIcon::FLAG_CHECKERED, 8.0f, ColorRGBA(0.60f, 0.84f, 0.74f, 0.9f * Alpha));
		}
		FogLabel(Ui(), TextRender(), MapName, pItem->m_aMap, 12.5f, TEXTALIGN_ML, (Selected ? FROST : ColorRGBA(0.84f, 0.87f, 0.91f, 1.0f)).WithAlpha(Alpha), "Manrope", 700);
	}

	// how full it is: count over a slim capacity bar
	{
		const int Max = std::max(ServerBrowser()->Max(*pItem), 1);
		const float Fill01 = std::clamp(pItem->m_NumFilteredPlayers / (float)Max, 0.0f, 1.0f);
		CUIRect Count, Bar;
		PlayersBlock.HSplitMid(&Count, &Bar);
		char aBuf[32];
		str_format(aBuf, sizeof(aBuf), "%d/%d", pItem->m_NumFilteredPlayers, ServerBrowser()->Max(*pItem));
		FogLabel(Ui(), TextRender(), Count, aBuf, 11.0f, TEXTALIGN_BR, (pItem->m_NumFilteredPlayers > 0 ? FROST : DIM).WithAlpha(Alpha), "Manrope", 700);
		const float BarY = Bar.y + 3.0f;
		Fill(Graphics(), {Bar.x, BarY, Bar.w, 2.5f}, ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f * Alpha), 1.5f);
		if(Fill01 > 0.0f)
		{
			const ColorRGBA BarColor = Fill01 >= 0.999f ? ColorRGBA(0.90f, 0.66f, 0.60f, 0.9f) : Lerp(ColorRGBA(0.62f, 0.72f, 0.80f, 0.8f), ColorRGBA(0.86f, 0.90f, 0.95f, 0.95f), Fill01);
			Fill(Graphics(), {Bar.x, BarY, std::max(Bar.w * Fill01, 2.5f), 2.5f}, BarColor.WithMultipliedAlpha(Alpha), 1.25f);
		}
	}

	// ping: a dot in the distance colour and the number
	{
		char aPing[16];
		FormatServerbrowserPing(aPing, pItem);
		const ColorRGBA Color = PingColor(pItem->m_Latency);
		const float Cy = PingBlock.y + PingBlock.h / 2.0f;
		Fill(Graphics(), {PingBlock.x + 2.0f, Cy - 2.0f, 4.0f, 4.0f}, Color.WithAlpha(0.9f * Alpha), 2.0f);
		Fill(Graphics(), {PingBlock.x, Cy - 4.0f, 8.0f, 8.0f}, Color.WithAlpha(0.12f * Alpha), 4.0f);
		CUIRect Num = PingBlock;
		Num.VSplitLeft(11.0f, nullptr, &Num);
		FogLabel(Ui(), TextRender(), Num, aPing, 11.0f, TEXTALIGN_ML, Color.WithAlpha(Alpha), "Manrope", 700);
	}
}

void CMenus::RenderFogServerbrowser(CUIRect MainView)
{
	UpdateCommunityCache(false);
	if(g_Config.m_UiPage == PAGE_LAN && m_ForceRefreshLanPage)
	{
		RefreshBrowserTab(true);
		m_ForceRefreshLanPage = false;
	}

	const float Time = Client()->GlobalTime();
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	if(g_Config.m_UiPage != m_FogBrowserLastPage)
	{
		m_FogBrowserLastPage = g_Config.m_UiPage;
		m_FogBrowserInTime = Time;
	}
	const float AnimSpeed = g_Config.m_FogAnim ? std::clamp(g_Config.m_FogAnimSpeed / 100.0f, 0.1f, 10.0f) : 1000.0f;

	CUIRect Panel, Header, Toolbar, List;
	MainView.VSplitRight(220.0f, &MainView, &Panel);
	MainView.VSplitRight(10.0f, &MainView, nullptr);
	MainView.VSplitLeft(4.0f, nullptr, &MainView);

	// ---- header: where you are, how much is out there, search ----
	MainView.HSplitTop(62.0f, &Header, &MainView);
	{
		const char *pTitle = Localize("Internet");
		if(g_Config.m_UiPage == PAGE_LAN)
			pTitle = Localize("LAN");
		else if(g_Config.m_UiPage == PAGE_FAVORITES)
			pTitle = Localize("Favorites");
		else if(g_Config.m_UiPage >= PAGE_FAVORITE_COMMUNITY_1 && g_Config.m_UiPage <= PAGE_FAVORITE_COMMUNITY_5)
		{
			const std::vector<const CCommunity *> vpFavorites = ServerBrowser()->FavoriteCommunities();
			const size_t Index = g_Config.m_UiPage - PAGE_FAVORITE_COMMUNITY_1;
			if(Index < vpFavorites.size())
				pTitle = vpFavorites[Index]->Name();
		}

		CUIRect TitleBlock, Tools, Title, Hint;
		Header.VSplitRight(std::min(300.0f, Header.w * 0.5f), &TitleBlock, &Tools);
		TitleBlock.HSplitTop(8.0f, nullptr, &TitleBlock);
		TitleBlock.HSplitTop(32.0f, &Title, &Hint);
		Hint.HSplitTop(18.0f, &Hint, nullptr);
		const float In = FogEaseOutBack(std::min(1.0f, (Time - m_FogBrowserInTime) * 3.0f * AnimSpeed));
		Title.y += (1.0f - In) * 10.0f;
		FogLabel(Ui(), TextRender(), Title, pTitle, 25.0f, TEXTALIGN_ML, FROST, "Manrope", 800);

		char aServers[128], aPlayers[64], aHint[256];
		if(ServerBrowser()->NumServers() != 1)
			str_format(aServers, sizeof(aServers), Localize("%d of %d servers"), ServerBrowser()->NumSortedServers(), ServerBrowser()->NumServers());
		else
			str_format(aServers, sizeof(aServers), Localize("%d of %d server"), ServerBrowser()->NumSortedServers(), ServerBrowser()->NumServers());
		if(ServerBrowser()->NumSortedPlayers() != 1)
			str_format(aPlayers, sizeof(aPlayers), Localize("%d players"), ServerBrowser()->NumSortedPlayers());
		else
			str_format(aPlayers, sizeof(aPlayers), Localize("%d player"), ServerBrowser()->NumSortedPlayers());
		if(!ServerBrowser()->NumServers() && ServerBrowser()->IsGettingServerlist())
			str_copy(aHint, Localize("Getting server list from master server"));
		else
			str_format(aHint, sizeof(aHint), "%s, %s", aServers, aPlayers);
		FogLabel(Ui(), TextRender(), Hint, aHint, 12.5f, TEXTALIGN_ML, MIST.WithAlpha(0.9f), "Manrope", 500);

		// search, address, refresh
		CUIRect Search, Address, Refresh;
		Tools.HSplitTop(18.0f, nullptr, &Tools);
		Tools.HSplitTop(24.0f, &Tools, nullptr);
		Tools.VSplitRight(24.0f, &Tools, &Refresh);
		Tools.VSplitRight(6.0f, &Tools, nullptr);
		Tools.VSplitRight(std::min(100.0f, Tools.w * 0.36f), &Search, &Address);
		Search.VSplitRight(6.0f, &Search, nullptr);

		const auto Field = [&](CUIRect R, const char *pIcon, CLineInput *pInput) {
			const bool Focused = CLineInput::GetActiveInput() == pInput;
			Fill(Graphics(), R, ColorRGBA(0.0f, 0.01f, 0.03f, Focused ? 0.34f : 0.24f), R.h / 2.0f);
			if(Focused)
			{
				Graphics()->DrawRect(R.x + R.h / 2.0f, R.y + R.h - 1.5f, R.w - R.h, 1.0f, ColorRGBA(0.85f, 0.90f, 0.95f, 0.35f), IGraphics::CORNER_NONE, 0.0f);
			}
			CUIRect Icon, Box;
			R.VSplitLeft(22.0f, &Icon, &Box);
			IconLabel(TextRender(), Ui(), Icon, pIcon, 9.0f, Focused ? FROST : MIST);
			Box.HMargin(3.0f, &Box);
			Box.VSplitRight(5.0f, &Box, nullptr);
			// the field draws its own frame through the glass hook; keep that frame invisible
			const auto pOldHook = CUIRect::ms_pfnDrawHook;
			CUIRect::ms_pfnDrawHook = [](const CUIRect &, const ColorRGBA &, int, float) { return true; };
			const bool Changed = Ui()->DoClearableEditBox(pInput, &Box, 11.0f);
			CUIRect::ms_pfnDrawHook = pOldHook;
			return Changed;
		};

		static CLineInput s_SearchInput(g_Config.m_BrFilterString, sizeof(g_Config.m_BrFilterString));
		s_SearchInput.SetEmptyText(Localize("Server, map or player"));
		if(!Ui()->IsPopupOpen() && Input()->KeyPress(KEY_F) && Input()->ModifierIsPressed())
		{
			Ui()->SetActiveItem(&s_SearchInput);
			s_SearchInput.SelectAll();
		}
		if(Field(Search, FontIcon::MAGNIFYING_GLASS, &s_SearchInput))
			Client()->ServerBrowserUpdate();

		static CLineInput s_AddressInput(g_Config.m_UiServerAddress, sizeof(g_Config.m_UiServerAddress));
		s_AddressInput.SetEmptyText(Localize("Address"));
		if(Field(Address, FontIcon::NETWORK_WIRED, &s_AddressInput))
			m_ServerBrowserShouldRevealSelection = true;

		static CButtonContainer s_RefreshButton;
		const bool Refreshing = ServerBrowser()->IsRefreshing() || ServerBrowser()->IsGettingServerlist();
		const bool RefreshHot = Ui()->HotItem() == &s_RefreshButton;
		Fill(Graphics(), Refresh, ColorRGBA(1.0f, 1.0f, 1.0f, RefreshHot ? 0.14f : 0.07f), Refresh.h / 2.0f);
		IconLabel(TextRender(), Ui(), Refresh, FontIcon::ARROW_ROTATE_RIGHT, 9.5f, Refreshing ? MIST.WithAlpha(0.55f + 0.45f * std::sin(Time * 6.0f)) : FROST);
		if(Ui()->DoButtonLogic(&s_RefreshButton, 0, &Refresh, BUTTONFLAG_LEFT) || (!Ui()->IsPopupOpen() && (Input()->KeyPress(KEY_F5) || (Input()->KeyPress(KEY_R) && Input()->ModifierIsPressed()))))
			RefreshBrowserTab(true);
		Ui()->DoToolTip(&s_RefreshButton, &Refresh, Localize("Refresh"));
	}

	// loading progress: a hairline under the header that fills and fades
	{
		static float s_FadeEnd = 0.0f;
		if(ServerBrowser()->IsRefreshing() && ServerBrowser()->LoadingProgression() < 100)
			s_FadeEnd = Time + 1.5f;
		const float Left = s_FadeEnd - Time;
		if(Left > 0.0f)
		{
			const float a = std::min(Left, 0.8f);
			Fill(Graphics(), {MainView.x, MainView.y, MainView.w, 1.5f}, ColorRGBA(1.0f, 1.0f, 1.0f, 0.06f * a), 0.75f);
			Fill(Graphics(), {MainView.x, MainView.y, MainView.w * ServerBrowser()->LoadingProgression() / 100.0f, 1.5f}, ColorRGBA(0.86f, 0.90f, 0.95f, 0.7f * a), 0.75f);
		}
		MainView.HSplitTop(8.0f, nullptr, &MainView);
	}

	// ---- communities and map categories ----
	const bool CommunityPage = g_Config.m_UiPage == PAGE_INTERNET || g_Config.m_UiPage == PAGE_FAVORITES;
	if(CommunityPage && !ServerBrowser()->Communities().empty())
	{
		static std::vector<unsigned char> s_vCommunityIds;
		const auto &vCommunities = ServerBrowser()->Communities();
		const auto GetId = [&](int i) { return vCommunities[i].Id(); };
		const auto GetLabel = [&](int i) { return vCommunities[i].Name(); };
		const auto GetPlayers = [&](int i) { return vCommunities[i].NumPlayers(); };
		const auto RenderIcon = [&](int i, CUIRect R) {
			if(const CCommunityIcon *pIcon = m_CommunityIcons.Find(vCommunities[i].Id()))
				m_CommunityIcons.Render(pIcon, R, true);
		};
		CUIRect Chips = MainView;
		RenderFogFilterChips(&Chips, ServerBrowser()->CommunitiesFilter(), vCommunities.size(), GetId, GetLabel, GetPlayers, RenderIcon, Localize("All communities"), s_vCommunityIds, true);
		CUIRect Row;
		MainView.HSplitTop(Chips.h, &Row, &MainView);
		if(RenderFogFilterChips(&Row, ServerBrowser()->CommunitiesFilter(), vCommunities.size(), GetId, GetLabel, GetPlayers, RenderIcon, Localize("All communities"), s_vCommunityIds, false))
		{
			Client()->ServerBrowserUpdate();
			UpdateCommunityCache(true);
		}
		MainView.HSplitTop(5.0f, nullptr, &MainView);
	}

	// game modes of the chosen community (Block, DDNet, Tutorial, ...)
	{
		const auto &vpTypes = ServerBrowser()->CommunityCache().SelectableTypes();
		if(ServerBrowser()->CommunityCache().CountriesTypesFilterAvailable() && !vpTypes.empty())
		{
			static std::vector<unsigned char> s_vTypeIds;
			const auto GetTypeId = [&](int i) { return vpTypes[i]->Name(); };
			CUIRect TypeChips = MainView;
			TypeChips.VSplitLeft(18.0f, nullptr, &TypeChips);
			RenderFogFilterChips(&TypeChips, ServerBrowser()->TypesFilter(), vpTypes.size(), GetTypeId, GetTypeId, nullptr, nullptr, Localize("All modes"), s_vTypeIds, true);
			CUIRect TypeRow, MapIcon;
			MainView.HSplitTop(TypeChips.h, &TypeRow, &MainView);
			TypeRow.VSplitLeft(18.0f, &MapIcon, &TypeRow);
			MapIcon.h = 20.0f;
			IconLabel(TextRender(), Ui(), MapIcon, FontIcon::LAYER_GROUP, 8.5f, DIM);
			if(RenderFogFilterChips(&TypeRow, ServerBrowser()->TypesFilter(), vpTypes.size(), GetTypeId, GetTypeId, nullptr, nullptr, Localize("All modes"), s_vTypeIds, false))
				Client()->ServerBrowserUpdate();
			MainView.HSplitTop(5.0f, nullptr, &MainView);
		}
	}

	// map filters the way DDNet players think about maps: how hard, what kind, how many people
	if(g_Config.m_UiPage != PAGE_LAN)
	{
		struct SChip
		{
			const char *m_pLabel;
			const char *m_pHint;
			ColorRGBA m_Color;
			int m_Pips; // difficulty steps drawn after the label, 0 for none
			const char *m_pIcon;
		};
		const float ChipH = 20.0f, Gap = 4.0f, FontSize = 10.5f, TitleW = 80.0f;
		// returns the clicked chip (-1 none, -2 "any"), Click 2 = right click
		const auto ChipRow = [&](const char *pTitle, const std::vector<SChip> &vChips, const std::function<bool(int)> &IsActive, bool AnyActive, unsigned char *pIds, int &Click) {
			const float Left = MainView.x + TitleW, Right = MainView.x + MainView.w;
			const auto Width = [&](int k) {
				if(k < 0)
					return DisplayWidth(TextRender(), Localize("Any"), FontSize, 700) + 18.0f;
				const SChip &Chip = vChips[k];
				return DisplayWidth(TextRender(), Chip.m_pLabel, FontSize, 700) + 18.0f + (Chip.m_Pips ? 4 * 4.0f + 4.0f : 0.0f) + (Chip.m_pIcon ? 13.0f : 0.0f);
			};
			std::vector<CUIRect> vRects(vChips.size() + 1);
			float x = Left, y = MainView.y;
			for(int k = -1; k < (int)vChips.size(); k++)
			{
				const float w = Width(k);
				if(x + w > Right && x > Left)
				{
					x = Left;
					y += ChipH + Gap;
				}
				vRects[k + 1] = {x, y, w, ChipH};
				x += w + Gap;
			}
			CUIRect Rows, Title;
			MainView.HSplitTop(y + ChipH - MainView.y, &Rows, &MainView);
			MainView.HSplitTop(4.0f, nullptr, &MainView);
			Rows.VSplitLeft(TitleW, &Title, nullptr);
			Title.h = ChipH;
			FogLabel(Ui(), TextRender(), Title, pTitle, 9.5f, TEXTALIGN_ML, DIM, "Manrope", 700);

			int Clicked = -1;
			Click = 0;
			for(int k = -1; k < (int)vChips.size(); k++)
			{
				const CUIRect R = vRects[k + 1];
				const void *pId = &pIds[k + 1];
				const bool Active = k < 0 ? !AnyActive : IsActive(k);
				const int Result = Ui()->DoButtonLogic(pId, 0, &R, k < 0 ? BUTTONFLAG_LEFT : BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
				if(Result)
				{
					Clicked = k < 0 ? -2 : k;
					Click = Result;
				}
				const bool Hot = Ui()->HotItem() == pId;
				const ColorRGBA Tint = k < 0 ? ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f) : vChips[k].m_Color;
				const ColorRGBA Top = Active ? Tint.WithAlpha(0.20f + (Hot ? 0.05f : 0.0f)) : ColorRGBA(1.0f, 1.0f, 1.0f, Hot ? 0.08f : 0.035f);
				const ColorRGBA Bottom = Top.WithMultipliedAlpha(0.55f);
				Graphics()->TextureClear();
				Graphics()->DrawRect4(R.x, R.y, R.w, R.h, Top, Top, Bottom, Bottom, IGraphics::CORNER_ALL, R.h / 2.0f);
				CUIRect Content;
				R.VMargin(9.0f, &Content);
				const ColorRGBA TextColor = Active ? (k < 0 ? FROST : Lerp(Tint, FROST, 0.45f)) : (Hot ? ColorRGBA(0.82f, 0.85f, 0.89f, 1.0f) : MIST.WithAlpha(0.85f));
				if(k >= 0 && vChips[k].m_pIcon)
				{
					CUIRect Icon;
					Content.VSplitLeft(13.0f, &Icon, &Content);
					IconLabel(TextRender(), Ui(), Icon, vChips[k].m_pIcon, 7.5f, TextColor, TEXTALIGN_ML);
				}
				if(k >= 0 && vChips[k].m_Pips)
				{
					// difficulty steps: filled pips up to the level, in the level colour
					CUIRect Pips;
					Content.VSplitRight(4 * 4.0f, &Content, &Pips);
					for(int p = 0; p < 4; p++)
					{
						const bool On = p < vChips[k].m_Pips;
						const ColorRGBA PipColor = On ? Tint.WithAlpha(Active || Hot ? 1.0f : 0.75f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.12f);
						Fill(Graphics(), {Pips.x + p * 4.0f, Pips.y + Pips.h / 2.0f - 3.5f, 2.5f, 7.0f}, PipColor, 1.0f);
					}
					Content.VSplitRight(4.0f, &Content, nullptr);
				}
				FogLabel(Ui(), TextRender(), Content, k < 0 ? Localize("Any") : vChips[k].m_pLabel, FontSize, TEXTALIGN_ML, TextColor, "Manrope", Active ? 700 : 600);
				if(k >= 0 && vChips[k].m_pHint)
					Ui()->DoToolTip(pId, &R, vChips[k].m_pHint);
			}
			return Clicked;
		};
		// mask chips: left click keeps only that one (again: show all), right click adds or removes
		const auto ApplyMask = [&](int &Mask, int Clicked, int Click, int NumBits) {
			if(Clicked == -1)
				return;
			if(Clicked == -2)
				Mask = 0;
			else if(Click == 2)
				Mask ^= 1 << Clicked;
			else
				Mask = Mask == (1 << Clicked) ? 0 : (1 << Clicked);
			if(Mask == (1 << NumBits) - 1)
				Mask = 0;
			Client()->ServerBrowserUpdate();
		};

		const std::vector<SChip> vDifficulties = {
			{"Novice", Localize("Easy maps, good for a first run with a friend"), ColorRGBA(0.56f, 0.80f, 0.68f, 1.0f), 1, nullptr},
			{"Moderate", Localize("Longer maps that expect clean basic moves"), ColorRGBA(0.84f, 0.80f, 0.54f, 1.0f), 2, nullptr},
			{"Brutal", Localize("Hard parts that need practised team play"), ColorRGBA(0.90f, 0.64f, 0.50f, 1.0f), 3, nullptr},
			{"Insane", Localize("The hardest pool, for very experienced teams"), ColorRGBA(0.88f, 0.52f, 0.62f, 1.0f), 4, nullptr},
		};
		static unsigned char s_aDifficultyIds[8];
		int Click;
		int Clicked = ChipRow(Localize("Difficulty"), vDifficulties, [](int k) { return (g_Config.m_FogBrDifficulty & (1 << k)) != 0; }, g_Config.m_FogBrDifficulty != 0, s_aDifficultyIds, Click);
		ApplyMask(g_Config.m_FogBrDifficulty, Clicked, Click, 4);

		const ColorRGBA Cat(0.70f, 0.76f, 0.86f, 1.0f);
		const std::vector<SChip> vCategories = {
			{"Solo", Localize("Maps you finish alone"), Cat, 0, nullptr},
			{"Dummy", Localize("Maps for you and your dummy"), Cat, 0, nullptr},
			{"Race", Localize("Race maps against the clock"), Cat, 0, nullptr},
			{"Fun", Localize("Fun maps, not ranked by difficulty"), Cat, 0, nullptr},
			{"DDmaX", Localize("Maps in the old DDmaX style"), Cat, 0, nullptr},
			{"Oldschool", Localize("Classic maps from the early days"), Cat, 0, nullptr},
		};
		static unsigned char s_aCategoryIds[8];
		Clicked = ChipRow(Localize("Category"), vCategories, [](int k) { return (g_Config.m_FogBrCategory & (1 << k)) != 0; }, g_Config.m_FogBrCategory != 0, s_aCategoryIds, Click);
		ApplyMask(g_Config.m_FogBrCategory, Clicked, Click, 6);

		const ColorRGBA Team(0.74f, 0.70f, 0.86f, 1.0f);
		const std::vector<SChip> vTeam = {
			{Localize("Alone"), Localize("Solo maps: one player is enough"), Team, 0, FontIcon::USER},
			{Localize("With a dummy"), Localize("One player and a dummy"), Team, 0, FontIcon::USER},
			{Localize("Team of 2+"), Localize("Team maps: you need at least one partner"), Team, 0, FontIcon::HEART},
		};
		static unsigned char s_aTeamIds[4];
		Clicked = ChipRow(Localize("Players needed"), vTeam, [](int k) { return g_Config.m_FogBrTeam == k + 1; }, g_Config.m_FogBrTeam != 0, s_aTeamIds, Click);
		if(Clicked != -1)
		{
			g_Config.m_FogBrTeam = Clicked == -2 || g_Config.m_FogBrTeam == Clicked + 1 ? 0 : Clicked + 1;
			Client()->ServerBrowserUpdate();
		}
		MainView.HSplitTop(2.0f, nullptr, &MainView);
	}

	// ---- sort: a pill slides between the keys ----
	MainView.HSplitTop(22.0f, &Toolbar, &MainView);
	{
		struct SSort
		{
			int m_Sort;
			const char *m_pLabel;
			int m_DefaultOrder;
		};
		const SSort aSorts[] = {
			{IServerBrowser::SORT_NUMPLAYERS, Localize("Players"), 1},
			{IServerBrowser::SORT_PING, Localize("Ping"), 0},
			{IServerBrowser::SORT_MAP, Localize("Map"), 0},
			{IServerBrowser::SORT_NAME, Localize("Name"), 0},
			{IServerBrowser::SORT_GAMETYPE, Localize("Type"), 0},
			{IServerBrowser::SORT_NUMFRIENDS, Localize("Friends"), 1},
		};
		static CButtonContainer s_aSortButtons[std::size(aSorts)];

		CUIRect Label;
		Toolbar.VSplitLeft(DisplayWidth(TextRender(), Localize("Sort by"), 9.5f, 600) + 10.0f, &Label, &Toolbar);
		FogLabel(Ui(), TextRender(), Label, Localize("Sort by"), 9.5f, TEXTALIGN_ML, DIM, "Manrope", 600);

		float x = Toolbar.x, ActiveX = -1.0f, ActiveW = 0.0f;
		std::vector<CUIRect> vRects;
		for(const SSort &Sort : aSorts)
		{
			const float w = DisplayWidth(TextRender(), Sort.m_pLabel, 10.5f, 700) + (g_Config.m_BrSort == Sort.m_Sort ? 28.0f : 18.0f);
			vRects.push_back({x, Toolbar.y + 1.0f, w, Toolbar.h - 2.0f});
			if(g_Config.m_BrSort == Sort.m_Sort)
				ActiveX = x, ActiveW = w;
			x += w + 2.0f;
		}
		if(ActiveX >= 0.0f)
		{
			if(m_FogSortX < 0.0f)
				m_FogSortX = ActiveX, m_FogSortW = ActiveW;
			FogSpring(m_FogSortX, m_FogSortXV, ActiveX, Dt, 4.5f, 0.45f);
			FogSpring(m_FogSortW, m_FogSortWV, ActiveW, Dt, 4.5f, 0.45f);
			const ColorRGBA Top(1.0f, 1.0f, 1.0f, 0.16f), Bottom(1.0f, 1.0f, 1.0f, 0.09f);
			Graphics()->TextureClear();
			Graphics()->DrawRect4(m_FogSortX, Toolbar.y + 1.0f, m_FogSortW, Toolbar.h - 2.0f, Top, Top, Bottom, Bottom, IGraphics::CORNER_ALL, (Toolbar.h - 2.0f) / 2.0f);
		}
		for(size_t i = 0; i < std::size(aSorts); i++)
		{
			const SSort &Sort = aSorts[i];
			const CUIRect &R = vRects[i];
			const bool Active = g_Config.m_BrSort == Sort.m_Sort;
			if(Ui()->DoButtonLogic(&s_aSortButtons[i], 0, &R, BUTTONFLAG_LEFT))
			{
				if(Active)
					g_Config.m_BrSortOrder = g_Config.m_BrSortOrder ? 0 : 1;
				else
				{
					g_Config.m_BrSort = Sort.m_Sort;
					g_Config.m_BrSortOrder = Sort.m_DefaultOrder;
				}
				Client()->ServerBrowserUpdate();
			}
			const bool Hot = Ui()->HotItem() == &s_aSortButtons[i];
			if(Hot && !Active)
				Fill(Graphics(), R, ColorRGBA(1.0f, 1.0f, 1.0f, 0.05f), R.h / 2.0f);
			CUIRect Text = R, Arrow;
			Text.VMargin(9.0f, &Text);
			if(Active)
			{
				Text.VSplitRight(10.0f, &Text, &Arrow);
				IconLabel(TextRender(), Ui(), Arrow, g_Config.m_BrSortOrder ? FontIcon::CHEVRON_DOWN : FontIcon::CHEVRON_UP, 6.5f, FROST);
			}
			FogLabel(Ui(), TextRender(), Text, Sort.m_pLabel, 10.5f, TEXTALIGN_ML, Active ? FROST : (Hot ? ColorRGBA(0.82f, 0.85f, 0.89f, 1.0f) : MIST), "Manrope", Active ? 700 : 600);
		}
	}
	MainView.HSplitTop(5.0f, nullptr, &MainView);
	List = MainView;

	// ---- the cards ----
	const int NumServers = ServerBrowser()->NumSortedServers();
	static CListBox s_ListBox;
	s_ListBox.SetDrawHighlights(false);
	s_ListBox.DoAutoSpacing(4.0f);
	s_ListBox.SetActive(!Ui()->IsPopupOpen());
	s_ListBox.DoStart(34.0f, NumServers, 1, 3, -1, &List, false);
	if(m_ServerBrowserShouldRevealSelection)
	{
		s_ListBox.ScrollToSelected();
		m_ServerBrowserShouldRevealSelection = false;
	}
	m_SelectedIndex = -1;

	std::vector<char> &vIds = m_vServerbrowserElementIds[ServerBrowser()->GetCurrentType()];
	if(vIds.size() < (size_t)NumServers)
		vIds.resize(NumServers);

	int VisibleIndex = 0;
	for(int i = 0; i < NumServers; i++)
	{
		const CServerInfo *pItem = ServerBrowser()->SortedGet(i);
		const CListboxItem ListItem = s_ListBox.DoNextItem(&vIds[i], str_comp(pItem->m_aAddress, g_Config.m_UiServerAddress) == 0, 9.0f);
		if(ListItem.m_Selected)
			m_SelectedIndex = i;
		if(!ListItem.m_Visible)
		{
			if(Ui()->CheckActiveItem(&vIds[i]))
				Ui()->SetActiveItem(nullptr);
			continue;
		}
		// cards arrive one after another when the page opens
		const float In = std::clamp(((Time - m_FogBrowserInTime) * AnimSpeed - VisibleIndex * 0.035f) * 3.2f, 0.0f, 1.0f);
		VisibleIndex++;
		RenderFogServerCard(ListItem.m_Rect, pItem, ListItem.m_Selected, Ui()->HotItem() == &vIds[i], In);
	}

	const int NewSelected = s_ListBox.DoEnd();
	if(NewSelected != m_SelectedIndex)
	{
		m_SelectedIndex = NewSelected;
		if(m_SelectedIndex >= 0)
		{
			if(const CServerInfo *pItem = ServerBrowser()->SortedGet(NewSelected))
			{
				str_copy(g_Config.m_UiServerAddress, pItem->m_aAddress);
				m_ServerBrowserShouldRevealSelection = true;
			}
		}
	}
	if(s_ListBox.WasItemActivated())
		Connect(g_Config.m_UiServerAddress);

	// empty states say what happened and what to do
	if(NumServers == 0)
	{
		CUIRect Message, Action;
		List.HMargin((List.h - 90.0f) / 2.0f, &Message);
		FogBrand::DrawMark(Graphics(), vec2(Message.x + Message.w / 2.0f, Message.y + 14.0f), 34.0f, MIST.WithAlpha(0.5f));
		Message.HSplitTop(40.0f, nullptr, &Message);
		Message.HSplitTop(22.0f, &Message, &Action);
		Action.HSplitTop(10.0f, nullptr, &Action);
		Action.HSplitTop(30.0f, &Action, nullptr);
		Action.VMargin((Action.w - 260.0f) / 2.0f, &Action);
		const char *pMessage = Localize("No servers found");
		if(!ServerBrowser()->NumServers() && ServerBrowser()->IsGettingServerlist())
			pMessage = Localize("Getting server list from master server");
		else if(!ServerBrowser()->NumServers() && ServerBrowser()->IsServerlistError())
			pMessage = Localize("Could not get server list from master server");
		else if(ServerBrowser()->NumServers())
			pMessage = Localize("No servers match your filter criteria");
		FogLabel(Ui(), TextRender(), Message, pMessage, 15.0f, TEXTALIGN_MC, MIST, "Manrope", 600);
		if(ServerBrowser()->NumServers())
		{
			static CButtonContainer s_ResetButton;
			if(DoButton_Menu(&s_ResetButton, Localize("Reset filter"), 0, &Action, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, Action.h / 2.0f))
				ResetServerbrowserFilters();
		}
		else if(ServerBrowser()->GetCurrentType() == IServerBrowser::TYPE_LAN)
		{
			static CButtonContainer s_LocalServerButton;
			if(DoButton_Menu(&s_LocalServerButton, Localize("Start and connect to local server"), 0, &Action, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, Action.h / 2.0f))
			{
				const bool WasRunning = GameClient()->m_LocalServer.IsServerRunning();
				if(WasRunning)
					RefreshBrowserTab(true);
				if(WasRunning || GameClient()->m_LocalServer.RunServer({}))
					GameClient()->m_LocalServer.Connect();
			}
		}
	}

	if(!Ui()->IsPopupOpen() && CLineInput::GetActiveInput() == nullptr && Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		Connect(g_Config.m_UiServerAddress);

	RenderFogServerPanel(Panel);
}

void CMenus::RenderFogServerPanel(CUIRect Panel)
{
	const float Time = Client()->GlobalTime();
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	Fill(Graphics(), Panel, ColorRGBA(0.0f, 0.01f, 0.03f, 0.16f), 12.0f);
	Panel.Margin(8.0f, &Panel);

	// segmented control: server, filters, friends
	if(!Ui()->IsPopupOpen() && Ui()->ConsumeHotkey(CUi::HOTKEY_TAB))
	{
		const int Direction = Input()->ShiftIsPressed() ? -1 : 1;
		g_Config.m_UiToolboxPage = (g_Config.m_UiToolboxPage + NUM_UI_TOOLBOX_PAGES + Direction) % NUM_UI_TOOLBOX_PAGES;
	}
	{
		CUIRect Tabs;
		Panel.HSplitTop(22.0f, &Tabs, &Panel);
		Panel.HSplitTop(8.0f, nullptr, &Panel);
		Fill(Graphics(), Tabs, ColorRGBA(0.0f, 0.01f, 0.03f, 0.22f), Tabs.h / 2.0f);
		const int aPages[] = {UI_TOOLBOX_PAGE_INFO, UI_TOOLBOX_PAGE_FILTERS, UI_TOOLBOX_PAGE_FRIENDS};
		const char *apLabels[] = {Localize("Server"), Localize("Filters"), Localize("Friends")};
		static CButtonContainer s_aTabs[3];
		const float w = (Tabs.w - 4.0f) / 3.0f;
		for(int i = 0; i < 3; i++)
			if(g_Config.m_UiToolboxPage == aPages[i])
			{
				const float Target = Tabs.x + 2.0f + i * w;
				if(m_FogPanelTabX < 0.0f)
					m_FogPanelTabX = Target;
				FogSpring(m_FogPanelTabX, m_FogPanelTabV, Target, Dt, 4.5f, 0.45f);
			}
		const ColorRGBA Top(1.0f, 1.0f, 1.0f, 0.17f), Bottom(1.0f, 1.0f, 1.0f, 0.10f);
		Graphics()->TextureClear();
		Graphics()->DrawRect4(m_FogPanelTabX, Tabs.y + 2.0f, w, Tabs.h - 4.0f, Top, Top, Bottom, Bottom, IGraphics::CORNER_ALL, (Tabs.h - 4.0f) / 2.0f);
		for(int i = 0; i < 3; i++)
		{
			const CUIRect R = {Tabs.x + 2.0f + i * w, Tabs.y + 2.0f, w, Tabs.h - 4.0f};
			const bool Active = g_Config.m_UiToolboxPage == aPages[i];
			if(Ui()->DoButtonLogic(&s_aTabs[i], 0, &R, BUTTONFLAG_LEFT))
				g_Config.m_UiToolboxPage = aPages[i];
			const bool Hot = Ui()->HotItem() == &s_aTabs[i];
			FogLabel(Ui(), TextRender(), R, apLabels[i], 10.0f, TEXTALIGN_MC, Active ? FROST : (Hot ? ColorRGBA(0.82f, 0.85f, 0.89f, 1.0f) : MIST), "Manrope", Active ? 700 : 600);
		}
	}

	if(g_Config.m_UiToolboxPage == UI_TOOLBOX_PAGE_FILTERS)
	{
		RenderServerbrowserFilters(Panel);
		return;
	}
	if(g_Config.m_UiToolboxPage == UI_TOOLBOX_PAGE_FRIENDS)
	{
		RenderServerbrowserFriends(Panel);
		return;
	}

	const CServerInfo *pServer = ServerBrowser()->SortedGet(m_SelectedIndex);
	if(!pServer)
	{
		CUIRect Message = Panel;
		Message.HMargin((Message.h - 80.0f) / 2.0f, &Message);
		FogBrand::DrawMark(Graphics(), vec2(Message.x + Message.w / 2.0f, Message.y + 14.0f), 30.0f, MIST.WithAlpha(0.45f));
		Message.HSplitTop(40.0f, nullptr, &Message);
		SLabelProperties Props;
		Props.m_MaxWidth = Message.w;
		TextRender()->TextColor(MIST);
		FogLabel(Ui(), TextRender(), Message, Localize("Pick a server to see its map and who is playing."), 10.5f, TEXTALIGN_TC, MIST, "Manrope", 500);
		return;
	}

	// the panel content settles in whenever another server is picked
	if(pServer->m_ServerIndex != m_FogPanelLastServer)
	{
		m_FogPanelLastServer = pServer->m_ServerIndex;
		m_FogPanelInTime = Time;
	}
	const float Speed = g_Config.m_FogAnim ? std::clamp(g_Config.m_FogAnimSpeed / 100.0f, 0.1f, 10.0f) : 1000.0f;
	const auto Stage = [&](float Delay) {
		return std::clamp(((Time - m_FogPanelInTime) * Speed - Delay) * 3.5f, 0.0f, 1.0f);
	};

	// map as the headline, the server name under it
	CUIRect Map, Name, Chips;
	Panel.HSplitTop(26.0f, &Map, &Panel);
	Panel.HSplitTop(15.0f, &Name, &Panel);
	Panel.HSplitTop(6.0f, nullptr, &Panel);
	Panel.HSplitTop(13.0f, &Chips, &Panel);
	Panel.HSplitTop(10.0f, nullptr, &Panel);
	{
		const float a = Stage(0.0f);
		Map.y += (1.0f - FogEaseOutBack(a)) * 10.0f;
		FogLabel(Ui(), TextRender(), Map, pServer->m_aMap, 20.0f, TEXTALIGN_ML, FROST.WithAlpha(std::min(a * 1.5f, 1.0f)), "Manrope", 800);
		const float b = Stage(0.06f);
		Name.y += (1.0f - FogEaseOutBack(b)) * 10.0f;
		BodyLabel(TextRender(), Ui(), Name, pServer->m_aName, 10.5f, TEXTALIGN_ML, MIST.WithAlpha(std::min(b * 1.5f, 1.0f)));

		float x = Chips.x;
		const auto Chip = [&](const char *pText, ColorRGBA Color) {
			const float w = DisplayWidth(TextRender(), pText, 8.0f, 700) + 10.0f;
			if(x + w > Chips.x + Chips.w)
				return;
			Fill(Graphics(), {x, Chips.y, w, Chips.h}, Color.WithAlpha(0.14f), Chips.h / 2.0f);
			FogLabel(Ui(), TextRender(), {x, Chips.y, w, Chips.h}, pText, 8.0f, TEXTALIGN_MC, Color, "Manrope", 700);
			x += w + 4.0f;
		};
		if(pServer->m_aGameType[0])
			Chip(pServer->m_aGameType, g_Config.m_UiColorizeGametype ? Muted(pServer->m_GametypeColor) : MIST);
		if(const CCommunity *pCommunity = ServerBrowser()->Community(pServer->m_aCommunityId); pCommunity && str_comp_nocase(pCommunity->Name(), pServer->m_aGameType) != 0)
			Chip(pCommunity->Name(), MIST);
		if(pServer->m_aVersion[0])
			Chip(pServer->m_aVersion, DIM);
	}

	// two quiet stat tiles
	{
		CUIRect Tiles, PlayersTile, PingTile;
		Panel.HSplitTop(40.0f, &Tiles, &Panel);
		Panel.HSplitTop(8.0f, nullptr, &Panel);
		Tiles.VSplitMid(&PlayersTile, &PingTile, 6.0f);
		const auto Tile = [&](CUIRect R, const char *pValue, const char *pLabel, ColorRGBA Color, float Delay) {
			const float a = Stage(Delay);
			R.y += (1.0f - FogEaseOutBack(a)) * 12.0f;
			const float Alpha = std::min(a * 1.5f, 1.0f);
			const ColorRGBA Top(1.0f, 1.0f, 1.0f, 0.07f * Alpha), Bottom(1.0f, 1.0f, 1.0f, 0.035f * Alpha);
			Graphics()->TextureClear();
			Graphics()->DrawRect4(R.x, R.y, R.w, R.h, Top, Top, Bottom, Bottom, IGraphics::CORNER_ALL, 9.0f);
			CUIRect Value, Label;
			R.Margin(vec2(8.0f, 5.0f), &R);
			R.HSplitTop(18.0f, &Value, &Label);
			FogLabel(Ui(), TextRender(), Value, pValue, 14.5f, TEXTALIGN_ML, Color.WithAlpha(Alpha), "Manrope", 800);
			FogLabel(Ui(), TextRender(), Label, pLabel, 8.5f, TEXTALIGN_ML, DIM.WithAlpha(Alpha), "Manrope", 600);
		};
		char aPlayers[32], aPing[16];
		str_format(aPlayers, sizeof(aPlayers), "%d/%d", pServer->m_NumFilteredPlayers, ServerBrowser()->Max(*pServer));
		FormatServerbrowserPing(aPing, pServer);
		Tile(PlayersTile, aPlayers, Localize("Players"), FROST, 0.10f);
		Tile(PingTile, aPing, Localize("Ping"), PingColor(pServer->m_Latency), 0.14f);
	}

	// connect: the one strong control on the page; it lifts on hover and springs back
	{
		CUIRect Button, Secondary;
		Panel.HSplitTop(28.0f, &Button, &Panel);
		Panel.HSplitTop(6.0f, nullptr, &Panel);
		Panel.HSplitTop(19.0f, &Secondary, &Panel);
		Panel.HSplitTop(10.0f, nullptr, &Panel);

		static CButtonContainer s_ConnectButton;
		const bool Hot = Ui()->HotItem() == &s_ConnectButton;
		const bool Down = Ui()->CheckActiveItem(&s_ConnectButton);
		FogSpring(m_FogConnectLift, m_FogConnectLiftV, Down ? -1.0f : (Hot ? 1.0f : 0.0f), Dt, 5.0f, 0.5f);
		const float Lift = m_FogConnectLift;
		CUIRect Body = Button;
		Body.y -= Lift * 1.5f;
		Graphics()->TextureClear();
		Graphics()->DrawRect(Body.x - 2.0f, Body.y + 3.0f, Body.w + 4.0f, Body.h + 2.0f, ColorRGBA(0.0f, 0.02f, 0.05f, 0.18f + 0.06f * Lift), IGraphics::CORNER_ALL, Body.h / 2.0f + 2.0f);
		const ColorRGBA Top(0.80f, 0.86f, 0.92f, 0.86f + 0.08f * Lift), Bottom(0.62f, 0.70f, 0.78f, 0.86f + 0.08f * Lift);
		Graphics()->DrawRect4(Body.x, Body.y, Body.w, Body.h, Top, Top, Bottom, Bottom, IGraphics::CORNER_ALL, Body.h / 2.0f);
		Graphics()->DrawRect(Body.x + Body.h / 2.0f, Body.y + 1.0f, Body.w - Body.h, 1.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.45f), IGraphics::CORNER_NONE, 0.0f);
		CUIRect Icon, Label;
		const float LabelW = DisplayWidth(TextRender(), Localize("Connect"), 12.0f, 800);
		Body.VMargin((Body.w - LabelW - 18.0f) / 2.0f, &Label);
		Label.VSplitLeft(18.0f, &Icon, &Label);
		IconLabel(TextRender(), Ui(), Icon, FontIcon::RIGHT_TO_BRACKET, 9.5f, ColorRGBA(0.08f, 0.10f, 0.13f, 1.0f), TEXTALIGN_ML);
		FogLabel(Ui(), TextRender(), Label, Localize("Connect"), 12.0f, TEXTALIGN_ML, ColorRGBA(0.08f, 0.10f, 0.13f, 1.0f), "Manrope", 800);
		if(Ui()->DoButtonLogic(&s_ConnectButton, 0, &Button, BUTTONFLAG_LEFT))
			Connect(g_Config.m_UiServerAddress);

		// favourite and copy, as quiet text buttons
		CUIRect Favorite, Copy;
		Secondary.VSplitMid(&Favorite, &Copy, 6.0f);
		static CButtonContainer s_FavoriteButton, s_CopyButton;
		const bool IsFavorite = pServer->m_Favorite != TRISTATE::NONE;
		const auto TextButton = [&](CButtonContainer *pButton, CUIRect R, const char *pIcon, const char *pText, ColorRGBA IconColor) {
			const bool ButtonHot = Ui()->HotItem() == pButton;
			Fill(Graphics(), R, ColorRGBA(1.0f, 1.0f, 1.0f, ButtonHot ? 0.10f : 0.045f), R.h / 2.0f);
			CUIRect IconRect, Text;
			const float w = DisplayWidth(TextRender(), pText, 9.5f, 600);
			R.VMargin(std::max((R.w - w - 15.0f) / 2.0f, 4.0f), &Text);
			Text.VSplitLeft(15.0f, &IconRect, &Text);
			IconLabel(TextRender(), Ui(), IconRect, pIcon, 7.5f, IconColor, TEXTALIGN_ML);
			FogLabel(Ui(), TextRender(), Text, pText, 9.5f, TEXTALIGN_ML, ButtonHot ? FROST : MIST, "Manrope", 600);
			return Ui()->DoButtonLogic(pButton, 0, &R, BUTTONFLAG_LEFT);
		};
		if(TextButton(&s_FavoriteButton, Favorite, FontIcon::STAR, IsFavorite ? Localize("In favorites") : Localize("Favorite"), IsFavorite ? ColorRGBA(0.92f, 0.84f, 0.60f, 1.0f) : MIST))
		{
			if(IsFavorite)
				Favorites()->Remove(pServer->m_aAddresses, pServer->m_NumAddresses);
			else
			{
				Favorites()->Add(pServer->m_aAddresses, pServer->m_NumAddresses);
				if(g_Config.m_UiPage == PAGE_LAN)
					Favorites()->AllowPing(pServer->m_aAddresses, pServer->m_NumAddresses, true);
			}
			Client()->ServerBrowserUpdate();
		}
		if(TextButton(&s_CopyButton, Copy, FontIcon::FILE, Localize("Copy"), MIST))
		{
			char aInfo[256];
			str_format(aInfo, sizeof(aInfo), "%s\nAddress: ddnet://%s\n", pServer->m_aName, pServer->m_aAddress);
			Input()->SetClipboardText(aInfo);
		}
	}

	// who is there
	{
		CUIRect Title;
		Panel.HSplitTop(14.0f, &Title, &Panel);
		Panel.HSplitTop(3.0f, nullptr, &Panel);
		FogLabel(Ui(), TextRender(), Title, Localize("Playing now"), 9.0f, TEXTALIGN_ML, DIM, "Manrope", 700);
		RenderServerbrowserInfoScoreboard(Panel, pServer);
	}
}
