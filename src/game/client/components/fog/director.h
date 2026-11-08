#ifndef GAME_CLIENT_COMPONENTS_FOG_DIRECTOR_H
#define GAME_CLIENT_COMPONENTS_FOG_DIRECTOR_H

#include <base/vmath.h>

#include <engine/console.h>

#include <game/client/component.h>

#include <vector>

// Dev-only scripted camera operator for the Fog trailer. It drives the UI cursor along a
// timeline (so hover and click effects are the real ones) and logs the cursor path, so the
// recording can be zoomed on the cursor in post. Run with `fog_director menu`, stop with
// `fog_director stop`. It never touches gameplay input.
class CFogDirector : public CComponent
{
public:
	// Rail rows of the start screen, published by the start screen every frame (UI coordinates).
	enum
	{
		NAV_PLAY,
		NAV_DEMOS,
		NAV_EDITOR,
		NAV_SERVER,
		NAV_SETTINGS,
		NAV_NEWS,
		NAV_QUIT,
		NUM_NAV
	};
	static void PublishNavRect(int Index, vec2 Pos, vec2 Size);

	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnRender() override;
	void OnReset() override;

	bool Active() const { return m_Active; }

private:
	struct SKey
	{
		float m_Time; // seconds
		vec2 m_Ui; // target in UI coordinates
		bool m_Click; // press at this key
	};

	bool m_Active = false;
	bool m_Pending = false; // waiting for the scene to have laid itself out
	float m_PendingWait = 0.0f;
	char m_aPendingScene[32] = "";
	float m_Time = 0.0f;
	std::vector<SKey> m_vKeys;
	vec2 m_Pos = vec2(0, 0); // current cursor, UI coordinates
	unsigned m_Buttons = 0;
	float m_ClickReleaseAt = -1.0f;
	size_t m_NextKey = 0;
	std::vector<vec3> m_vLog; // time, normalized x, normalized y

	void Start(const char *pScene);
	void Begin();
	void Stop();
	void BuildMenuScene();
	void WriteLog() const;

	static void ConDirector(IConsole::IResult *pResult, void *pUserData);
};

#endif
