#include "mpris.h"

#include <base/str.h>
#include <base/time.h>

#include <chrono>

#if defined(CONF_FOG_DBUS)
#include <dbus/dbus.h>
#endif

using namespace std::chrono_literals;

static int64_t NowNs()
{
	return time_get_nanoseconds().count();
}

CMprisClient::~CMprisClient()
{
	Stop();
}

bool CMprisClient::Supported() const
{
#if defined(CONF_FOG_DBUS)
	return true;
#else
	return false;
#endif
}

void CMprisClient::Start()
{
	if(!Supported() || m_Running)
		return;
	m_Running = true;
	m_Thread = std::thread([this]() { Run(); });
}

void CMprisClient::Stop()
{
	m_Running = false;
	if(m_Thread.joinable())
		m_Thread.join();
}

void CMprisClient::SetPreferredPlayer(const char *pPlayer)
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	m_PreferredPlayer = pPlayer;
}

void CMprisClient::SendCommand(ECommand Command)
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	m_vCommands.push_back(Command);
}

SMediaState CMprisClient::State() const
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	return m_State;
}

int64_t CMprisClient::EstimatePositionUs(const SMediaState &State, int64_t NowNs)
{
	int64_t Position = State.m_PositionUs;
	if(State.m_Playing && State.m_SampleTimeNs > 0)
		Position += (NowNs - State.m_SampleTimeNs) / 1000;
	if(State.m_LengthUs > 0 && Position > State.m_LengthUs)
		Position = State.m_LengthUs;
	return Position < 0 ? 0 : Position;
}

#if defined(CONF_FOG_DBUS)

namespace {

constexpr const char *MPRIS_PREFIX = "org.mpris.MediaPlayer2.";
constexpr const char *MPRIS_PATH = "/org/mpris/MediaPlayer2";
constexpr const char *MPRIS_PLAYER_IFACE = "org.mpris.MediaPlayer2.Player";
constexpr int CALL_TIMEOUT_MS = 400;

struct SDBusMessage
{
	DBusMessage *m_pMsg = nullptr;
	explicit SDBusMessage(DBusMessage *pMsg) :
		m_pMsg(pMsg) {}
	~SDBusMessage()
	{
		if(m_pMsg)
			dbus_message_unref(m_pMsg);
	}
	SDBusMessage(const SDBusMessage &) = delete;
	SDBusMessage &operator=(const SDBusMessage &) = delete;
};

DBusMessage *Call(DBusConnection *pConn, DBusMessage *pMsg)
{
	DBusError Err;
	dbus_error_init(&Err);
	DBusMessage *pReply = dbus_connection_send_with_reply_and_block(pConn, pMsg, CALL_TIMEOUT_MS, &Err);
	if(dbus_error_is_set(&Err))
		dbus_error_free(&Err);
	return pReply;
}

std::vector<std::string> ListPlayers(DBusConnection *pConn)
{
	std::vector<std::string> vPlayers;
	SDBusMessage Msg(dbus_message_new_method_call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames"));
	if(!Msg.m_pMsg)
		return vPlayers;
	SDBusMessage Reply(Call(pConn, Msg.m_pMsg));
	if(!Reply.m_pMsg)
		return vPlayers;

	DBusMessageIter It;
	if(!dbus_message_iter_init(Reply.m_pMsg, &It) || dbus_message_iter_get_arg_type(&It) != DBUS_TYPE_ARRAY)
		return vPlayers;
	DBusMessageIter Arr;
	dbus_message_iter_recurse(&It, &Arr);
	while(dbus_message_iter_get_arg_type(&Arr) == DBUS_TYPE_STRING)
	{
		const char *pName = nullptr;
		dbus_message_iter_get_basic(&Arr, &pName);
		if(pName && str_startswith(pName, MPRIS_PREFIX))
			vPlayers.emplace_back(pName);
		dbus_message_iter_next(&Arr);
	}
	return vPlayers;
}

std::string ReadString(DBusMessageIter *pVariant)
{
	const int Type = dbus_message_iter_get_arg_type(pVariant);
	if(Type == DBUS_TYPE_STRING || Type == DBUS_TYPE_OBJECT_PATH)
	{
		const char *pStr = nullptr;
		dbus_message_iter_get_basic(pVariant, &pStr);
		return pStr ? pStr : "";
	}
	if(Type == DBUS_TYPE_ARRAY)
	{
		// xesam:artist is a list of strings: join them
		std::string Joined;
		DBusMessageIter Arr;
		dbus_message_iter_recurse(pVariant, &Arr);
		while(dbus_message_iter_get_arg_type(&Arr) == DBUS_TYPE_STRING)
		{
			const char *pStr = nullptr;
			dbus_message_iter_get_basic(&Arr, &pStr);
			if(pStr && pStr[0])
			{
				if(!Joined.empty())
					Joined += ", ";
				Joined += pStr;
			}
			dbus_message_iter_next(&Arr);
		}
		return Joined;
	}
	return "";
}

int64_t ReadInt(DBusMessageIter *pVariant)
{
	switch(dbus_message_iter_get_arg_type(pVariant))
	{
	case DBUS_TYPE_INT64:
	{
		dbus_int64_t Value;
		dbus_message_iter_get_basic(pVariant, &Value);
		return Value;
	}
	case DBUS_TYPE_UINT64:
	{
		dbus_uint64_t Value;
		dbus_message_iter_get_basic(pVariant, &Value);
		return (int64_t)Value;
	}
	case DBUS_TYPE_INT32:
	{
		dbus_int32_t Value;
		dbus_message_iter_get_basic(pVariant, &Value);
		return Value;
	}
	case DBUS_TYPE_UINT32:
	{
		dbus_uint32_t Value;
		dbus_message_iter_get_basic(pVariant, &Value);
		return Value;
	}
	case DBUS_TYPE_DOUBLE:
	{
		double Value;
		dbus_message_iter_get_basic(pVariant, &Value);
		return (int64_t)Value;
	}
	default:
		return 0;
	}
}

void ReadMetadata(DBusMessageIter *pDict, SMediaState &State)
{
	DBusMessageIter Arr;
	dbus_message_iter_recurse(pDict, &Arr);
	while(dbus_message_iter_get_arg_type(&Arr) == DBUS_TYPE_DICT_ENTRY)
	{
		DBusMessageIter Entry, Variant;
		dbus_message_iter_recurse(&Arr, &Entry);
		const char *pKey = nullptr;
		dbus_message_iter_get_basic(&Entry, &pKey);
		dbus_message_iter_next(&Entry);
		dbus_message_iter_recurse(&Entry, &Variant);
		if(pKey)
		{
			if(str_comp(pKey, "xesam:title") == 0)
				State.m_Title = ReadString(&Variant);
			else if(str_comp(pKey, "xesam:artist") == 0)
				State.m_Artist = ReadString(&Variant);
			else if(str_comp(pKey, "xesam:album") == 0)
				State.m_Album = ReadString(&Variant);
			else if(str_comp(pKey, "mpris:artUrl") == 0)
				State.m_ArtUrl = ReadString(&Variant);
			else if(str_comp(pKey, "mpris:trackid") == 0)
				State.m_TrackId = ReadString(&Variant);
			else if(str_comp(pKey, "mpris:length") == 0)
				State.m_LengthUs = ReadInt(&Variant);
		}
		dbus_message_iter_next(&Arr);
	}
}

bool ReadPlayer(DBusConnection *pConn, const std::string &BusName, SMediaState &State)
{
	SDBusMessage Msg(dbus_message_new_method_call(BusName.c_str(), MPRIS_PATH, "org.freedesktop.DBus.Properties", "GetAll"));
	if(!Msg.m_pMsg)
		return false;
	const char *pIface = MPRIS_PLAYER_IFACE;
	dbus_message_append_args(Msg.m_pMsg, DBUS_TYPE_STRING, &pIface, DBUS_TYPE_INVALID);
	SDBusMessage Reply(Call(pConn, Msg.m_pMsg));
	if(!Reply.m_pMsg)
		return false;

	DBusMessageIter It;
	if(!dbus_message_iter_init(Reply.m_pMsg, &It) || dbus_message_iter_get_arg_type(&It) != DBUS_TYPE_ARRAY)
		return false;

	State = SMediaState();
	State.m_Available = true;
	State.m_Player = BusName.substr(str_length(MPRIS_PREFIX));
	State.m_SampleTimeNs = NowNs();

	DBusMessageIter Arr;
	dbus_message_iter_recurse(&It, &Arr);
	while(dbus_message_iter_get_arg_type(&Arr) == DBUS_TYPE_DICT_ENTRY)
	{
		DBusMessageIter Entry, Variant;
		dbus_message_iter_recurse(&Arr, &Entry);
		const char *pKey = nullptr;
		dbus_message_iter_get_basic(&Entry, &pKey);
		dbus_message_iter_next(&Entry);
		dbus_message_iter_recurse(&Entry, &Variant);
		if(pKey)
		{
			if(str_comp(pKey, "PlaybackStatus") == 0)
				State.m_Playing = ReadString(&Variant) == "Playing";
			else if(str_comp(pKey, "Position") == 0)
				State.m_PositionUs = ReadInt(&Variant);
			else if(str_comp(pKey, "Metadata") == 0 && dbus_message_iter_get_arg_type(&Variant) == DBUS_TYPE_ARRAY)
				ReadMetadata(&Variant, State);
		}
		dbus_message_iter_next(&Arr);
	}
	return true;
}

void SendPlayerCommand(DBusConnection *pConn, const std::string &BusName, CMprisClient::ECommand Command)
{
	const char *pMethod = "PlayPause";
	if(Command == CMprisClient::ECommand::NEXT)
		pMethod = "Next";
	else if(Command == CMprisClient::ECommand::PREVIOUS)
		pMethod = "Previous";
	SDBusMessage Msg(dbus_message_new_method_call(BusName.c_str(), MPRIS_PATH, MPRIS_PLAYER_IFACE, pMethod));
	if(!Msg.m_pMsg)
		return;
	dbus_message_set_no_reply(Msg.m_pMsg, true);
	dbus_connection_send(pConn, Msg.m_pMsg, nullptr);
	dbus_connection_flush(pConn);
}

} // namespace

void CMprisClient::Run()
{
	DBusConnection *pConn = nullptr;
	int64_t LastConnectAttempt = 0;

	while(m_Running)
	{
		if(!pConn)
		{
			const int64_t Now = NowNs();
			if(Now - LastConnectAttempt > 3'000'000'000LL)
			{
				LastConnectAttempt = Now;
				DBusError Err;
				dbus_error_init(&Err);
				pConn = dbus_bus_get_private(DBUS_BUS_SESSION, &Err);
				if(dbus_error_is_set(&Err))
					dbus_error_free(&Err);
				if(pConn)
					dbus_connection_set_exit_on_disconnect(pConn, false);
			}
			if(!pConn)
			{
				std::this_thread::sleep_for(250ms);
				continue;
			}
		}

		std::string Preferred;
		std::vector<ECommand> vCommands;
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			Preferred = m_PreferredPlayer;
			vCommands.swap(m_vCommands);
		}

		// Pick the player: the preferred one if running, else the first one playing, else any.
		std::vector<std::string> vPlayers = ListPlayers(pConn);
		SMediaState Best;
		std::string BestName;
		int BestScore = -1;
		for(const std::string &Name : vPlayers)
		{
			SMediaState Candidate;
			if(!ReadPlayer(pConn, Name, Candidate))
				continue;
			int Score = Candidate.m_Playing ? 1 : 0;
			if(!Preferred.empty() && str_find_nocase(Name.c_str(), Preferred.c_str()))
				Score += 2;
			if(Score > BestScore)
			{
				BestScore = Score;
				Best = Candidate;
				BestName = Name;
			}
		}

		for(ECommand Command : vCommands)
			if(!BestName.empty())
				SendPlayerCommand(pConn, BestName, Command);

		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			m_State = Best;
		}

		if(!dbus_connection_get_is_connected(pConn))
		{
			dbus_connection_close(pConn);
			dbus_connection_unref(pConn);
			pConn = nullptr;
		}

		// Commands feel instant: re-poll quickly after sending one.
		std::this_thread::sleep_for(vCommands.empty() ? 200ms : 60ms);
	}

	if(pConn)
	{
		dbus_connection_close(pConn);
		dbus_connection_unref(pConn);
	}
}

#else

void CMprisClient::Run()
{
}

#endif
