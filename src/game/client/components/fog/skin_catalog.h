#ifndef GAME_CLIENT_COMPONENTS_FOG_SKIN_CATALOG_H
#define GAME_CLIENT_COMPONENTS_FOG_SKIN_CATALOG_H

#include <engine/graphics.h>

#include <game/client/component.h>
#include <game/client/skin.h>

#include <memory>
#include <string>
#include <vector>

class IHttpRequest;

// Fog tee studio: skins from the internet. Two sources:
//  - the DDNet skin database (ddnet.org/skins, normal and community skins, one JSON list)
//  - teedata.net (where skins.tw now lives; paged API with search and sorting)
// Pictures are downloaded into fog/skincache/<source>/ only for the cards you scroll to, and
// installing copies the file into skins/ so it becomes a normal skin.
class CFogSkinCatalog : public CComponent
{
public:
	enum
	{
		SOURCE_DDNET = 0,
		SOURCE_TEEDATA,
		NUM_SOURCES,
	};
	enum
	{
		STATE_NONE = 0,
		STATE_DOWNLOADING,
		STATE_READY,
		STATE_FAILED,
	};
	struct SEntry
	{
		std::string m_Name;
		std::string m_Creator;
		std::string m_Url;
		std::string m_CacheFile;
		std::string m_Date;
		bool m_Hd = false;
		bool m_Community = false;
		int m_State = STATE_NONE;
		std::shared_ptr<IHttpRequest> m_pDownload;
		CSkin::CSkinTextures m_Textures;
		float m_LastUsed = 0.0f;
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnShutdown() override;

	// what the studio shows
	void SetQuery(int Source, int Sort, const char *pSearch, int Filter);
	std::vector<int> &Visible() { return m_vVisible; } // indices into Entries(Source)
	std::vector<SEntry> &Entries(int Source) { return m_avEntries[Source]; }
	int CurrentSource() const { return m_Source; }
	bool Loading() const;
	const char *Error() const { return m_aError; }
	int Total() const { return m_Source == SOURCE_TEEDATA ? m_TeedataTotal : (int)m_vVisible.size(); }
	void LoadMore(); // teedata: next page
	bool CanLoadMore() const;

	// a card wants its picture: starts the download or loads the cached file
	const CSkin::CSkinTextures *Textures(SEntry &Entry);
	bool Installed(const SEntry &Entry) const;
	bool Install(SEntry &Entry, char *pInstalledName, int NameSize, char *pError, int ErrorSize);

private:
	void FetchDdnetList();
	void FetchTeedataPage();
	void FinishRequests();
	void Rebuild();
	void UnloadEntry(SEntry &Entry);
	void TrimTextures();
	bool LoadEntryTextures(SEntry &Entry);

	std::vector<SEntry> m_avEntries[NUM_SOURCES];
	std::vector<int> m_vVisible;
	int m_Source = SOURCE_DDNET;
	int m_Sort = 0;
	int m_Filter = 0;
	char m_aSearch[64] = "";

	std::shared_ptr<IHttpRequest> m_pDdnetList;
	bool m_DdnetListDone = false;
	std::shared_ptr<IHttpRequest> m_pTeedataPage;
	int m_TeedataPage = 0;
	int m_TeedataTotal = 0;
	bool m_TeedataEnd = false;
	char m_aError[128] = "";
	int m_ActiveDownloads = 0;
};

#endif
