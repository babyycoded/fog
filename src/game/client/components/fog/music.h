#ifndef GAME_CLIENT_COMPONENTS_FOG_MUSIC_H
#define GAME_CLIENT_COMPONENTS_FOG_MUSIC_H

#include "lyrics.h"
#include "mpris.h"

#include <engine/console.h>

#include <engine/graphics.h>

#include <game/client/component.h>

#include <memory>
#include <string>

class IHttpRequest;

// Now playing state (MPRIS) plus synced lyrics from the local cache or LRCLIB.
class CFogMusic : public CComponent
{
public:
	enum class ELyricsStatus
	{
		NONE, // nothing playing or lyrics disabled
		LOADING,
		READY,
		NOT_FOUND,
		INSTRUMENTAL,
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnInit() override;
	void OnShutdown() override;
	void OnUpdate() override;

	const SMediaState &Media() const { return m_Media; }
	bool HasTrack() const { return m_Media.m_Available && !m_Media.m_Title.empty(); }
	// Playback position used for lyrics, with fog_lyrics_delay applied.
	int64_t LyricsTimeMs() const;
	int64_t PositionMs() const;
	const CLyrics &Lyrics() const { return m_Lyrics; }
	ELyricsStatus LyricsStatus() const { return m_LyricsStatus; }
	// Increments whenever the track or its lyrics change, so renderers can rebuild caches.
	int Revision() const { return m_Revision; }

	void Command(CMprisClient::ECommand Command) { m_Mpris.SendCommand(Command); }

	// album art of the current track (an invalid handle while there is none)
	IGraphics::CTextureHandle AlbumArt() const { return m_ArtTexture; }
	bool HasAlbumArt() const { return m_ArtTexture.IsValid(); }

private:
	static void ConPlayPause(IConsole::IResult *pResult, void *pUserData);
	static void ConNext(IConsole::IResult *pResult, void *pUserData);
	static void ConPrevious(IConsole::IResult *pResult, void *pUserData);
	static void ConLyricsReload(IConsole::IResult *pResult, void *pUserData);
	static void ConLyricsPreview(IConsole::IResult *pResult, void *pUserData);

	void OnTrackChanged();
	void CachePath(char *pBuf, int BufSize) const;
	bool LoadFromCache();
	void SaveToCache(const char *pLrc);
	void StartFetch(int Stage);
	void FinishFetch();
	void ApplyLrc(const char *pLrc);
	void UpdateAlbumArt();
	void SetAlbumArt(const unsigned char *pData, size_t Size);

	IGraphics::CTextureHandle m_ArtTexture;
	std::string m_ArtUrl; // the art currently loaded or loading
	std::shared_ptr<IHttpRequest> m_pArtRequest;

	CMprisClient m_Mpris;
	SMediaState m_Media;
	std::string m_TrackKey;
	CLyrics m_Lyrics;
	ELyricsStatus m_LyricsStatus = ELyricsStatus::NONE;
	int m_Revision = 0;

	std::shared_ptr<IHttpRequest> m_pLyricsRequest;
	int64_t m_PreviewStartNs = 0; // fog_lyrics_preview: a built-in sample plays instead of the player
	int m_FetchStage = 0; // 0 = exact get, 1 = search, 2 = search with a cleaned title
};

#endif
