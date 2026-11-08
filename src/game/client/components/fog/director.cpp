#include "director.h"

#include <base/io.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>
#include <game/client/ui.h>

#include <algorithm>
#include <cmath>

namespace
{
vec2 g_aNavPos[CFogDirector::NUM_NAV];
vec2 g_aNavSize[CFogDirector::NUM_NAV];
bool g_aNavValid[CFogDirector::NUM_NAV];

float SmootherStep(float T)
{
	T = std::clamp(T, 0.0f, 1.0f);
	return T * T * T * (T * (T * 6.0f - 15.0f) + 10.0f);
}
}

void CFogDirector::PublishNavRect(int Index, vec2 Pos, vec2 Size)
{
	if(Index < 0 || Index >= NUM_NAV)
		return;
	g_aNavPos[Index] = Pos;
	g_aNavSize[Index] = Size;
	g_aNavValid[Index] = true;
}

void CFogDirector::OnConsoleInit()
{
	Console()->Register("fog_director", "?s[scene]", CFGFLAG_CLIENT, ConDirector, this, "Fog trailer director (dev): scenes: menu; `stop` ends it");
}

void CFogDirector::ConDirector(IConsole::IResult *pResult, void *pUserData)
{
	CFogDirector *pSelf = static_cast<CFogDirector *>(pUserData);
	const char *pScene = pResult->NumArguments() ? pResult->GetString(0) : "menu";
	if(str_comp(pScene, "stop") == 0)
		pSelf->Stop();
	else
		pSelf->Start(pScene);
}

void CFogDirector::OnReset()
{
	// scenes outlive map changes and connects, so a reset must not cancel them
}

void CFogDirector::Start(const char *pScene)
{
	Stop();
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "fog_director", "scene queued");
	// give the start screen a moment to appear and publish its rail rows
	m_Pending = true;
	m_PendingWait = 0.0f;
	str_copy(m_aPendingScene, pScene);
}

void CFogDirector::Begin()
{
	const char *pScene = m_aPendingScene;
	m_Pending = false;
	m_vKeys.clear();
	m_vLog.clear();
	m_Time = 0.0f;
	m_NextKey = 0;
	m_Buttons = 0;
	m_ClickReleaseAt = -1.0f;
	if(str_comp(pScene, "menu") == 0)
		BuildMenuScene();
	else
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "fog_director", "unknown scene (try: menu)");
		return;
	}
	m_Pos = Ui()->MousePos();
	m_Active = true;
	if(g_Config.m_FogCaptureRecord)
	{
		Storage()->CreateFolder("fog", IStorage::TYPE_SAVE);
		Storage()->CreateFolder("fog/capture", IStorage::TYPE_SAVE);
		g_Config.m_FogCaptureActive = 1; // the client now steps time by 1/fps and saves every frame
	}
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "fog_director", "scene started");
}

vec2 CFogDirector::Resolve(const SKey &Key, vec2 Fallback) const
{
	if(Key.m_Nav < 0)
		return Key.m_Ui;
	if(!g_aNavValid[Key.m_Nav])
		return Fallback;
	return g_aNavPos[Key.m_Nav] + g_aNavSize[Key.m_Nav] * vec2(0.55f, 0.5f);
}

void CFogDirector::BuildMenuScene()
{
	// Menu scene: run down the rail, open Settings, open the Fog page, then tour its tabs.
	// The five headline tabs get a longer stop; the rest flash by. Rows are read live.
	const CUIRect *pScreen = Ui()->Screen();
	const auto Nav = [](float Time, int Index, bool Click) {
		SKey Key;
		Key.m_Time = Time;
		Key.m_Ui = vec2(0, 0);
		Key.m_Nav = Index;
		Key.m_Click = Click;
		return Key;
	};
	SKey Start;
	Start.m_Time = 0.0f;
	Start.m_Ui = vec2(pScreen->w * 0.62f, pScreen->h * 0.55f);
	m_vKeys.push_back(Start);
	m_vKeys.push_back(Nav(1.0f, NAV_PLAY, false));
	float T = 1.0f;
	for(int Index : {NAV_DEMOS, NAV_EDITOR, NAV_SERVER, NAV_NEWS, NAV_QUIT})
	{
		T += 0.28f;
		m_vKeys.push_back(Nav(T, Index, false));
	}
	T += 0.5f;
	m_vKeys.push_back(Nav(T, NAV_SETTINGS, false));
	T += 0.45f;
	m_vKeys.push_back(Nav(T, NAV_SETTINGS, true)); // opens Settings
	T += 1.1f;
	m_vKeys.push_back(Nav(T, NAV_SETTINGS_FOG, false));
	T += 0.4f;
	m_vKeys.push_back(Nav(T, NAV_SETTINGS_FOG, true)); // opens the Fog page
	T += 0.9f;
	const bool aHeadline[9] = {true, true, false, false, true, true, false, false, true};
	for(int Tab = 0; Tab < 9; Tab++)
	{
		m_vKeys.push_back(Nav(T, NAV_TAB0 + Tab, false));
		T += 0.25f;
		m_vKeys.push_back(Nav(T, NAV_TAB0 + Tab, true));
		T += aHeadline[Tab] ? 1.6f : 0.45f;
	}
	m_vKeys.push_back(Nav(T, NAV_TAB0 + 8, false));
}

void CFogDirector::Stop()
{
	m_Pending = false;
	if(!m_Active)
		return;
	m_Active = false;
	g_Config.m_FogCaptureActive = 0;
	Ui()->FogScriptMouse(false, vec2(0, 0), 0);
	WriteLog();
}

void CFogDirector::WriteLog() const
{
	if(m_vLog.empty())
		return;
	IOHANDLE File = Storage()->OpenFile("fog/director_cursor.csv", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return;
	io_write(File, "t,x,y\n", 6);
	char aLine[96];
	for(const vec3 &Row : m_vLog)
	{
		str_format(aLine, sizeof(aLine), "%.4f,%.5f,%.5f\n", Row.x, Row.y, Row.z);
		io_write(File, aLine, str_length(aLine));
	}
	io_close(File);
}

void CFogDirector::OnRender()
{
	if(m_Pending)
	{
		m_PendingWait += Client()->RenderFrameTime();
		// the menu restores the last page at startup, so keep asking for the start screen
		if(!g_aNavValid[NAV_SETTINGS])
			GameClient()->m_Menus.SetShowStart(true);
		if(m_PendingWait > 1.5f && g_aNavValid[NAV_SETTINGS])
			Begin();
		return;
	}
	if(!m_Active)
		return;

	// RenderFrameTime is the fixed step while start_video records, so the timeline is exact.
	m_Time += Client()->RenderFrameTime();

	// find the segment we are in and ease between its two keys (rows are resolved live)
	vec2 Target = Resolve(m_vKeys.front(), m_Pos);
	vec2 Prev = Target;
	for(size_t i = 0; i + 1 < m_vKeys.size(); i++)
	{
		const SKey &A = m_vKeys[i], &B = m_vKeys[i + 1];
		const vec2 PosA = Resolve(A, Prev);
		const vec2 PosB = Resolve(B, PosA);
		Prev = PosB;
		if(m_Time >= A.m_Time && m_Time < B.m_Time)
		{
			Target = mix(PosA, PosB, SmootherStep((m_Time - A.m_Time) / (B.m_Time - A.m_Time)));
			break;
		}
		if(m_Time >= B.m_Time)
			Target = PosB;
	}
	m_Pos = Target;

	// click: press when its key time passes, release a short moment later
	if(m_NextKey < m_vKeys.size() && m_Time >= m_vKeys[m_NextKey].m_Time)
	{
		if(m_vKeys[m_NextKey].m_Click)
		{
			m_Buttons = 1;
			m_ClickReleaseAt = m_Time + 0.09f;
		}
		m_NextKey++;
	}
	if(m_ClickReleaseAt >= 0.0f && m_Time >= m_ClickReleaseAt)
	{
		m_Buttons = 0;
		m_ClickReleaseAt = -1.0f;
	}

	const CUIRect *pScreen = Ui()->Screen();
	const vec2 Window(Graphics()->WindowWidth(), Graphics()->WindowHeight());
	Ui()->FogScriptMouse(true, m_Pos * Window / vec2(pScreen->w, pScreen->h), m_Buttons);
	m_vLog.emplace_back(m_Time, m_Pos.x / pScreen->w, m_Pos.y / pScreen->h);

	// end a moment after the last key
	if(m_Time > m_vKeys.back().m_Time + 0.6f)
		Stop();
}
