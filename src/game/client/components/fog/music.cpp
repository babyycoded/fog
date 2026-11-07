#include "music.h"

#include <base/io.h>
#include <base/mem.h>
#include <base/log.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/storage.h>

#include <cmath>
#include <cstdio>
#include <vector>

#if defined(CONF_FOG_TURBOJPEG)
#include <turbojpeg.h>
#endif
#include <cstdlib>

static constexpr const char *LYRICS_FOLDER = "fog/lyrics";
// versioned: a smarter search in a later build retries tracks that found nothing before
static constexpr const char *NO_LYRICS_MARKER = "#fog:none:v2";
static constexpr const char *INSTRUMENTAL_MARKER = "#fog:instrumental";

// Strips edit markers so "Song - slowed + reverb" or "Song (feat. X)" finds the original.
// Reports whether the title marks a tempo change, in which case timings get rescaled.
static std::string CleanTitle(const std::string &Title, bool &TempoVariant)
{
	static const char *const s_apTempo[] = {"slowed", "sped up", "speed up", "speedup", "nightcore", "reverb", "daycore"};
	static const char *const s_apNoise[] = {"feat.", "feat ", "ft.", "remaster", "remix", "version", "edit", "radio", "bonus", "explicit", "clean"};
	TempoVariant = false;
	std::string Result = Title;

	// bracketed parts: drop the ones that only describe the edit
	for(const char *pPair : {"()", "[]"})
	{
		size_t Open;
		while((Open = Result.find(pPair[0])) != std::string::npos)
		{
			const size_t Close = Result.find(pPair[1], Open);
			if(Close == std::string::npos)
				break;
			const std::string Inner = Result.substr(Open + 1, Close - Open - 1);
			bool Drop = false;
			for(const char *pWord : s_apTempo)
				if(str_find_nocase(Inner.c_str(), pWord))
					Drop = TempoVariant = true;
			for(const char *pWord : s_apNoise)
				if(str_find_nocase(Inner.c_str(), pWord))
					Drop = true;
			if(!Drop)
				break;
			Result.erase(Open, Close - Open + 1);
		}
	}

	// " - slowed", " - Remastered 2011" style suffixes
	const size_t Dash = Result.find(" - ");
	if(Dash != std::string::npos)
	{
		const std::string Tail = Result.substr(Dash + 3);
		bool Drop = false;
		for(const char *pWord : s_apTempo)
			if(str_find_nocase(Tail.c_str(), pWord))
				Drop = TempoVariant = true;
		for(const char *pWord : s_apNoise)
			if(str_find_nocase(Tail.c_str(), pWord))
				Drop = true;
		if(Drop)
			Result.erase(Dash);
	}

	while(!Result.empty() && Result.back() == ' ')
		Result.pop_back();
	return Result.empty() ? Title : Result;
}

void CFogMusic::OnConsoleInit()
{
	Console()->Register("fog_music_playpause", "", CFGFLAG_CLIENT, ConPlayPause, this, "Play or pause the current MPRIS player (Spotify)");
	Console()->Register("fog_music_next", "", CFGFLAG_CLIENT, ConNext, this, "Skip to the next track");
	Console()->Register("fog_music_prev", "", CFGFLAG_CLIENT, ConPrevious, this, "Go back to the previous track");
	Console()->Register("fog_lyrics_preview", "", CFGFLAG_CLIENT, ConLyricsPreview, this, "Play a short built-in lyrics sample to preview the lyrics look (run again to stop)");
	Console()->Register("fog_fx_toggle", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *) { g_Config.m_FogFxLive ^= 1; }, this, "Switch the live shader effects on or off");
	Console()->Register("fog_lyrics_reload", "", CFGFLAG_CLIENT, ConLyricsReload, this, "Download the lyrics of the current track again");
}

void CFogMusic::ConPlayPause(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CFogMusic *>(pUserData)->Command(CMprisClient::ECommand::PLAY_PAUSE);
}

void CFogMusic::ConNext(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CFogMusic *>(pUserData)->Command(CMprisClient::ECommand::NEXT);
}

void CFogMusic::ConPrevious(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CFogMusic *>(pUserData)->Command(CMprisClient::ECommand::PREVIOUS);
}

void CFogMusic::ConLyricsReload(IConsole::IResult *pResult, void *pUserData)
{
	CFogMusic *pSelf = static_cast<CFogMusic *>(pUserData);
	if(pSelf->m_TrackKey.empty())
		return;
	char aPath[IO_MAX_PATH_LENGTH];
	pSelf->CachePath(aPath, sizeof(aPath));
	pSelf->Storage()->RemoveFile(aPath, IStorage::TYPE_SAVE);
	pSelf->m_TrackKey.clear(); // forces OnTrackChanged on the next update
}

// Original sample text in the Fog mood, with enhanced (word) timings on the second half.
static constexpr const char *PREVIEW_LRC =
	"[00:00.50]grey light settles on the empty road\n"
	"[00:04.20]every jump I make is softer than the last\n"
	"[00:08.00]<00:08.00>the <00:08.35>fog <00:08.90>keeps <00:09.30>what <00:09.60>I <00:09.80>let <00:10.20>go <00:11.20>\n"
	"[00:11.80]<00:11.80>and <00:12.10>I <00:12.30>keep <00:12.70>moving <00:13.60>through <00:14.20>it <00:15.00>\n"
	"[00:15.60]\n"
	"[00:17.00]грустный тэ летит сквозь туман\n";

void CFogMusic::ConLyricsPreview(IConsole::IResult *pResult, void *pUserData)
{
	CFogMusic *pSelf = static_cast<CFogMusic *>(pUserData);
	pSelf->m_PreviewStartNs = pSelf->m_PreviewStartNs ? 0 : time_get_nanoseconds().count();
	pSelf->m_TrackKey = "\x01preview-switch"; // forces a refresh on the next update
}

void CFogMusic::OnInit()
{
	Storage()->CreateFolder("fog", IStorage::TYPE_SAVE);
	Storage()->CreateFolder(LYRICS_FOLDER, IStorage::TYPE_SAVE);
	if(!m_Mpris.Supported())
		log_info("fog", "built without D-Bus, music integration is unavailable");
}

void CFogMusic::OnShutdown()
{
	if(m_pLyricsRequest)
		m_pLyricsRequest->Abort();
	m_Mpris.Stop();
}

int64_t CFogMusic::PositionMs() const
{
	return CMprisClient::EstimatePositionUs(m_Media, time_get_nanoseconds().count()) / 1000;
}

int64_t CFogMusic::LyricsTimeMs() const
{
	return PositionMs() - g_Config.m_FogLyricsDelay;
}

void CFogMusic::OnUpdate()
{
	if(!g_Config.m_FogMusic)
	{
		m_Mpris.Stop();
		if(m_Media.m_Available)
		{
			m_Media = SMediaState();
			m_TrackKey.clear();
			m_Lyrics.Clear();
			m_LyricsStatus = ELyricsStatus::NONE;
			m_Revision++;
		}
		return;
	}

	m_Mpris.Start();
	m_Mpris.SetPreferredPlayer(g_Config.m_FogMusicPlayer);
	m_Media = m_Mpris.State();
	UpdateAlbumArt();

	if(m_PreviewStartNs)
	{
		const int64_t NowNs = time_get_nanoseconds().count();
		const int64_t LengthUs = 21'000'000;
		if((NowNs - m_PreviewStartNs) / 1000 > LengthUs)
			m_PreviewStartNs = 0;
		else
		{
			m_Media = SMediaState();
			m_Media.m_Available = true;
			m_Media.m_Playing = true;
			m_Media.m_Player = "preview";
			m_Media.m_Title = "Lyrics preview";
			m_Media.m_Artist = "Fog";
			m_Media.m_LengthUs = LengthUs;
			m_Media.m_PositionUs = (NowNs - m_PreviewStartNs) / 1000;
			m_Media.m_SampleTimeNs = NowNs;
			const std::string Key = "\x01preview";
			if(m_TrackKey != Key)
			{
				m_TrackKey = Key;
				if(m_pLyricsRequest)
				{
					m_pLyricsRequest->Abort();
					m_pLyricsRequest = nullptr;
				}
				m_Lyrics.Clear();
				ApplyLrc(PREVIEW_LRC);
			}
			return;
		}
	}

	std::string Key;
	if(HasTrack())
		Key = m_Media.m_Artist + "\n" + m_Media.m_Title;
	if(Key != m_TrackKey)
	{
		m_TrackKey = Key;
		OnTrackChanged();
	}

	if(m_pLyricsRequest && m_pLyricsRequest->Done())
		FinishFetch();
}

void CFogMusic::OnTrackChanged()
{
	if(m_pLyricsRequest)
	{
		m_pLyricsRequest->Abort();
		m_pLyricsRequest = nullptr;
	}
	m_Lyrics.Clear();
	m_Revision++;

	if(m_TrackKey.empty() || !g_Config.m_FogLyrics)
	{
		m_LyricsStatus = ELyricsStatus::NONE;
		return;
	}
	if(LoadFromCache())
		return;
	if(!g_Config.m_FogLyricsOnline)
	{
		m_LyricsStatus = ELyricsStatus::NOT_FOUND;
		return;
	}
	StartFetch(0);
}

void CFogMusic::CachePath(char *pBuf, int BufSize) const
{
	char aName[160];
	str_format(aName, sizeof(aName), "%s - %s", m_Media.m_Artist.c_str(), m_Media.m_Title.c_str());
	str_sanitize_filename(aName);
	// keep the name readable but bounded; the hash keeps similar titles apart
	char aShort[96];
	str_truncate(aShort, sizeof(aShort), aName, str_length(aName));
	unsigned Hash = 2166136261u;
	for(char c : m_TrackKey)
		Hash = (Hash ^ (unsigned char)c) * 16777619u;
	str_format(pBuf, BufSize, "%s/%s_%08x.lrc", LYRICS_FOLDER, aShort, Hash);
}

bool CFogMusic::LoadFromCache()
{
	char aPath[IO_MAX_PATH_LENGTH];
	CachePath(aPath, sizeof(aPath));
	char *pLrc = Storage()->ReadFileStr(aPath, IStorage::TYPE_SAVE);
	if(!pLrc)
		return false;
	if(str_startswith(pLrc, "#fog:none") && !str_startswith(pLrc, NO_LYRICS_MARKER))
	{
		free(pLrc);
		return false;
	}
	ApplyLrc(pLrc);
	free(pLrc);
	return true;
}

void CFogMusic::SaveToCache(const char *pLrc)
{
	char aPath[IO_MAX_PATH_LENGTH];
	CachePath(aPath, sizeof(aPath));
	IOHANDLE File = Storage()->OpenFile(aPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return;
	io_write(File, pLrc, str_length(pLrc));
	io_close(File);
}

void CFogMusic::ApplyLrc(const char *pLrc)
{
	m_Revision++;
	if(str_startswith(pLrc, INSTRUMENTAL_MARKER))
	{
		m_LyricsStatus = ELyricsStatus::INSTRUMENTAL;
		return;
	}
	if(str_startswith(pLrc, NO_LYRICS_MARKER))
	{
		m_LyricsStatus = ELyricsStatus::NOT_FOUND;
		return;
	}
	m_LyricsStatus = m_Lyrics.Parse(pLrc, m_Media.m_LengthUs / 1000) ? ELyricsStatus::READY : ELyricsStatus::NOT_FOUND;
}

void CFogMusic::StartFetch(int Stage)
{
	bool TempoVariant;
	const std::string Title = Stage == 2 ? CleanTitle(m_Media.m_Title, TempoVariant) : m_Media.m_Title;
	// only the first artist: "A, B" rarely matches a database entry
	std::string Artist = m_Media.m_Artist;
	if(Stage == 2 && Artist.find(", ") != std::string::npos)
		Artist.erase(Artist.find(", "));

	char aArtist[256], aTitle[256], aAlbum[256];
	EscapeUrl(aArtist, Artist.c_str());
	EscapeUrl(aTitle, Title.c_str());
	EscapeUrl(aAlbum, m_Media.m_Album.c_str());

	char aUrl[1024];
	if(Stage > 0)
		str_format(aUrl, sizeof(aUrl), "https://lrclib.net/api/search?track_name=%s&artist_name=%s", aTitle, aArtist);
	else if(m_Media.m_LengthUs > 0)
		str_format(aUrl, sizeof(aUrl), "https://lrclib.net/api/get?artist_name=%s&track_name=%s&album_name=%s&duration=%d", aArtist, aTitle, aAlbum, (int)std::lround(m_Media.m_LengthUs / 1e6));
	else
		str_format(aUrl, sizeof(aUrl), "https://lrclib.net/api/get?artist_name=%s&track_name=%s", aArtist, aTitle);

	std::shared_ptr<IHttpRequest> pRequest = HttpGet(aUrl);
	pRequest->Timeout(CTimeout{8000, 0, 500, 10});
	pRequest->MaxResponseSize(2 * 1024 * 1024);
	pRequest->FailOnErrorStatus(false);
	pRequest->LogProgress(HTTPLOG::FAILURE);
	pRequest->HeaderString("Lrclib-Client", "DDNet Fog client");
	m_pLyricsRequest = pRequest;
	m_FetchStage = Stage;
	m_LyricsStatus = ELyricsStatus::LOADING;
	Http()->Run(pRequest);
}

static double JsonNumber(const json_value *pValue)
{
	if(!pValue)
		return -1.0;
	if(pValue->type == json_integer)
		return (double)pValue->u.integer;
	if(pValue->type == json_double)
		return pValue->u.dbl;
	return -1.0;
}

void CFogMusic::FinishFetch()
{
	std::shared_ptr<IHttpRequest> pRequest = m_pLyricsRequest;
	m_pLyricsRequest = nullptr;

	if(pRequest->State() != EHttpState::DONE)
	{
		// network trouble: don't cache, just give up for this play
		m_LyricsStatus = ELyricsStatus::NOT_FOUND;
		return;
	}

	json_value *pJson = pRequest->StatusCode() < 400 ? pRequest->ResultJson() : nullptr;
	const json_value *pEntry = nullptr;
	if(pJson && pJson->type == json_object)
	{
		pEntry = pJson;
	}
	else if(pJson && pJson->type == json_array)
	{
		// search: prefer synced lyrics whose duration is closest to the track's
		const double Length = m_Media.m_LengthUs / 1e6;
		double BestDiff = 1e9;
		for(int i = 0; i < json_array_length(pJson); i++)
		{
			const json_value *pItem = json_array_get(pJson, i);
			const json_value *pSynced = json_object_get(pItem, "syncedLyrics");
			if(!pSynced || pSynced->type != json_string)
				continue;
			const double Diff = Length > 0 ? std::abs(JsonNumber(json_object_get(pItem, "duration")) - Length) : 0.0;
			if(Diff < BestDiff)
			{
				BestDiff = Diff;
				pEntry = pItem;
			}
		}
	}

	const char *pLrc = nullptr;
	bool Instrumental = false;
	double TimeScale = 1.0;
	if(pEntry)
	{
		// slowed or sped up edits: stretch the original timings to this track's length
		bool TempoVariant = false;
		CleanTitle(m_Media.m_Title, TempoVariant);
		const double EntryLength = JsonNumber(json_object_get(pEntry, "duration"));
		const double Length = m_Media.m_LengthUs / 1e6;
		if(TempoVariant && EntryLength > 1.0 && Length > 1.0 && std::abs(Length - EntryLength) > 2.0)
			TimeScale = Length / EntryLength;
		const json_value *pSynced = json_object_get(pEntry, "syncedLyrics");
		if(pSynced && pSynced->type == json_string)
			pLrc = json_string_get(pSynced);
		const json_value *pInstrumental = json_object_get(pEntry, "instrumental");
		Instrumental = pInstrumental && pInstrumental->type == json_boolean && pInstrumental->u.boolean;
	}

	if(pLrc && pLrc[0])
	{
		std::string Lrc = pLrc;
		if(TimeScale != 1.0)
		{
			char aScale[64];
			str_format(aScale, sizeof(aScale), "[fog_scale:%.6f]\n", TimeScale);
			Lrc = aScale + Lrc;
		}
		SaveToCache(Lrc.c_str());
		ApplyLrc(Lrc.c_str());
	}
	else if(Instrumental)
	{
		SaveToCache(INSTRUMENTAL_MARKER);
		ApplyLrc(INSTRUMENTAL_MARKER);
	}
	else if(m_FetchStage < 2)
	{
		// exact match failed (album or duration differ): search, then search with a cleaned title
		int Next = m_FetchStage + 1;
		bool TempoVariant;
		if(Next == 2 && CleanTitle(m_Media.m_Title, TempoVariant) == m_Media.m_Title && m_Media.m_Artist.find(", ") == std::string::npos)
			Next = 3; // nothing to clean, the second search would be identical
		if(Next <= 2)
		{
			if(pJson)
				json_value_free(pJson);
			StartFetch(Next);
			return;
		}
		SaveToCache(NO_LYRICS_MARKER);
		ApplyLrc(NO_LYRICS_MARKER);
	}
	else
	{
		SaveToCache(NO_LYRICS_MARKER);
		ApplyLrc(NO_LYRICS_MARKER);
	}

	if(pJson)
		json_value_free(pJson);
}

// ---------------------------------------------------------------- album art

void CFogMusic::SetAlbumArt(const unsigned char *pData, size_t Size)
{
	Graphics()->UnloadTexture(&m_ArtTexture);
	if(!pData || Size < 8)
		return;
	CImageInfo Image;
	static const unsigned char s_aPngMagic[] = {0x89, 'P', 'N', 'G'};
	if(mem_comp(pData, s_aPngMagic, sizeof(s_aPngMagic)) == 0)
	{
		if(!Graphics()->LoadPng(Image, pData, Size, "album art"))
			return;
		if(Image.m_Format != CImageInfo::FORMAT_RGBA)
		{
			Image.Free();
			return;
		}
	}
	else
	{
#if defined(CONF_FOG_TURBOJPEG)
		tjhandle Handle = tjInitDecompress();
		if(!Handle)
			return;
		int Width = 0, Height = 0, Subsamp = 0, Colorspace = 0;
		if(tjDecompressHeader3(Handle, pData, (unsigned long)Size, &Width, &Height, &Subsamp, &Colorspace) != 0 || Width <= 0 || Height <= 0 || Width > 4096 || Height > 4096)
		{
			tjDestroy(Handle);
			return;
		}
		Image.m_Width = Width;
		Image.m_Height = Height;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		Image.m_pData = static_cast<uint8_t *>(malloc(Image.DataSize()));
		const int Result = tjDecompress2(Handle, pData, (unsigned long)Size, Image.m_pData, Width, 0, Height, TJPF_RGBA, TJFLAG_FASTDCT);
		tjDestroy(Handle);
		if(Result != 0)
		{
			Image.Free();
			return;
		}
#else
		return;
#endif
	}
	m_ArtTexture = Graphics()->LoadTextureRawMove(Image, 0, "fog album art");
}

void CFogMusic::UpdateAlbumArt()
{
	if(m_pArtRequest && m_pArtRequest->Done())
	{
		std::shared_ptr<IHttpRequest> pRequest = m_pArtRequest;
		m_pArtRequest = nullptr;
		if(pRequest->State() == EHttpState::DONE && pRequest->StatusCode() < 400)
		{
			unsigned char *pData = nullptr;
			size_t Size = 0;
			pRequest->Result(&pData, &Size);
			SetAlbumArt(pData, Size);
		}
	}

	const std::string &Url = HasTrack() ? m_Media.m_ArtUrl : std::string();
	if(Url == m_ArtUrl)
		return;
	m_ArtUrl = Url;
	if(m_pArtRequest)
	{
		m_pArtRequest->Abort();
		m_pArtRequest = nullptr;
	}
	Graphics()->UnloadTexture(&m_ArtTexture);
	if(Url.empty())
		return;

	if(str_startswith(Url.c_str(), "file://"))
	{
		// local players point at a file on disk (percent-encoded)
		std::string Path;
		for(const char *p = Url.c_str() + 7; *p; p++)
		{
			if(p[0] == '%' && p[1] && p[2])
			{
				char aHex[3] = {p[1], p[2], 0};
				Path += (char)strtol(aHex, nullptr, 16);
				p += 2;
			}
			else
				Path += *p;
		}
		FILE *pFile = fopen(Path.c_str(), "rb");
		if(!pFile)
			return;
		std::vector<unsigned char> vData;
		unsigned char aBuf[16384];
		size_t Read;
		while((Read = fread(aBuf, 1, sizeof(aBuf), pFile)) > 0 && vData.size() < 16 * 1024 * 1024)
			vData.insert(vData.end(), aBuf, aBuf + Read);
		fclose(pFile);
		SetAlbumArt(vData.data(), vData.size());
	}
	else if(str_startswith(Url.c_str(), "https://"))
	{
		std::shared_ptr<IHttpRequest> pRequest = HttpGet(Url.c_str());
		pRequest->Timeout(CTimeout{8000, 0, 500, 10});
		pRequest->MaxResponseSize(8 * 1024 * 1024);
		pRequest->FailOnErrorStatus(false);
		pRequest->LogProgress(HTTPLOG::FAILURE);
		m_pArtRequest = pRequest;
		Http()->Run(pRequest);
	}
}
