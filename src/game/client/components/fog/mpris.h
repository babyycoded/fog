#ifndef GAME_CLIENT_COMPONENTS_FOG_MPRIS_H
#define GAME_CLIENT_COMPONENTS_FOG_MPRIS_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Snapshot of the active MPRIS media player (Spotify by default).
struct SMediaState
{
	bool m_Available = false;
	bool m_Playing = false;
	std::string m_Player; // bus name suffix, e.g. "spotify"
	std::string m_TrackId;
	std::string m_Title;
	std::string m_Artist;
	std::string m_Album;
	std::string m_ArtUrl;
	int64_t m_LengthUs = 0;
	int64_t m_PositionUs = 0;
	int64_t m_SampleTimeNs = 0; // time_get_nanoseconds() when m_PositionUs was read
};

// Talks to the session bus on its own thread so a slow player never stalls a frame.
class CMprisClient
{
public:
	enum class ECommand
	{
		PLAY_PAUSE,
		NEXT,
		PREVIOUS,
	};

	CMprisClient() = default;
	~CMprisClient();
	CMprisClient(const CMprisClient &) = delete;
	CMprisClient &operator=(const CMprisClient &) = delete;

	void Start();
	void Stop();
	bool Supported() const;

	void SetPreferredPlayer(const char *pPlayer);
	void SendCommand(ECommand Command);
	SMediaState State() const;

	// Current playback position, extrapolated between polls while playing.
	static int64_t EstimatePositionUs(const SMediaState &State, int64_t NowNs);

private:
	void Run();

	std::thread m_Thread;
	std::atomic<bool> m_Running = false;

	mutable std::mutex m_Mutex;
	SMediaState m_State;
	std::string m_PreferredPlayer;
	std::vector<ECommand> m_vCommands;
};

#endif
