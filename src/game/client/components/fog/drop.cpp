// Fog client: drag and drop. Drop files onto the window: a PNG opens a small glass card that
// asks where it belongs (skin, game skin, emoticons, particles, HUD, entities, extras or a tee
// pattern) with a guess already picked; the file is copied into place and switched on right
// away. Demos and maps keep the engine's own handling (play / open in the editor).

#include "glass.h"
#include "tee_style.h"

#include <base/fs.h>
#include <base/io.h>
#include <base/str.h>

#include <engine/font_icons.h>
#include <engine/image.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>

namespace {

constexpr ColorRGBA FROST(0.91f, 0.93f, 0.95f, 1.0f);
constexpr ColorRGBA MIST(0.64f, 0.68f, 0.74f, 1.0f);
constexpr ColorRGBA DIM(0.46f, 0.50f, 0.56f, 1.0f);

enum
{
	DEST_SKIN = 0,
	DEST_GAME,
	DEST_EMOTICONS,
	DEST_PARTICLES,
	DEST_HUD,
	DEST_ENTITIES,
	DEST_EXTRAS,
	DEST_PATTERN,
	NUM_DESTS,
};

// matches ASSETS_TAB_* in menus_settings_assets.cpp
int AssetTab(int Dest)
{
	switch(Dest)
	{
	case DEST_ENTITIES: return 0;
	case DEST_GAME: return 1;
	case DEST_EMOTICONS: return 2;
	case DEST_PARTICLES: return 3;
	case DEST_HUD: return 4;
	case DEST_EXTRAS: return 5;
	default: return -1;
	}
}

bool CopyAbsoluteToStorage(IStorage *pStorage, const char *pFrom, const char *pTo)
{
	IOHANDLE In = io_open(pFrom, IOFLAG_READ);
	if(!In)
		return false;
	void *pData = nullptr;
	unsigned Size = 0;
	const bool Read = io_read_all(In, &pData, &Size);
	io_close(In);
	if(!Read)
		return false;
	IOHANDLE Out = pStorage->OpenFile(pTo, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	bool Ok = false;
	if(Out)
	{
		Ok = io_write(Out, pData, Size) == Size;
		io_close(Out);
	}
	free(pData);
	return Ok;
}

} // namespace

void CMenus::FogHandleDrop(const char *pPath)
{
	m_vFogDropQueue.emplace_back(pPath);
	if(!IsActive())
	{
		SetActive(true);
		m_FogDropOpenedMenu = true; // dropped in game: give the game back when done
	}
}

void CMenus::FogStartNextDrop()
{
	Graphics()->UnloadTexture(&m_FogDropTexture);
	m_FogDropPath.clear();
	while(!m_vFogDropQueue.empty() && m_FogDropPath.empty())
	{
		const std::string Path = m_vFogDropQueue.front();
		m_vFogDropQueue.erase(m_vFogDropQueue.begin());
		if(fs_is_dir(Path.c_str()) || !str_endswith_nocase(Path.c_str(), ".png"))
		{
			str_format(m_aFogDropStatus, sizeof(m_aFogDropStatus), "%s", Localize("Drop a PNG (skin, asset, pattern), a demo or a map"));
			m_FogDropStatusTime = Client()->GlobalTime();
			continue;
		}
		CImageInfo Image;
		if(!Graphics()->LoadPng(Image, Path.c_str(), IStorage::TYPE_ABSOLUTE))
		{
			str_copy(m_aFogDropStatus, Localize("Could not read that picture"));
			m_FogDropStatusTime = Client()->GlobalTime();
			continue;
		}
		m_FogDropWidth = (int)Image.m_Width;
		m_FogDropHeight = (int)Image.m_Height;
		m_FogDropTexture = Graphics()->LoadTextureRawMove(Image, 0, "fog_drop_preview");
		m_FogDropPath = Path;

		// a name from the file, safe for the folders
		const char *pBase = fs_filename(Path.c_str());
		char aName[128];
		str_copy(aName, pBase, std::min((int)sizeof(aName), str_length(pBase) - 4 + 1));
		str_sanitize_filename(aName);
		m_FogDropName = aName;

		// guess where it goes: names first, then the shape of the picture
		char aLower[128];
		str_copy(aLower, aName);
		for(char *p = aLower; *p; p++)
			*p = (char)tolower((unsigned char)*p);
		const bool Wide = m_FogDropWidth == m_FogDropHeight * 2;
		if(str_find(aLower, "game"))
			m_FogDropGuess = DEST_GAME;
		else if(str_find(aLower, "emot"))
			m_FogDropGuess = DEST_EMOTICONS;
		else if(str_find(aLower, "particle"))
			m_FogDropGuess = DEST_PARTICLES;
		else if(str_find(aLower, "hud"))
			m_FogDropGuess = DEST_HUD;
		else if(str_find(aLower, "entit"))
			m_FogDropGuess = DEST_ENTITIES;
		else if(str_find(aLower, "extra"))
			m_FogDropGuess = DEST_EXTRAS;
		else if(str_find(aLower, "pattern"))
			m_FogDropGuess = DEST_PATTERN;
		else if(Wide && m_FogDropWidth <= 1024)
			m_FogDropGuess = DEST_SKIN;
		else if(Wide)
			m_FogDropGuess = DEST_GAME;
		else if(m_FogDropWidth == m_FogDropHeight && m_FogDropWidth >= 1024)
			m_FogDropGuess = DEST_ENTITIES;
		else if(m_FogDropWidth == m_FogDropHeight)
			m_FogDropGuess = DEST_EMOTICONS;
		else
			m_FogDropGuess = DEST_PATTERN;
		m_FogDropOpenTime = Client()->GlobalTime();
	}
}

bool CMenus::FogInstallDrop(int Dest)
{
	const char *pName = m_FogDropName.c_str();
	char aPath[IO_MAX_PATH_LENGTH];
	static const char *const s_apAssetFolders[] = {"", "assets/game", "assets/emoticons", "assets/particles", "assets/hud", "assets/entities", "assets/extras", "fog/patterns"};
	if(Dest == DEST_SKIN)
	{
		Storage()->CreateFolder("skins", IStorage::TYPE_SAVE);
		str_format(aPath, sizeof(aPath), "skins/%s.png", pName);
	}
	else
	{
		Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
		Storage()->CreateFolder("fog", IStorage::TYPE_SAVE);
		Storage()->CreateFolder(s_apAssetFolders[Dest], IStorage::TYPE_SAVE);
		str_format(aPath, sizeof(aPath), "%s/%s.png", s_apAssetFolders[Dest], pName);
	}
	if(!CopyAbsoluteToStorage(Storage(), m_FogDropPath.c_str(), aPath))
	{
		str_format(m_aFogDropStatus, sizeof(m_aFogDropStatus), Localize("Could not write %s"), aPath);
		return false;
	}

	// switch it on
	switch(Dest)
	{
	case DEST_SKIN:
		GameClient()->RefreshSkins(CSkinDescriptor::FLAG_SIX);
		str_copy(g_Config.m_ClPlayerSkin, pName);
		g_Config.m_ClPlayerUseCustomColor = 0;
		SetNeedSendInfo();
		break;
	case DEST_GAME: str_copy(g_Config.m_ClAssetGame, pName); break;
	case DEST_EMOTICONS: str_copy(g_Config.m_ClAssetEmoticons, pName); break;
	case DEST_PARTICLES: str_copy(g_Config.m_ClAssetParticles, pName); break;
	case DEST_HUD: str_copy(g_Config.m_ClAssetHud, pName); break;
	case DEST_ENTITIES: str_copy(g_Config.m_ClAssetsEntities, pName); break;
	case DEST_EXTRAS: str_copy(g_Config.m_ClAssetExtras, pName); break;
	case DEST_PATTERN:
		str_copy(g_Config.m_FogTeePatternFile, pName);
		g_Config.m_FogTeePattern = 9;
		g_Config.m_FogTeeStyle = 1;
		GameClient()->m_FogTeeStyle.ScanPatterns();
		break;
	default: break;
	}
	if(AssetTab(Dest) >= 0)
		ClearCustomItems(AssetTab(Dest)); // refreshes the list and reloads the chosen asset
	str_format(m_aFogDropStatus, sizeof(m_aFogDropStatus), Localize("Installed %s and switched it on"), pName);
	return true;
}

void CMenus::RenderFogDrop()
{
	const float Now = Client()->GlobalTime();
	if(m_FogDropPath.empty() && !m_vFogDropQueue.empty())
		FogStartNextDrop();

	const CUIRect Screen = *Ui()->Screen();
	if(!m_FogDropPath.empty())
	{
		const float In = FogEaseOutBack(std::min((Now - m_FogDropOpenTime) * 3.5f, 1.0f));
		Graphics()->TextureClear();
		Graphics()->DrawRect(Screen.x, Screen.y, Screen.w, Screen.h, ColorRGBA(0.02f, 0.03f, 0.05f, 0.7f * std::min(In, 1.0f)), IGraphics::CORNER_NONE, 0.0f);
		// an invisible catcher over the whole screen: nothing under the card reacts to the mouse
		static CButtonContainer s_Catcher;
		Ui()->DoButtonLogic(&s_Catcher, 0, &Screen, BUTTONFLAG_LEFT);

		CUIRect Card = {Screen.w / 2.0f - 240.0f, Screen.h / 2.0f - 130.0f + (1.0f - In) * 24.0f, 480.0f, 260.0f};
		Graphics()->DrawRect(Card.x, Card.y, Card.w, Card.h, ColorRGBA(0.10f, 0.11f, 0.13f, 0.85f * std::min(In, 1.0f)), IGraphics::CORNER_ALL, 22.0f);
		FogDrawGlass(Graphics(), Card, 22.0f, std::min(In, 1.0f));
		CUIRect Inner, Preview, Right, Title, Sub;
		Card.Margin(vec2(20.0f, 18.0f), &Inner);
		Inner.VSplitLeft(130.0f, &Preview, &Right);
		Right.VSplitLeft(16.0f, nullptr, &Right);

		// preview of the picture, fitted, on a quiet checkerboard
		{
			CUIRect Box;
			Preview.HSplitTop(130.0f, &Box, nullptr);
			Graphics()->DrawRect(Box.x, Box.y, Box.w, Box.h, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 12.0f);
			const float Aspect = m_FogDropHeight > 0 ? (float)m_FogDropWidth / m_FogDropHeight : 1.0f;
			float w = Box.w - 16.0f, h = w / Aspect;
			if(h > Box.h - 16.0f)
			{
				h = Box.h - 16.0f;
				w = h * Aspect;
			}
			Graphics()->TextureSet(m_FogDropTexture);
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			IGraphics::CQuadItem Item(Box.x + (Box.w - w) / 2.0f, Box.y + (Box.h - h) / 2.0f, w, h);
			Graphics()->QuadsDrawTL(&Item, 1);
			Graphics()->QuadsEnd();
			char aSize[32];
			str_format(aSize, sizeof(aSize), "%d×%d", m_FogDropWidth, m_FogDropHeight);
			CUIRect SizeLabel = {Box.x, Box.y + Box.h + 6.0f, Box.w, 14.0f};
			FogLabel(Ui(), TextRender(), SizeLabel, aSize, 10.0f, TEXTALIGN_MC, DIM, "Manrope", 600);
		}

		Right.HSplitTop(24.0f, &Title, &Right);
		Right.HSplitTop(16.0f, &Sub, &Right);
		Right.HSplitTop(10.0f, nullptr, &Right);
		char aTitle[192];
		str_format(aTitle, sizeof(aTitle), Localize("Where does %s go?"), m_FogDropName.c_str());
		FogLabel(Ui(), TextRender(), Title, aTitle, 16.0f, TEXTALIGN_ML, FROST, "Manrope", 800);
		FogLabel(Ui(), TextRender(), Sub, Localize("It is copied there and switched on right away."), 10.0f, TEXTALIGN_ML, DIM, "Manrope", 500);

		const char *apDests[] = {Localize("Skin"), Localize("Game skin"), Localize("Emoticons"), Localize("Particles"), Localize("HUD"), Localize("Entities"), Localize("Extras"), Localize("Tee pattern")};
		static CButtonContainer s_aDestButtons[NUM_DESTS], s_CancelButton;
		const float Gap = 6.0f, ChipH = 26.0f, ChipW = (Right.w - Gap) / 2.0f;
		int Chosen = -1;
		for(int d = 0; d < NUM_DESTS; d++)
		{
			const CUIRect Chip = {Right.x + (d % 2) * (ChipW + Gap), Right.y + (d / 2) * (ChipH + Gap), ChipW, ChipH};
			const bool Hot = Ui()->HotItem() == &s_aDestButtons[d];
			const bool Guess = d == m_FogDropGuess;
			Graphics()->TextureClear();
			Graphics()->DrawRect(Chip.x, Chip.y, Chip.w, Chip.h, Guess ? ColorRGBA(0.85f, 0.9f, 0.97f, Hot ? 0.32f : 0.24f) : ColorRGBA(1.0f, 1.0f, 1.0f, Hot ? 0.12f : 0.05f), IGraphics::CORNER_ALL, ChipH / 2.0f);
			FogLabel(Ui(), TextRender(), Chip, apDests[d], 11.0f, TEXTALIGN_MC, Guess || Hot ? FROST : MIST, "Manrope", Guess ? 800 : 600);
			if(Ui()->DoButtonLogic(&s_aDestButtons[d], 0, &Chip, BUTTONFLAG_LEFT))
				Chosen = d;
		}
		CUIRect Bottom = {Right.x, Card.y + Card.h - 18.0f - 22.0f, Right.w, 22.0f}, Hint, Cancel;
		Bottom.VSplitRight(90.0f, &Hint, &Cancel);
		FogLabel(Ui(), TextRender(), Hint, Localize("Enter: the highlighted one. Esc: skip."), 9.0f, TEXTALIGN_ML, DIM, "Manrope", 500);
		const bool CancelHot = Ui()->HotItem() == &s_CancelButton;
		Graphics()->TextureClear();
		Graphics()->DrawRect(Cancel.x, Cancel.y, Cancel.w, Cancel.h, ColorRGBA(1.0f, 1.0f, 1.0f, CancelHot ? 0.12f : 0.05f), IGraphics::CORNER_ALL, Cancel.h / 2.0f);
		FogLabel(Ui(), TextRender(), Cancel, Localize("Skip"), 11.0f, TEXTALIGN_MC, MIST, "Manrope", 700);

		bool Close = false;
		if(Ui()->DoButtonLogic(&s_CancelButton, 0, &Cancel, BUTTONFLAG_LEFT) || m_FogDropEscape)
			Close = true;
		if(m_FogDropEnter)
			Chosen = m_FogDropGuess;
		if(Chosen >= 0)
		{
			FogInstallDrop(Chosen);
			m_FogDropStatusTime = Now;
			Close = true;
		}
		if(Close)
		{
			Graphics()->UnloadTexture(&m_FogDropTexture);
			m_FogDropPath.clear();
			if(m_vFogDropQueue.empty() && m_FogDropOpenedMenu)
			{
				m_FogDropOpenedMenu = false;
				if(Client()->State() == IClient::STATE_ONLINE)
					SetActive(false);
			}
		}
	}

	// a short line about what happened, at the bottom of the screen
	const float Since = Now - m_FogDropStatusTime;
	if(m_aFogDropStatus[0] && Since >= 0.0f && Since < 3.5f)
	{
		const float a = std::clamp(Since * 6.0f, 0.0f, 1.0f) * std::clamp((3.5f - Since) / 0.5f, 0.0f, 1.0f);
		const float W = TextRender()->TextWidth(11.0f, m_aFogDropStatus) + 40.0f;
		CUIRect Toast = {Screen.w / 2.0f - W / 2.0f, Screen.h - 50.0f, W, 26.0f};
		FogDrawGlass(Graphics(), Toast, 13.0f, a);
		FogLabel(Ui(), TextRender(), Toast, m_aFogDropStatus, 11.0f, TEXTALIGN_MC, FROST.WithAlpha(a), "Manrope", 700);
	}
}
