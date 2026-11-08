#include "director.h"

#include <base/io.h>
#include "glass.h"

#include <engine/graphics.h>
#include <engine/textrender.h>
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
	m_vCaptions.clear();
	m_vCropLog.clear();
	m_CropX = -1.0f;
	m_CropV = 0.0f;
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
	// One continuous curve: Play, Demos, Editor, Run server, Settings. It begins very slowly,
	// speeds up as it goes and comes to rest on Settings. News and Quit are never touched.
	m_vPath.clear();
	m_vPath.push_back(Start);
	for(int Index : {NAV_PLAY, NAV_DEMOS, NAV_EDITOR, NAV_SERVER, NAV_SETTINGS})
		m_vPath.push_back(Nav(0.0f, Index, false));
	m_PathEnd = 4.2f;
	m_vKeys.push_back(Nav(m_PathEnd, NAV_SETTINGS, false));
	float T = m_PathEnd + 0.4f;
	m_vKeys.push_back(Nav(T, NAV_SETTINGS, true)); // opens Settings
	Caption(0.35, 2.3, "Meet Fog", "A quiet client for DDNet");
	Caption(T + 0.1f, T + 1.05f, "Built to be yours", "Every effect has a switch");
	T += 1.1f;
	m_vKeys.push_back(Nav(T, NAV_SETTINGS_FOG, false));
	T += 0.4f;
	m_vKeys.push_back(Nav(T, NAV_SETTINGS_FOG, true)); // opens the Fog page
	T += 0.9f;
	const bool aHeadline[9] = {true, true, false, false, true, true, false, false, true};
	float aTabStart[9], aTabEnd[9];
	for(int Tab = 0; Tab < 9; Tab++)
	{
		m_vKeys.push_back(Nav(T, NAV_TAB0 + Tab, false));
		T += 0.25f;
		aTabStart[Tab] = T;
		m_vKeys.push_back(Nav(T, NAV_TAB0 + Tab, true));
		T += aHeadline[Tab] ? 1.6f : 0.45f;
		aTabEnd[Tab] = T;
	}
	Caption(aTabStart[0] + 0.1f, aTabEnd[0] - 0.05f, "Lyrics in the map", "Synced lines from your music, drawn in the world");
	Caption(aTabStart[1] + 0.1f, aTabEnd[1] - 0.05f, "26 shader presets", "Bloom, grain and glass in one click");
	Caption(aTabStart[2], aTabEnd[3], "Light, weather, glass", "All of it adjustable");
	Caption(aTabStart[4] + 0.1f, aTabEnd[4] - 0.05f, "Sandevistan trails", "Afterimages that follow your speed");
	Caption(aTabStart[5] + 0.1f, aTabEnd[5] - 0.05f, "Seen by other Fog players", "Name styles and badges, synced live");
	Caption(aTabStart[6], aTabEnd[7], "Interface and gameplay", "Tune every part");
	Caption(aTabStart[8] + 0.1f, aTabEnd[8] - 0.05f, "Little details", "Confetti, thoughts, frost and footprints");
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
	io_write(File, "t,x,y,cropx\n", 12);
	char aLine[96];
	for(size_t i = 0; i < m_vLog.size(); i++)
	{
		const vec3 &Row = m_vLog[i];
		str_format(aLine, sizeof(aLine), "%.4f,%.5f,%.5f,%.2f\n", Row.x, Row.y, Row.z, i < m_vCropLog.size() ? m_vCropLog[i] : 0.0f);
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
	for(size_t i = 0; i + 1 < m_vKeys.size() && m_Time >= m_PathEnd; i++)
	{
		const SKey &A = m_vKeys[i], &B = m_vKeys[i + 1];
		const vec2 PosA = Resolve(A, Prev);
		const vec2 PosB = Resolve(B, PosA);
		Prev = PosB;
		if(m_Time >= A.m_Time && m_Time < B.m_Time)
		{
			// a slight bow instead of a ruler-straight line, so the cursor moves like a hand
			const float U = SmootherStep((m_Time - A.m_Time) / (B.m_Time - A.m_Time));
			const vec2 Delta = PosB - PosA;
			const vec2 Side = length(Delta) > 0.001f ? vec2(-Delta.y, Delta.x) * 0.06f : vec2(0, 0);
			Target = mix(PosA, PosB, U) + Side * std::sin(U * pi);
			break;
		}
		if(m_Time >= B.m_Time)
			Target = PosB;
	}
	if(m_Time < m_PathEnd)
		Target = PathPosition(m_Time, m_Pos);
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
	// the 9:16 crop window follows the cursor with a critically damped spring (same as the post step used to)
	{
		const float TargetX = std::clamp(m_Pos.x / pScreen->w * 3840.0f - 607.5f, 0.0f, 3840.0f - 1215.0f);
		if(m_CropX < 0.0f)
			m_CropX = TargetX;
		const float Dt = Client()->RenderFrameTime(), Omega = 6.0f;
		m_CropV += (Omega * Omega * (TargetX - m_CropX) - 2.0f * Omega * m_CropV) * Dt;
		m_CropX = std::clamp(m_CropX + m_CropV * Dt, 0.0f, 3840.0f - 1215.0f);
		m_vCropLog.push_back(m_CropX);
	}
	m_vLog.emplace_back(m_Time, m_Pos.x / pScreen->w, m_Pos.y / pScreen->h);
	RenderCaptions();

	// end a moment after the last key
	if(m_Time > m_vKeys.back().m_Time + 0.6f)
		Stop();
}

void CFogDirector::Caption(float Start, float End, const char *pTitle, const char *pSub)
{
	m_vCaptions.push_back({Start, End, pTitle, pSub});
}

void CFogDirector::RenderCaptions()
{
	if(!g_Config.m_FogDirectorCaptions)
		return;
	const SCaption *pCaption = nullptr;
	for(const SCaption &Candidate : m_vCaptions)
		if(m_Time >= Candidate.m_Start && m_Time < Candidate.m_End)
			pCaption = &Candidate;
	if(!pCaption)
		return;

	// fade and rise in, fade out
	const float In = SmootherStep((m_Time - pCaption->m_Start) / 0.35f);
	const float Out = SmootherStep((pCaption->m_End - m_Time) / 0.3f);
	const float Alpha = std::min(In, Out);
	if(Alpha <= 0.001f)
		return;

	Ui()->MapScreen();
	const CUIRect Screen = *Ui()->Screen();
	const bool Portrait = g_Config.m_FogDirectorFormat == 1;
	const float CropW = 1215.0f / 3840.0f * Screen.w; // the 9:16 window, in UI units
	const float MaxW = Portrait ? CropW * 0.9f : Screen.w * 0.5f;
	float CenterX = Screen.w * 0.5f;
	if(Portrait && m_CropX >= 0.0f)
		CenterX = (m_CropX + 607.5f) / 3840.0f * Screen.w;

	const float TitleBase = Portrait ? 26.0f : 30.0f, SubBase = Portrait ? 12.5f : 14.5f;
	const float TitleLen = (float)str_length(pCaption->m_Title.c_str()), SubLen = (float)str_length(pCaption->m_Sub.c_str());
	const float TitleSize = std::min(TitleBase, MaxW / (TitleLen * 0.56f));
	const float SubSize = std::min(SubBase, MaxW / (SubLen * 0.52f));
	const float TitleW = TitleLen * TitleSize * 0.56f, SubW = SubLen * SubSize * 0.52f;
	const float Pad = 18.0f;
	const float W = std::max(TitleW, SubW) + Pad * 2.0f, H = TitleSize * 1.25f + SubSize * 1.5f + Pad * 1.5f;
	const float Rise = (1.0f - In) * 12.0f;
	CUIRect Box = {CenterX - W / 2.0f, Screen.h * 0.86f - H + Rise, W, H};
	Box.x = std::clamp(Box.x, 4.0f, Screen.w - W - 4.0f);

	// a dark backing keeps the words readable over busy settings pages
	Graphics()->TextureClear();
	Graphics()->DrawRect(Box.x, Box.y, Box.w, Box.h, ColorRGBA(0.015f, 0.025f, 0.045f, 0.66f * Alpha), IGraphics::CORNER_ALL, 16.0f);
	FogDrawGlass(Graphics(), Box, 16.0f, Alpha);
	CUIRect Inner;
	Box.Margin(vec2(Pad, Pad * 0.75f), &Inner);
	CUIRect Title, Sub;
	Inner.HSplitTop(TitleSize * 1.25f, &Title, &Sub);
	FogLabel(Ui(), TextRender(), Title, pCaption->m_Title.c_str(), TitleSize, TEXTALIGN_MC, ColorRGBA(0.95f, 0.96f, 0.98f, Alpha), "Manrope", 700);
	FogLabel(Ui(), TextRender(), Sub, pCaption->m_Sub.c_str(), SubSize, TEXTALIGN_MC, ColorRGBA(0.72f, 0.76f, 0.82f, Alpha), "Manrope", 500);
}

vec2 CFogDirector::PathPosition(float Time, vec2 Fallback) const
{
	const int Count = (int)m_vPath.size();
	std::vector<vec2> vPoints;
	vec2 Last = Fallback;
	for(const SKey &Key : m_vPath)
	{
		Last = Resolve(Key, Last);
		vPoints.push_back(Last);
	}

	// Catmull-Rom through the rows, parameter S in [0, Count - 1]
	const auto Spline = [&](float S) {
		const int Segment = std::clamp((int)S, 0, Count - 2);
		const float F = S - Segment, F2 = F * F, F3 = F2 * F;
		const vec2 P0 = vPoints[std::max(Segment - 1, 0)], P1 = vPoints[Segment], P2 = vPoints[Segment + 1], P3 = vPoints[std::min(Segment + 2, Count - 1)];
		const vec2 A = P1 * 2.0f;
		const vec2 B = (P2 - P0) * F;
		const vec2 C = (P0 * 2.0f - P1 * 5.0f + P2 * 4.0f - P3) * F2;
		const vec2 D = (P1 * 3.0f - P0 - P2 * 3.0f + P3) * F3;
		return (A + B + C + D) * 0.5f;
	};

	// Arc-length table, so speed depends on distance travelled and not on how the rows are spaced
	constexpr int SAMPLES_PER_SEGMENT = 48;
	const int Total = (Count - 1) * SAMPLES_PER_SEGMENT;
	std::vector<float> vLength(Total + 1, 0.0f);
	vec2 PrevPoint = Spline(0.0f);
	for(int i = 1; i <= Total; i++)
	{
		const vec2 Point = Spline((float)i / SAMPLES_PER_SEGMENT);
		vLength[i] = vLength[i - 1] + length(Point - PrevPoint);
		PrevPoint = Point;
	}

	// Progress in distance: a long crawl, smooth acceleration, a soft landing (one curve, no stops between rows)
	const float Tau = std::clamp(Time / m_PathEnd, 0.0f, 1.0f);
	const float In = Tau * Tau * Tau * Tau, Out = std::pow(1.0f - Tau, 1.8f);
	const float U = In / (In + Out);
	const float TargetLength = U * vLength[Total];
	int Index = 0;
	while(Index < Total && vLength[Index + 1] < TargetLength)
		Index++;
	const float SegmentLength = std::max(vLength[std::min(Index + 1, Total)] - vLength[Index], 0.0001f);
	const float Fraction = std::clamp((TargetLength - vLength[Index]) / SegmentLength, 0.0f, 1.0f);
	return Spline(((float)Index + Fraction) / SAMPLES_PER_SEGMENT);
}
