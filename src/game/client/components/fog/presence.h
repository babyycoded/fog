#ifndef GAME_CLIENT_COMPONENTS_FOG_PRESENCE_H
#define GAME_CLIENT_COMPONENTS_FOG_PRESENCE_H

#include "look.h"

#include <base/color.h>

#include <game/client/component.h>

#include <memory>
#include <string>
#include <unordered_map>

class IHttpRequest;

// How a Fog player wants their name to look (only Fog clients draw it)
struct SFogNameStyle
{
	int m_Mode = 0; // 0 plain, 1 shimmer, 2 gradient, 3 rainbow, 4 pulse, 5 wave
	ColorRGBA m_Color1 = ColorRGBA(0.78f, 0.83f, 0.88f, 1.0f);
	ColorRGBA m_Color2 = ColorRGBA(0.62f, 0.69f, 0.78f, 1.0f);
	float m_Speed = 1.0f;
	bool m_Badge = true;
	bool m_HasLook = false;
	SFogLook m_Look; // their tee studio look
};

// Fog client: a heartbeat to the presence server (fog_presence_url) every 20 seconds while
// online. The answer lists the Fog players on the same DDNet server and their name styles.
class CFogPresence : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnShutdown() override;

	// the style of a Fog player on this server, or nullptr for everyone else
	const SFogNameStyle *Find(const char *pName) const;
	SFogNameStyle OwnStyle() const;
	int NumPlayers() const { return (int)m_Players.size(); }

private:
	void SendHeartbeat();
	void FinishHeartbeat();

	std::shared_ptr<IHttpRequest> m_pRequest;
	std::unordered_map<std::string, SFogNameStyle> m_Players;
	float m_NextHeartbeat = 0.0f;
	char m_aLastName[64] = "";
};

#endif
