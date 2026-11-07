// Fog client: the race HUD. A glass capsule holds the timer at the top, checkpoint
// differences pop out under it on a spring, a finish gets its own moment in the middle of
// the screen, and the server and personal records sit in a quiet card on the left.

#include "brand.h"
#include "glass.h"

#include <base/math.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/protocol.h>

#include <game/client/components/hud.h>
#include <game/client/components/motd.h>
#include <game/client/components/voting.h>
#include <game/client/components/binds.h>
#include <game/client/gameclient.h>
#include <generated/client_data.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr ColorRGBA FROST(0.93f, 0.95f, 0.97f, 1.0f);
constexpr ColorRGBA MIST(0.68f, 0.72f, 0.78f, 1.0f);
constexpr ColorRGBA FASTER(0.58f, 0.86f, 0.72f, 1.0f);
constexpr ColorRGBA SLOWER(0.92f, 0.58f, 0.62f, 1.0f);

float DisplayWidth(ITextRender *pTextRender, const char *pText, float Size, int Weight)
{
	pTextRender->SetDisplayFont("Manrope", Weight);
	pTextRender->SetFontPreset(EFontPreset::DISPLAY_FONT);
	const float w = pTextRender->TextWidth(Size, pText);
	pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
	return w;
}

// dark smoked glass for the HUD: readable over bright and dark maps alike
void Capsule(IGraphics *pGraphics, float x, float y, float w, float h, float Alpha, ColorRGBA Tint = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f))
{
	pGraphics->TextureClear();
	pGraphics->DrawRect(x - 1.0f, y + 1.0f, w + 2.0f, h + 1.0f, ColorRGBA(0.0f, 0.01f, 0.03f, 0.18f * Alpha), IGraphics::CORNER_ALL, (h + 1.0f) / 2.0f);
	const ColorRGBA Top(0.10f + Tint.r * 0.10f, 0.11f + Tint.g * 0.10f, 0.14f + Tint.b * 0.10f, 0.62f * Alpha);
	const ColorRGBA Bottom(0.06f, 0.07f, 0.09f, 0.55f * Alpha);
	pGraphics->DrawRect4(x, y, w, h, Top, Top, Bottom, Bottom, IGraphics::CORNER_ALL, h / 2.0f);
	pGraphics->DrawRect(x + h / 2.0f, y + 0.4f, w - h, 0.5f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.14f * Alpha), IGraphics::CORNER_NONE, 0.0f);
}

// appear: a springy pop; then hold; then fade over the last two seconds of a six second life
void Life(float Seconds, float PopSpeed, float &Pop, float &Alpha)
{
	Pop = FogEaseOutBack(std::min(Seconds * PopSpeed, 1.0f));
	Alpha = std::clamp(Seconds * PopSpeed * 2.0f, 0.0f, 1.0f) * (Seconds < 4.0f ? 1.0f : std::clamp((6.0f - Seconds) / 2.0f, 0.0f, 1.0f));
}

// race times without padding: 0.42, 12.07, 1:23.45, 1:02:03.45
void FormatRaceTime(char *pBuf, int Size, int64_t Centis)
{
	Centis = std::max<int64_t>(Centis, 0);
	const int64_t Whole = Centis / 100, Frac = Centis % 100;
	if(Whole >= 3600)
		str_format(pBuf, Size, "%d:%02d:%02d.%02d", (int)(Whole / 3600), (int)(Whole / 60 % 60), (int)(Whole % 60), (int)Frac);
	else if(Whole >= 60)
		str_format(pBuf, Size, "%d:%02d.%02d", (int)(Whole / 60), (int)(Whole % 60), (int)Frac);
	else
		str_format(pBuf, Size, "%d.%02d", (int)Whole, (int)Frac);
}

void FormatDiff(char *pBuf, int Size, float Diff)
{
	char aTime[32];
	FormatRaceTime(aTime, sizeof(aTime), (int64_t)std::round(std::abs(Diff) * 100.0f));
	str_format(pBuf, Size, "%s%s", Diff < 0.0f ? "-" : (Diff > 0.0f ? "+" : ""), aTime);
}

} // namespace

void CHud::OnConsoleInit()
{
	Console()->Register("fog_clip", "", CFGFLAG_CLIENT, ConFogClip, this, "Save the last fog_clip_length seconds as a clip (demos/replays)");
	Console()->Register("fog_fx_apply", "i[preset]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *) {
		extern bool FogApplyFxPreset(int Index);
		FogApplyFxPreset(pResult->GetInteger(0));
	},
		this, "Apply a live shader preset by number (0 = Clean)");
	Console()->Register("fog_drop", "r[path]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUserData) {
		static_cast<CHud *>(pUserData)->GameClient()->m_Menus.FogHandleDrop(pResult->GetString(0));
	},
		this, "Act as if this file was dropped onto the window");
	Console()->Register("fog_hud_preview", "?i[kind]", CFGFLAG_CLIENT, ConFogHudPreview, this, "Preview the Fog HUD race moments: 0 checkpoint faster, 1 checkpoint slower, 2 finish with a new best, 3 finish slower, 4 a vote");
}

void CHud::ConFogHudPreview(IConsole::IResult *pResult, void *pUserData)
{
	CHud *pSelf = static_cast<CHud *>(pUserData);
	const int Kind = pResult->NumArguments() ? pResult->GetInteger(0) : 0;
	if(Kind == 4)
	{
		pSelf->GameClient()->m_Voting.FogPreview();
		return;
	}
	const int Tick = pSelf->Client()->GameTick(g_Config.m_ClDummy);
	pSelf->m_DDRaceTime = 8345; // 1:23.45
	pSelf->m_ShowFinishTime = Kind >= 2;
	if(Kind >= 2)
	{
		pSelf->m_FinishTimeDiff = Kind == 2 ? -1.27f : 0.84f;
		pSelf->m_FinishTimeLastReceivedTick = Tick;
	}
	else
	{
		pSelf->m_TimeCpDiff = Kind == 0 ? -0.42f : 0.31f;
		pSelf->m_TimeCpLastReceivedTick = Tick;
	}
}

float CHud::FogSecondsSince(int Tick) const
{
	const int Dummy = g_Config.m_ClDummy;
	return (Client()->GameTick(Dummy) - Tick + Client()->IntraGameTick(Dummy)) / (float)Client()->GameTickSpeed();
}

void CHud::FogRenderTimer()
{
	const CNetObj_GameInfo *pInfo = GameClient()->m_Snap.m_pGameInfoObj;
	if(pInfo->m_GameStateFlags & GAMESTATEFLAG_SUDDENDEATH)
		return;

	const int Dummy = g_Config.m_ClDummy;
	const float TickSpeed = Client()->GameTickSpeed();
	float Seconds;
	bool Race = false, Countdown = false;
	if(pInfo->m_TimeLimit && pInfo->m_WarmupTimer <= 0)
	{
		Countdown = true;
		Seconds = pInfo->m_TimeLimit * 60 - (Client()->GameTick(Dummy) - pInfo->m_RoundStartTick) / TickSpeed;
		if(pInfo->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER)
			Seconds = 0.0f;
	}
	else if(pInfo->m_GameStateFlags & GAMESTATEFLAG_RACETIME)
	{
		// the warmup timer carries the negative race start tick
		Race = true;
		Seconds = (Client()->GameTick(Dummy) + pInfo->m_WarmupTimer + Client()->IntraGameTick(Dummy)) / TickSpeed;
	}
	else
		Seconds = (Client()->GameTick(Dummy) - pInfo->m_RoundStartTick) / TickSpeed;
	Seconds = std::max(Seconds, 0.0f);

	// big minutes:seconds, small hundredths while racing
	char aMain[32], aFrac[8] = "";
	const int Whole = (int)Seconds;
	if(Whole >= 3600)
		str_format(aMain, sizeof(aMain), "%d:%02d:%02d", Whole / 3600, (Whole / 60) % 60, Whole % 60);
	else
		str_format(aMain, sizeof(aMain), "%d:%02d", Whole / 60, Whole % 60);
	if(Race)
		str_format(aFrac, sizeof(aFrac), ".%02d", std::clamp((int)((Seconds - Whole) * 100.0f), 0, 99));

	const float MainSize = 10.5f, FracSize = 7.0f, H = 17.0f;
	// width from the widest digits, so the capsule does not wobble as the time runs
	char aMeasure[32];
	str_copy(aMeasure, aMain);
	for(char *p = aMeasure; *p; p++)
		if(*p >= '0' && *p <= '9')
			*p = '0';
	const float MainW = DisplayWidth(TextRender(), aMeasure, MainSize, 800) + 1.0f;
	const float FracW = Race ? DisplayWidth(TextRender(), ".00", FracSize, 700) + 1.0f : 0.0f;
	const float W = MainW + FracW + 18.0f;
	const float X = m_Width / 2.0f - W / 2.0f, Y = 3.0f;

	ColorRGBA Color = FROST;
	ColorRGBA Tint(0.0f, 0.0f, 0.0f, 0.0f);
	if(Countdown && Seconds <= 60.0f)
	{
		// the last minute turns rose; the last ten seconds breathe
		Color = SLOWER;
		Tint = SLOWER;
		if(Seconds <= 10.0f)
			Color = Color.WithAlpha(0.65f + 0.35f * std::cos((float)(time_get() % time_freq()) / time_freq() * 2.0f * pi));
	}
	Capsule(Graphics(), X, Y, W, H, 1.0f, Tint);
	CUIRect MainRect = {X + 9.0f, Y, MainW, H};
	FogLabel(Ui(), TextRender(), MainRect, aMain, MainSize, TEXTALIGN_MC, Color, "Manrope", 800);
	if(Race)
	{
		CUIRect FracRect = {X + 9.0f + MainW, Y + 1.2f, FracW, H};
		FogLabel(Ui(), TextRender(), FracRect, aFrac, FracSize, TEXTALIGN_ML, MIST, "Manrope", 700);
	}
}

void CHud::FogRenderRaceEffects()
{
	if(!m_DDRaceTime)
		return;
	const float Cx = m_Width / 2.0f;

	if(m_ShowFinishTime)
	{
		const float Since = FogSecondsSince(m_FinishTimeLastReceivedTick);
		if(Since < 0.0f || Since >= 6.0f)
			return;
		float Pop, Alpha;
		Life(Since, 2.5f, Pop, Alpha);

		char aTime[32];
		FormatRaceTime(aTime, sizeof(aTime), m_DDRaceTime);
		const float Size = 22.0f * (0.75f + 0.25f * Pop);
		const float TimeW = DisplayWidth(TextRender(), aTime, Size, 800);
		const float Y = 48.0f + (1.0f - Pop) * 6.0f;
		const bool Better = m_FinishTimeDiff < 0.0f;

		// a soft halo of mist behind the time, tinted by how the run went
		const ColorRGBA Halo = m_FinishTimeDiff == 0.0f ? FROST : (Better ? FASTER : SLOWER);
		FogBrand::DrawMist(Graphics(), vec2(Cx, Y + 20.0f), vec2(TimeW * 2.2f + 60.0f, 70.0f), Halo.WithAlpha(0.20f * Alpha));
		// the time sits on smoked glass so it reads on bright and dark maps alike
		const float PlateW = TimeW + 34.0f;
		Capsule(Graphics(), Cx - PlateW / 2.0f, Y - 2.0f, PlateW, 35.0f, Alpha * 0.95f, Halo);

		CUIRect Label = {Cx - 100.0f, Y, 200.0f, 10.0f};
		FogLabel(Ui(), TextRender(), Label, Localize("Finish"), 7.0f, TEXTALIGN_MC, MIST.WithAlpha(Alpha), "Manrope", 700);
		CUIRect Time = {Cx - 150.0f, Y + 9.0f, 300.0f, 22.0f};
		FogLabel(Ui(), TextRender(), Time, aTime, Size, TEXTALIGN_MC, FROST.WithAlpha(Alpha), "Manrope", 800);

		if(m_FinishTimeDiff != 0.0f)
		{
			char aDiff[48];
			FormatDiff(aDiff, sizeof(aDiff), m_FinishTimeDiff);
			char aLine[96];
			str_format(aLine, sizeof(aLine), "%s  %s", aDiff, Better ? Localize("new personal best") : Localize("to your best"));
			const float ChipPop = FogEaseOutBack(std::clamp((Since - 0.25f) * 3.0f, 0.0f, 1.0f));
			const float ChipSize = 7.5f;
			const float ChipW = DisplayWidth(TextRender(), aLine, ChipSize, 700) + 14.0f;
			const float ChipY = Y + 35.0f + (1.0f - ChipPop) * 5.0f;
			const float ChipAlpha = Alpha * std::clamp((Since - 0.25f) * 6.0f, 0.0f, 1.0f);
			Capsule(Graphics(), Cx - ChipW / 2.0f, ChipY, ChipW, 12.0f, ChipAlpha, Halo);
			CUIRect Chip = {Cx - ChipW / 2.0f, ChipY, ChipW, 12.0f};
			FogLabel(Ui(), TextRender(), Chip, aLine, ChipSize, TEXTALIGN_MC, Halo.WithAlpha(ChipAlpha), "Manrope", 700);
		}
		return;
	}

	if(!g_Config.m_ClShowhudTimeCpDiff)
		return;
	const float Since = FogSecondsSince(m_TimeCpLastReceivedTick);
	if(Since < 0.0f || Since >= 6.0f)
		return;
	float Pop, Alpha;
	Life(Since, 4.0f, Pop, Alpha);

	char aDiff[48];
	FormatDiff(aDiff, sizeof(aDiff), m_TimeCpDiff);
	const ColorRGBA Color = m_TimeCpDiff < 0.0f ? FASTER : (m_TimeCpDiff > 0.0f ? SLOWER : FROST);
	const float Size = 8.0f * (0.7f + 0.3f * Pop);
	const float W = DisplayWidth(TextRender(), aDiff, Size, 800) + 14.0f;
	const float H = 12.0f * (0.8f + 0.2f * Pop);
	// it drops out from under the timer capsule
	const float Y = 23.0f + (1.0f - Pop) * -5.0f;
	Capsule(Graphics(), Cx - W / 2.0f, Y, W, H, Alpha, Color);
	CUIRect Chip = {Cx - W / 2.0f, Y, W, H};
	FogLabel(Ui(), TextRender(), Chip, aDiff, Size, TEXTALIGN_MC, Color.WithAlpha(Alpha), "Manrope", 800);
}

void CHud::FogRenderRecord()
{
	char aServer[32] = "", aPersonal[32] = "";
	if(GameClient()->m_MapBestTimeSeconds != FinishTime::UNSET && GameClient()->m_MapBestTimeSeconds != FinishTime::NOT_FINISHED_MILLIS)
	{
		const int64_t Centis = static_cast<int64_t>(GameClient()->m_MapBestTimeSeconds) * 100 + static_cast<int64_t>(GameClient()->m_MapBestTimeMillis) / 10;
		FormatRaceTime(aServer, sizeof(aServer), Centis);
	}
	if(GameClient()->m_ReceivedDDNetPlayerFinishTimes)
	{
		const auto &Client = GameClient()->m_aClients[GameClient()->m_aLocalIds[g_Config.m_ClDummy]];
		if(Client.m_FinishTimeSeconds != FinishTime::NOT_FINISHED_MILLIS)
		{
			const int64_t Centis = static_cast<int64_t>(Client.m_FinishTimeSeconds) * 100 + static_cast<int64_t>(Client.m_FinishTimeMillis) / 10;
			FormatRaceTime(aPersonal, sizeof(aPersonal), Centis);
		}
	}
	else if(m_aPlayerRecord[g_Config.m_ClDummy] > 0.0f)
		FormatRaceTime(aPersonal, sizeof(aPersonal), (int64_t)std::round(m_aPlayerRecord[g_Config.m_ClDummy] * 100.0f));
	if(!aServer[0] && !aPersonal[0])
		return;

	const int Rows = (aServer[0] ? 1 : 0) + (aPersonal[0] ? 1 : 0);
	const float X = 5.0f, Y = 72.0f, W = 66.0f, RowH = 15.0f;
	const float H = Rows * RowH + 4.0f;
	Graphics()->TextureClear();
	const ColorRGBA Top(0.10f, 0.11f, 0.14f, 0.50f), Bottom(0.06f, 0.07f, 0.09f, 0.42f);
	Graphics()->DrawRect4(X, Y, W, H, Top, Top, Bottom, Bottom, IGraphics::CORNER_ALL, 5.0f);
	Graphics()->DrawRect(X + 5.0f, Y + 0.4f, W - 10.0f, 0.5f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.12f), IGraphics::CORNER_NONE, 0.0f);

	float RowY = Y + 2.0f;
	const auto Row = [&](const char *pLabel, const char *pTime, ColorRGBA Color) {
		CUIRect Label = {X + 6.0f, RowY + 1.5f, W - 12.0f, 5.5f};
		CUIRect Time = {X + 6.0f, RowY + 6.5f, W - 12.0f, 8.0f};
		FogLabel(Ui(), TextRender(), Label, pLabel, 4.8f, TEXTALIGN_ML, MIST.WithAlpha(0.85f), "Manrope", 700);
		FogLabel(Ui(), TextRender(), Time, pTime, 7.5f, TEXTALIGN_ML, Color, "Manrope", 800);
		RowY += RowH;
	};
	if(aServer[0])
		Row(Localize("Server best"), aServer, FROST);
	if(aPersonal[0])
		Row(Localize("Your best"), aPersonal, ColorRGBA(0.80f, 0.86f, 0.94f, 1.0f));
}

// the two places that matter (the leader and you) as a quiet glass card, bottom right
void CHud::FogRenderScoreHud(const CNetObj_PlayerInfo *const *ppPlayerInfo, const int *pPos, const char (*paScore)[16], int Local)
{
	const float W = 112.0f, RowH = 16.0f, Gap = 3.0f;
	const float X = m_Width - W - 5.0f;
	float Y = 250.0f;
	for(int t = 0; t < 2; t++)
	{
		const CNetObj_PlayerInfo *pInfo = ppPlayerInfo[t];
		if(!pInfo || pInfo->m_ClientId < 0 || pInfo->m_ClientId >= MAX_CLIENTS)
		{
			Y += RowH + Gap;
			continue;
		}
		const bool Me = t == Local;
		Graphics()->TextureClear();
		const ColorRGBA Top = Me ? ColorRGBA(0.20f, 0.23f, 0.28f, 0.62f) : ColorRGBA(0.10f, 0.11f, 0.14f, 0.55f);
		const ColorRGBA Bottom = Me ? ColorRGBA(0.14f, 0.16f, 0.20f, 0.56f) : ColorRGBA(0.06f, 0.07f, 0.09f, 0.48f);
		Graphics()->DrawRect4(X, Y, W, RowH, Top, Top, Bottom, Bottom, IGraphics::CORNER_ALL, RowH / 2.0f);
		Graphics()->DrawRect(X + RowH / 2.0f, Y + 0.4f, W - RowH, 0.5f, ColorRGBA(1.0f, 1.0f, 1.0f, Me ? 0.18f : 0.10f), IGraphics::CORNER_NONE, 0.0f);

		// rank in a small disc
		char aRank[8];
		str_format(aRank, sizeof(aRank), "%d", pPos[t]);
		const float D = RowH - 5.0f;
		Graphics()->DrawRect(X + 2.5f, Y + 2.5f, D, D, pPos[t] == 1 ? ColorRGBA(0.88f, 0.82f, 0.62f, 0.85f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.14f), IGraphics::CORNER_ALL, D / 2.0f);
		CUIRect Rank = {X + 2.5f, Y + 2.5f, D, D};
		FogLabel(Ui(), TextRender(), Rank, aRank, 6.0f, TEXTALIGN_MC, pPos[t] == 1 ? ColorRGBA(0.12f, 0.12f, 0.14f, 1.0f) : FROST, "Manrope", 800);

		CTeeRenderInfo TeeInfo = GameClient()->m_aClients[pInfo->m_ClientId].m_RenderInfo;
		TeeInfo.m_Size = 12.0f;
		RenderTools()->RenderIdleTeeAtCenter(&TeeInfo, vec2(X + D + 12.0f, Y + RowH / 2.0f));

		const float ScoreW = paScore[t][0] ? DisplayWidth(TextRender(), paScore[t], 7.0f, 700) + 2.0f : 0.0f;
		CUIRect Name = {X + D + 21.0f, Y, W - D - 21.0f - ScoreW - 8.0f, RowH};
		const ColorRGBA OldOutline = TextRender()->GetTextOutlineColor();
		TextRender()->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.0f);
		TextRender()->TextColor(Me ? FROST : ColorRGBA(0.84f, 0.87f, 0.91f, 1.0f));
		SLabelProperties Props;
		Props.m_MaxWidth = Name.w;
		Props.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Name, GameClient()->m_aClients[pInfo->m_ClientId].m_aName, 6.5f, TEXTALIGN_ML, Props);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		TextRender()->TextOutlineColor(OldOutline);
		if(paScore[t][0])
		{
			CUIRect Score = {X + W - ScoreW - 7.0f, Y, ScoreW, RowH};
			FogLabel(Ui(), TextRender(), Score, paScore[t], 7.0f, TEXTALIGN_MR, MIST, "Manrope", 700);
		}
		Y += RowH + Gap;
	}
}

// The player panel: one glass card with the weapons (a light disc glides to the one in hand),
// jumps as pips and the active rules of the map as small grouped icon chips.
void CHud::FogRenderPlayerState(int ClientId)
{
	CCharacterCore *pCharacter = &GameClient()->m_aClients[ClientId].m_Predicted;
	CNetObj_Character *pPlayer = &GameClient()->m_aClients[ClientId].m_RenderCur;
	const auto &SnapChar = GameClient()->m_Snap.m_aCharacters[ClientId];
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);

	// jumps, counted exactly like the stock HUD
	int TotalJumps = 0, AvailableJumps = 0;
	if(g_Config.m_ClShowhudJumpsIndicator)
	{
		if(SnapChar.m_HasExtendedDisplayInfo)
		{
			const bool Grounded = Collision()->IsOnGround(vec2(pPlayer->m_X, pPlayer->m_Y), CCharacterCore::PhysicalSize());
			int UsedJumps = pCharacter->m_JumpedTotal;
			if(pCharacter->m_Jumps > 1)
				UsedJumps += !Grounded;
			else if(pCharacter->m_Jumps == 1)
				UsedJumps = pPlayer->m_Jumped & 2;
			else if(pCharacter->m_Jumps == -1)
				UsedJumps = !Grounded;
			if(pCharacter->m_EndlessJump && UsedJumps >= absolute(pCharacter->m_Jumps))
				UsedJumps = absolute(pCharacter->m_Jumps) - 1;
			int UnusedJumps = absolute(pCharacter->m_Jumps) - UsedJumps;
			if(!(pPlayer->m_Jumped & 2) && UnusedJumps <= 0)
				UnusedJumps = 1;
			TotalJumps = std::clamp(absolute(pCharacter->m_Jumps), 0, 10);
			AvailableJumps = std::clamp(UnusedJumps, 0, TotalJumps);
		}
		else
			TotalJumps = AvailableJumps = std::clamp(absolute(SnapChar.m_ExtendedData.m_Jumps), 0, 10);
	}

	// map rules as icon groups: what you can do, what is taken away, what state you are in
	struct SIcon
	{
		IGraphics::CTextureHandle m_Texture;
		int m_Offset;
	};
	std::vector<SIcon> vAbilities, vProhibited, vStates;
	const auto &Skin = GameClient()->m_HudSkin;
	if(pCharacter->m_EndlessJump)
		vAbilities.push_back({Skin.m_SpriteHudEndlessJump, m_EndlessJumpOffset});
	if(pCharacter->m_EndlessHook)
		vAbilities.push_back({Skin.m_SpriteHudEndlessHook, m_EndlessHookOffset});
	if(pCharacter->m_Jetpack)
		vAbilities.push_back({Skin.m_SpriteHudJetpack, m_JetpackOffset});
	if(pCharacter->m_HasTelegunGun && pCharacter->m_aWeapons[WEAPON_GUN].m_Got)
		vAbilities.push_back({Skin.m_SpriteHudTeleportGun, m_TeleportGunOffset});
	if(pCharacter->m_HasTelegunGrenade && pCharacter->m_aWeapons[WEAPON_GRENADE].m_Got)
		vAbilities.push_back({Skin.m_SpriteHudTeleportGrenade, m_TeleportGrenadeOffset});
	if(pCharacter->m_HasTelegunLaser && pCharacter->m_aWeapons[WEAPON_LASER].m_Got)
		vAbilities.push_back({Skin.m_SpriteHudTeleportLaser, m_TeleportLaserOffset});
	if(pCharacter->m_Solo)
		vProhibited.push_back({Skin.m_SpriteHudSolo, m_SoloOffset});
	if(pCharacter->m_CollisionDisabled)
		vProhibited.push_back({Skin.m_SpriteHudCollisionDisabled, m_CollisionDisabledOffset});
	if(pCharacter->m_HookHitDisabled)
		vProhibited.push_back({Skin.m_SpriteHudHookHitDisabled, m_HookHitDisabledOffset});
	if(pCharacter->m_HammerHitDisabled)
		vProhibited.push_back({Skin.m_SpriteHudHammerHitDisabled, m_HammerHitDisabledOffset});
	if(pCharacter->m_GrenadeHitDisabled && pCharacter->m_HasTelegunGun && pCharacter->m_aWeapons[WEAPON_GUN].m_Got)
		vProhibited.push_back({Skin.m_SpriteHudGunHitDisabled, m_LaserHitDisabledOffset});
	if(pCharacter->m_ShotgunHitDisabled && pCharacter->m_aWeapons[WEAPON_SHOTGUN].m_Got)
		vProhibited.push_back({Skin.m_SpriteHudShotgunHitDisabled, m_ShotgunHitDisabledOffset});
	if(pCharacter->m_GrenadeHitDisabled && pCharacter->m_aWeapons[WEAPON_GRENADE].m_Got)
		vProhibited.push_back({Skin.m_SpriteHudGrenadeHitDisabled, m_GrenadeHitDisabledOffset});
	if(pCharacter->m_LaserHitDisabled && pCharacter->m_aWeapons[WEAPON_LASER].m_Got)
		vProhibited.push_back({Skin.m_SpriteHudLaserHitDisabled, m_LaserHitDisabledOffset});
	const int Flags = SnapChar.m_HasExtendedDisplayInfo ? SnapChar.m_ExtendedData.m_Flags : 0;
	if(Flags & CHARACTERFLAG_LOCK_MODE)
		vStates.push_back({Skin.m_SpriteHudLockMode, m_LockModeOffset});
	if(Flags & CHARACTERFLAG_PRACTICE_MODE)
		vStates.push_back({Skin.m_SpriteHudPracticeMode, m_PracticeModeOffset});
	if(Flags & CHARACTERFLAG_TEAM0_MODE)
		vStates.push_back({Skin.m_SpriteHudTeam0Mode, m_Team0ModeOffset});
	if(pCharacter->m_DeepFrozen)
		vStates.push_back({Skin.m_SpriteHudDeepFrozen, m_DeepFrozenOffset});
	if(pCharacter->m_LiveFrozen)
		vStates.push_back({Skin.m_SpriteHudLiveFrozen, m_LiveFrozenOffset});

	std::vector<int> vWeapons;
	for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
		if(pCharacter->m_aWeapons[Weapon].m_Got)
			vWeapons.push_back(Weapon);
	float NinjaProgress = 0.0f;
	if(pCharacter->m_aWeapons[WEAPON_NINJA].m_Got && SnapChar.m_HasExtendedDisplayInfo)
	{
		const int Max = g_pData->m_Weapons.m_Ninja.m_Duration * Client()->GameTickSpeed() / 1000;
		NinjaProgress = std::clamp(pCharacter->m_Ninja.m_ActivationTick + Max - Client()->GameTick(g_Config.m_ClDummy), 0, Max) / (float)Max;
	}

	// layout
	const float Pad = 4.0f, Slot = 19.0f, IconRowH = 14.0f, ChipGap = 4.0f;
	const float X = 5.0f;
	const float Y = 5.0f + (GameClient()->m_GameInfo.m_HudHealthArmor && g_Config.m_ClShowhudHealthAmmo ? 24.0f : 0.0f) + (GameClient()->m_GameInfo.m_HudAmmo && g_Config.m_ClShowhudHealthAmmo ? 12.0f : 0.0f);
	const float WeaponsW = vWeapons.size() * Slot + (NinjaProgress > 0.0f ? 9.0f : 0.0f);
	const float JumpsW = TotalJumps * 8.0f;
	float ChipsW = 0.0f;
	for(const auto *pGroup : {&vAbilities, &vProhibited, &vStates})
		if(!pGroup->empty())
			ChipsW += pGroup->size() * 12.0f + 4.0f + ChipGap;
	const float ContentW = std::max({WeaponsW, JumpsW, ChipsW - ChipGap, 20.0f});
	float H = Pad;
	const float WeaponsY = Y + H;
	if(!vWeapons.empty())
		H += 16.0f;
	const float JumpsY = Y + H + 1.0f;
	if(TotalJumps > 0)
		H += 7.0f;
	const float ChipsY = Y + H + 1.0f;
	if(ChipsW > 0.0f)
		H += IconRowH + 2.0f;
	H += Pad - 1.0f;
	if(vWeapons.empty() && TotalJumps == 0 && ChipsW <= 0.0f)
		return;
	const float W = ContentW + 2.0f * Pad;

	// card
	Graphics()->TextureClear();
	const ColorRGBA Top(0.10f, 0.11f, 0.14f, 0.52f), Bottom(0.06f, 0.07f, 0.09f, 0.44f);
	Graphics()->DrawRect(X - 1.0f, Y + 1.0f, W + 2.0f, H + 1.0f, ColorRGBA(0.0f, 0.01f, 0.03f, 0.16f), IGraphics::CORNER_ALL, 7.0f);
	Graphics()->DrawRect4(X, Y, W, H, Top, Top, Bottom, Bottom, IGraphics::CORNER_ALL, 6.0f);
	Graphics()->DrawRect(X + 6.0f, Y + 0.4f, W - 12.0f, 0.5f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.12f), IGraphics::CORNER_NONE, 0.0f);

	// weapons: a frosted disc springs to the one in hand
	if(!vWeapons.empty())
	{
		int ActiveSlot = -1;
		for(size_t i = 0; i < vWeapons.size(); i++)
			if(vWeapons[i] == pPlayer->m_Weapon)
				ActiveSlot = i;
		if(ActiveSlot >= 0)
		{
			const float Target = X + Pad + ActiveSlot * Slot;
			if(m_FogWeaponX < 0.0f)
				m_FogWeaponX = Target;
			FogSpring(m_FogWeaponX, m_FogWeaponV, Target, Dt, 6.0f, 0.5f);
			const ColorRGBA DiscTop(1.0f, 1.0f, 1.0f, 0.18f), DiscBottom(1.0f, 1.0f, 1.0f, 0.09f);
			Graphics()->TextureClear();
			Graphics()->DrawRect4(m_FogWeaponX + 1.0f, WeaponsY, Slot - 2.0f, 16.0f, DiscTop, DiscTop, DiscBottom, DiscBottom, IGraphics::CORNER_ALL, 5.0f);
		}
		for(size_t i = 0; i < vWeapons.size(); i++)
		{
			const int Weapon = vWeapons[i];
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, Weapon == pPlayer->m_Weapon ? 1.0f : 0.38f);
			Graphics()->QuadsSetRotation(pi * 7 / 4);
			Graphics()->TextureSet(GameClient()->m_GameSkin.m_aSpritePickupWeapons[Weapon]);
			// scaled down so every weapon stays inside its own slot (the hammer is the widest)
			const float WeaponScale = Weapon == WEAPON_HAMMER ? 0.62f : 0.78f;
			Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_aWeaponOffset[Weapon], X + Pad + i * Slot + Slot / 2.0f, WeaponsY + 8.0f, WeaponScale, WeaponScale);
			Graphics()->QuadsSetRotation(0);
		}
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
		if(NinjaProgress > 0.0f)
			RenderNinjaBarPos(X + Pad + vWeapons.size() * Slot + 3.0f, WeaponsY + 1.0f, 4.0f, 14.0f, NinjaProgress);
	}

	// jumps: pips, filled while still available
	for(int i = 0; i < TotalJumps; i++)
	{
		const bool Available = i < AvailableJumps;
		Graphics()->TextureClear();
		Graphics()->DrawRect(X + Pad + i * 8.0f + 0.5f, JumpsY, 6.5f, 4.0f, Available ? ColorRGBA(0.86f, 0.91f, 0.96f, 0.95f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.13f), IGraphics::CORNER_ALL, 2.0f);
	}

	// rule chips
	float ChipX = X + Pad;
	const auto Group = [&](const std::vector<SIcon> &vIcons, ColorRGBA Tint) {
		if(vIcons.empty())
			return;
		const float ChipW = vIcons.size() * 12.0f + 4.0f;
		Graphics()->TextureClear();
		Graphics()->DrawRect(ChipX, ChipsY, ChipW, IconRowH, Tint, IGraphics::CORNER_ALL, 4.5f);
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
		for(size_t i = 0; i < vIcons.size(); i++)
		{
			Graphics()->TextureSet(vIcons[i].m_Texture);
			Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, vIcons[i].m_Offset, ChipX + 2.0f + i * 12.0f, ChipsY + 1.0f);
		}
		ChipX += ChipW + ChipGap;
	};
	Group(vAbilities, ColorRGBA(1.0f, 1.0f, 1.0f, 0.07f));
	Group(vProhibited, ColorRGBA(0.92f, 0.58f, 0.62f, 0.20f));
	Group(vStates, ColorRGBA(0.62f, 0.80f, 0.94f, 0.20f));
}

void CMotd::FogRender()
{
	const float ScreenH = 300.0f, ScreenW = ScreenH * Graphics()->ScreenAspect();
	Graphics()->MapScreenToSize(ScreenW, ScreenH);

	// life: settles in from above with a spring, fades out over the last moments
	const int64_t Now = time();
	const int64_t Duration = std::max<int64_t>(time_freq() * g_Config.m_ClMotdTime, 1);
	const float Since = (Now - (m_ServerMotdTime - Duration)) / (float)time_freq();
	const float Left = (m_ServerMotdTime - Now) / (float)time_freq();
	const float Speed = g_Config.m_FogAnim ? std::clamp(g_Config.m_FogAnimSpeed / 100.0f, 0.1f, 10.0f) : 1000.0f;
	const float In = FogEaseOutBack(std::min(Since * 3.0f * Speed, 1.0f));
	const float Alpha = std::clamp(Since * 6.0f * Speed, 0.0f, 1.0f) * std::clamp(Left / 0.4f, 0.0f, 1.0f);
	if(Alpha <= 0.0f)
		return;

	const float W = 230.0f, Pad = 9.0f, FontSize = 6.5f, HeaderH = 14.0f;
	const float TextW = W - 2.0f * Pad;
	const STextBoundingBox Box = TextRender()->TextBoundingBox(FontSize, ServerMotd(), -1, TextW);
	const float TextH = std::min(Box.m_H, FontSize * 1.25f * 18.0f);
	const float H = HeaderH + TextH + Pad + 4.0f;
	const float X = ScreenW / 2.0f - W / 2.0f;
	const float Y = 26.0f - (1.0f - In) * 10.0f;
	m_TouchRect = CUIRect{X / ScreenW, Y / ScreenH, W / ScreenW, H / ScreenH};

	// smoked glass card
	Graphics()->TextureClear();
	Graphics()->DrawRect(X - 1.5f, Y + 1.5f, W + 3.0f, H + 1.5f, ColorRGBA(0.0f, 0.01f, 0.03f, 0.20f * Alpha), IGraphics::CORNER_ALL, 9.0f);
	const ColorRGBA Top(0.11f, 0.12f, 0.15f, 0.74f * Alpha), Bottom(0.07f, 0.08f, 0.10f, 0.68f * Alpha);
	Graphics()->DrawRect4(X, Y, W, H, Top, Top, Bottom, Bottom, IGraphics::CORNER_ALL, 8.0f);
	Graphics()->DrawRect(X + 8.0f, Y + 0.4f, W - 16.0f, 0.5f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.14f * Alpha), IGraphics::CORNER_NONE, 0.0f);

	// header: mark, title, how to close
	FogBrand::DrawMark(Graphics(), vec2(X + Pad + 4.0f, Y + HeaderH / 2.0f + 1.0f), 10.0f, MIST.WithAlpha(Alpha));
	CUIRect Title = {X + Pad + 11.0f, Y + 2.0f, W - 2.0f * Pad - 64.0f, HeaderH - 2.0f};
	FogLabel(Ui(), TextRender(), Title, Localize("Message from the server"), 6.0f, TEXTALIGN_ML, MIST.WithAlpha(Alpha), "Manrope", 700);
	CUIRect Hint = {X + W - Pad - 50.0f, Y + 2.0f, 50.0f, HeaderH - 2.0f};
	FogLabel(Ui(), TextRender(), Hint, Localize("Esc to close"), 5.0f, TEXTALIGN_MR, ColorRGBA(0.50f, 0.54f, 0.60f, Alpha), "Manrope", 600);
	Graphics()->TextureClear();
	Graphics()->DrawRect(X + Pad, Y + HeaderH + 0.5f, W - 2.0f * Pad, 0.4f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.07f * Alpha), IGraphics::CORNER_NONE, 0.0f);

	// the message itself in the game face (servers write in any script), no outline
	const ColorRGBA OldOutline = TextRender()->GetTextOutlineColor();
	TextRender()->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.0f);
	TextRender()->TextColor(FROST.WithAlpha(Alpha));
	CTextCursor Cursor;
	Cursor.SetPosition(vec2(X + Pad, Y + HeaderH + 4.0f));
	Cursor.m_FontSize = FontSize;
	Cursor.m_LineWidth = TextW;
	Cursor.m_MaxLines = 18;
	TextRender()->TextEx(&Cursor, ServerMotd());
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->TextOutlineColor(OldOutline);

	// time left as a hairline along the bottom edge
	const float Remaining = std::clamp(Left / (Duration / (float)time_freq()), 0.0f, 1.0f);
	Graphics()->TextureClear();
	Graphics()->DrawRect(X + 8.0f, Y + H - 1.6f, (W - 16.0f) * Remaining, 0.8f, ColorRGBA(0.85f, 0.90f, 0.95f, 0.35f * Alpha), IGraphics::CORNER_ALL, 0.4f);
}

// The vote as a quiet glass card on the left: what is being voted on, why, how it stands,
// and the two keys. It slides in with a spring; the bar settles on the counts.
void CVoting::FogRender()
{
	const int Seconds = SecondsLeft();
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	const float Speed = g_Config.m_FogAnim ? std::clamp(g_Config.m_FogAnimSpeed / 100.0f, 0.1f, 10.0f) : 1000.0f;
	const float Since = (time() - m_Opentime) / (float)time_freq();
	const float Duration = std::max((m_Closetime - m_Opentime) / (float)time_freq(), 0.001f);
	const float Left = (m_Closetime - time()) / (float)time_freq();
	const float In = FogEaseOutBack(std::min(Since * 3.0f * Speed, 1.0f));
	const float Alpha = std::clamp(Since * 6.0f * Speed, 0.0f, 1.0f) * std::clamp(Left / 0.4f, 0.0f, 1.0f);
	if(Alpha <= 0.0f)
		return;

	const float W = 150.0f, Pad = 7.0f;
	const bool HasReason = VoteReason()[0] != '\0';
	const float H = 14.0f + 12.0f + (HasReason ? 9.0f : 0.0f) + 16.0f + 14.0f + Pad;
	const float X = 5.0f - (1.0f - In) * 30.0f, Y = 92.0f;

	Graphics()->TextureClear();
	Graphics()->DrawRect(X - 1.0f, Y + 1.5f, W + 2.0f, H + 1.5f, ColorRGBA(0.0f, 0.01f, 0.03f, 0.18f * Alpha), IGraphics::CORNER_ALL, 8.0f);
	const ColorRGBA Top(0.11f, 0.12f, 0.15f, 0.74f * Alpha), Bottom(0.07f, 0.08f, 0.10f, 0.68f * Alpha);
	Graphics()->DrawRect4(X, Y, W, H, Top, Top, Bottom, Bottom, IGraphics::CORNER_ALL, 7.0f);
	Graphics()->DrawRect(X + 7.0f, Y + 0.4f, W - 14.0f, 0.5f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.14f * Alpha), IGraphics::CORNER_NONE, 0.0f);

	// header: what this is, and how long is left as a ring
	float y = Y + 3.0f;
	CUIRect Header = {X + Pad, y, W - 2.0f * Pad, 10.0f};
	FogLabel(Ui(), TextRender(), Header, Localize("Vote"), 5.5f, TEXTALIGN_ML, MIST.WithAlpha(Alpha), "Manrope", 700);
	char aTime[32];
	str_format(aTime, sizeof(aTime), Localize("%ds left"), std::max(Seconds, 0));
	CUIRect Time = {X + W - Pad - 70.0f, y, 70.0f - 10.0f, 10.0f};
	FogLabel(Ui(), TextRender(), Time, aTime, 5.5f, TEXTALIGN_MR, MIST.WithAlpha(Alpha), "Manrope", 700);
	SProgressSpinnerProperties Ring;
	Ring.m_Progress = std::clamp(Since / Duration, 0.0f, 1.0f);
	Ring.m_Color = ColorRGBA(0.80f, 0.86f, 0.94f, 0.8f * Alpha);
	Ui()->RenderProgressSpinner(vec2(X + W - Pad - 4.0f, y + 5.0f), 3.2f, Ring);
	y += 12.0f;

	// the vote itself, then why
	CUIRect Description = {X + Pad, y, W - 2.0f * Pad, 12.0f};
	FogLabel(Ui(), TextRender(), Description, VoteDescription(), 8.0f, TEXTALIGN_ML, FROST.WithAlpha(Alpha), "Manrope", 700);
	y += 12.0f;
	if(HasReason)
	{
		char aReason[300];
		str_format(aReason, sizeof(aReason), "%s %s", Localize("Reason:"), VoteReason());
		CUIRect Reason = {X + Pad, y, W - 2.0f * Pad, 8.0f};
		const ColorRGBA OldOutline = TextRender()->GetTextOutlineColor();
		TextRender()->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.0f);
		TextRender()->TextColor(MIST.WithAlpha(Alpha));
		SLabelProperties Props;
		Props.m_MaxWidth = Reason.w;
		Props.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Reason, aReason, 5.5f, TEXTALIGN_ML, Props);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		TextRender()->TextOutlineColor(OldOutline);
		y += 9.0f;
	}

	// how it stands: yes grows from the left, no from the right; the fills glide to the counts
	const float YesTarget = m_Total ? m_Yes / (float)m_Total : 0.0f;
	const float NoTarget = m_Total ? m_No / (float)m_Total : 0.0f;
	FogSpring(m_FogYesShown, m_FogYesV, YesTarget, Dt, 4.0f, 0.4f);
	FogSpring(m_FogNoShown, m_FogNoV, NoTarget, Dt, 4.0f, 0.4f);
	const float BarX = X + Pad, BarW = W - 2.0f * Pad, BarY = y + 2.0f, BarH = 4.0f;
	Graphics()->TextureClear();
	Graphics()->DrawRect(BarX, BarY, BarW, BarH, ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f * Alpha), IGraphics::CORNER_ALL, BarH / 2.0f);
	if(m_FogYesShown > 0.005f)
		Graphics()->DrawRect(BarX, BarY, std::max(BarW * std::clamp(m_FogYesShown, 0.0f, 1.0f), BarH), BarH, FASTER.WithAlpha(0.85f * Alpha), IGraphics::CORNER_ALL, BarH / 2.0f);
	if(m_FogNoShown > 0.005f)
	{
		const float NoW = std::max(BarW * std::clamp(m_FogNoShown, 0.0f, 1.0f), BarH);
		Graphics()->DrawRect(BarX + BarW - NoW, BarY, NoW, BarH, SLOWER.WithAlpha(0.85f * Alpha), IGraphics::CORNER_ALL, BarH / 2.0f);
	}
	Graphics()->DrawRect(BarX + BarW / 2.0f - 0.25f, BarY - 1.0f, 0.5f, BarH + 2.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * Alpha), IGraphics::CORNER_NONE, 0.0f);
	char aYes[32], aNo[32];
	str_format(aYes, sizeof(aYes), "%d %s", m_Yes, Localize("yes"));
	str_format(aNo, sizeof(aNo), "%d %s", m_No, Localize("no"));
	CUIRect YesCount = {BarX, BarY + BarH + 1.0f, BarW / 2.0f, 7.0f}, NoCount = {BarX + BarW / 2.0f, BarY + BarH + 1.0f, BarW / 2.0f, 7.0f};
	FogLabel(Ui(), TextRender(), YesCount, aYes, 5.5f, TEXTALIGN_ML, FASTER.WithAlpha(Alpha), "Manrope", 700);
	FogLabel(Ui(), TextRender(), NoCount, aNo, 5.5f, TEXTALIGN_MR, SLOWER.WithAlpha(Alpha), "Manrope", 700);
	y += 16.0f;

	// the two keys; after voting, the choice stays lit and the other fades
	char aKeyYes[64], aKeyNo[64];
	GameClient()->m_Binds.GetKey("vote yes", aKeyYes, sizeof(aKeyYes));
	GameClient()->m_Binds.GetKey("vote no", aKeyNo, sizeof(aKeyNo));
	for(char *p : {aKeyYes, aKeyNo})
		for(; *p; p++)
			if(*p >= 'a' && *p <= 'z')
				*p = *p - 'a' + 'A';
	const auto Key = [&](float x, float w, const char *pKey, const char *pLabel, ColorRGBA Tint, int Choice) {
		const bool Chosen = TakenChoice() == Choice;
		const float Fade = TakenChoice() != 0 && !Chosen ? 0.4f : 1.0f;
		Graphics()->TextureClear();
		Graphics()->DrawRect(x, y, w, 11.0f, Tint.WithAlpha((Chosen ? 0.30f : 0.10f) * Alpha * Fade), IGraphics::CORNER_ALL, 5.5f);
		TextRender()->SetDisplayFont("Manrope", 800);
		TextRender()->SetFontPreset(EFontPreset::DISPLAY_FONT);
		const float CapW = TextRender()->TextWidth(5.5f, pKey) + 6.0f;
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		Graphics()->DrawRect(x + 2.0f, y + 2.0f, CapW, 7.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.12f * Alpha * Fade), IGraphics::CORNER_ALL, 2.0f);
		CUIRect Cap = {x + 2.0f, y + 2.0f, CapW, 7.0f};
		FogLabel(Ui(), TextRender(), Cap, pKey, 5.5f, TEXTALIGN_MC, FROST.WithAlpha(Alpha * Fade), "Manrope", 800);
		CUIRect Label = {x + CapW + 5.0f, y, w - CapW - 7.0f, 11.0f};
		FogLabel(Ui(), TextRender(), Label, pLabel, 6.5f, TEXTALIGN_ML, (Chosen ? FROST : Tint).WithAlpha(Alpha * Fade), "Manrope", 700);
	};
	const float KeyW = (W - 2.0f * Pad - 4.0f) / 2.0f;
	Key(X + Pad, KeyW, aKeyYes, Localize("Vote yes"), FASTER, 1);
	Key(X + Pad + KeyW + 4.0f, KeyW, aKeyNo, Localize("Vote no"), SLOWER, -1);
}

void CVoting::FogPreview()
{
	m_Opentime = time();
	m_Closetime = m_Opentime + time_freq() * 20;
	str_copy(m_aDescription, "Change map to Sunny Side Up");
	str_copy(m_aReason, "everyone finished this one");
	m_Yes = 3;
	m_No = 1;
	m_Pass = 3;
	m_Total = 6;
	m_Voted = 0;
}

void CHud::ConFogClip(IConsole::IResult *pResult, void *pUserData)
{
	CHud *pSelf = static_cast<CHud *>(pUserData);
	if(pSelf->Client()->State() != IClient::STATE_ONLINE)
		return;
	if(!g_Config.m_ClReplays)
	{
		g_Config.m_ClReplays = 1;
		pSelf->FogToast(Localize("Clips are on now: recording starts after you rejoin"));
		return;
	}
	char aTimestamp[20], aCommand[256];
	str_timestamp(aTimestamp, sizeof(aTimestamp));
	str_format(aCommand, sizeof(aCommand), "save_replay %d fog_%s_%s", g_Config.m_FogClipLength, pSelf->GameClient()->Map()->BaseName(), aTimestamp);
	pSelf->Console()->ExecuteLine(aCommand, IConsole::CLIENT_ID_UNSPECIFIED);
	char aToast[128];
	str_format(aToast, sizeof(aToast), Localize("Clip saved: the last %d seconds"), g_Config.m_FogClipLength);
	pSelf->FogToast(aToast);
}

void CHud::FogToast(const char *pText)
{
	str_copy(m_aFogToast, pText);
	m_FogToastTime = Client()->LocalTime();
}

// a pill under the timer that springs in, stays three seconds and fades
void CHud::FogRenderToast()
{
	const float Since = Client()->LocalTime() - m_FogToastTime;
	if(!m_aFogToast[0] || Since < 0.0f || Since > 3.5f)
		return;
	const float In = FogEaseOutBack(std::min(Since * 4.0f, 1.0f));
	const float Alpha = std::clamp(Since * 8.0f, 0.0f, 1.0f) * std::clamp((3.5f - Since) / 0.5f, 0.0f, 1.0f);
	const float W = DisplayWidth(TextRender(), m_aFogToast, 7.0f, 700) + 30.0f, H = 15.0f;
	const float X = m_Width / 2.0f - W / 2.0f, Y = 24.0f + (1.0f - In) * -8.0f;
	Capsule(Graphics(), X, Y, W, H, Alpha, ColorRGBA(0.62f, 0.72f, 0.80f, 1.0f));
	Graphics()->TextureClear();
	Graphics()->DrawRect(X + 6.0f, Y + H / 2.0f - 2.5f, 5.0f, 5.0f, ColorRGBA(0.92f, 0.58f, 0.62f, Alpha), IGraphics::CORNER_ALL, 2.5f);
	CUIRect Label = {X + 16.0f, Y, W - 20.0f, H};
	FogLabel(Ui(), TextRender(), Label, m_aFogToast, 7.0f, TEXTALIGN_ML, FROST.WithAlpha(Alpha), "Manrope", 700);
}
