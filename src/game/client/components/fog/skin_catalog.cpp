#include "skin_catalog.h"

#include <base/io.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/http.h>
#include <engine/image.h>
#include <engine/shared/json.h>
#include <engine/storage.h>

#include <generated/client_data.h>

#include <algorithm>

namespace {

constexpr int MAX_DOWNLOADS = 6;
constexpr int MAX_LOADED = 160;
constexpr int TEEDATA_PAGE = 48;
const char *const gs_apTeedataSorts[] = {"download", "trending", "date", "name", "hd"};

std::string SafeName(const char *pName)
{
	char aBuf[128];
	str_copy(aBuf, pName);
	str_sanitize_filename(aBuf);
	return aBuf;
}

const char *JsonString(const json_value *pObj, const char *pKey)
{
	const json_value *pVal = json_object_get(pObj, pKey);
	return pVal && pVal->type == json_string ? pVal->u.string.ptr : "";
}

} // namespace

void CFogSkinCatalog::OnShutdown()
{
	for(auto &vEntries : m_avEntries)
		for(auto &Entry : vEntries)
		{
			if(Entry.m_pDownload)
				Entry.m_pDownload->Abort();
			UnloadEntry(Entry);
		}
	if(m_pDdnetList)
		m_pDdnetList->Abort();
	if(m_pTeedataPage)
		m_pTeedataPage->Abort();
}

void CFogSkinCatalog::UnloadEntry(SEntry &Entry)
{
	Entry.m_Textures.Unload(Graphics());
	Entry.m_Textures.Reset();
	if(Entry.m_State == STATE_READY)
		Entry.m_State = STATE_NONE;
}

bool CFogSkinCatalog::Loading() const
{
	return (m_Source == SOURCE_DDNET && m_pDdnetList) || (m_Source == SOURCE_TEEDATA && m_pTeedataPage);
}

bool CFogSkinCatalog::CanLoadMore() const
{
	return m_Source == SOURCE_TEEDATA && !m_TeedataEnd && !m_pTeedataPage;
}

void CFogSkinCatalog::SetQuery(int Source, int Sort, const char *pSearch, int Filter)
{
	const bool Changed = Source != m_Source || Sort != m_Sort || Filter != m_Filter || str_comp(pSearch, m_aSearch) != 0;
	const bool FirstTeedata = m_avEntries[SOURCE_TEEDATA].empty() && !m_pTeedataPage && !m_TeedataEnd && m_aError[0] == '\0';
	const bool TeedataChanged = Source == SOURCE_TEEDATA && (Changed || FirstTeedata);
	m_Source = Source;
	m_Sort = Sort;
	m_Filter = Filter;
	str_copy(m_aSearch, pSearch);
	if(Source == SOURCE_DDNET && !m_DdnetListDone && !m_pDdnetList && m_aError[0] == '\0')
		FetchDdnetList();
	if(TeedataChanged)
	{
		if(m_pTeedataPage)
			m_pTeedataPage->Abort();
		m_pTeedataPage = nullptr;
		for(auto &Entry : m_avEntries[SOURCE_TEEDATA])
		{
			if(Entry.m_pDownload)
			{
				Entry.m_pDownload->Abort();
				m_ActiveDownloads--;
			}
			UnloadEntry(Entry);
		}
		m_avEntries[SOURCE_TEEDATA].clear();
		m_TeedataPage = 0;
		m_TeedataEnd = false;
		m_aError[0] = '\0';
		FetchTeedataPage();
	}
	if(Changed)
		Rebuild();
}

void CFogSkinCatalog::FetchDdnetList()
{
	m_aError[0] = '\0';
	m_pDdnetList = HttpGet("https://ddnet.org/skins/skin/skins.json");
	m_pDdnetList->Timeout(CTimeout{8000, 30000, 500, 10});
	m_pDdnetList->MaxResponseSize(8 * 1024 * 1024);
	Http()->Run(m_pDdnetList);
}

void CFogSkinCatalog::FetchTeedataPage()
{
	char aUrl[512], aName[192] = "";
	if(m_aSearch[0])
	{
		char aEscaped[160];
		EscapeUrl(aEscaped, m_aSearch);
		str_format(aName, sizeof(aName), "&name=%s", aEscaped);
	}
	str_format(aUrl, sizeof(aUrl), "https://teedata.net/api/skin/read?status=accepted&page=%d&limit=%d&sort=%s%s", m_TeedataPage + 1, TEEDATA_PAGE, gs_apTeedataSorts[std::clamp(m_Sort, 0, 4)], aName);
	m_pTeedataPage = HttpGet(aUrl);
	m_pTeedataPage->Timeout(CTimeout{8000, 20000, 500, 10});
	m_pTeedataPage->MaxResponseSize(4 * 1024 * 1024);
	Http()->Run(m_pTeedataPage);
}

void CFogSkinCatalog::LoadMore()
{
	if(CanLoadMore())
	{
		m_TeedataPage++;
		FetchTeedataPage();
	}
}

void CFogSkinCatalog::FinishRequests()
{
	if(m_pDdnetList && m_pDdnetList->Done())
	{
		std::shared_ptr<IHttpRequest> pRequest = m_pDdnetList;
		m_pDdnetList = nullptr;
		json_value *pJson = pRequest->State() == EHttpState::DONE ? pRequest->ResultJson() : nullptr;
		const json_value *pSkins = pJson ? json_object_get(pJson, "skins") : nullptr;
		if(pSkins && pSkins->type == json_array)
		{
			std::vector<SEntry> &vEntries = m_avEntries[SOURCE_DDNET];
			vEntries.clear();
			vEntries.reserve(pSkins->u.array.length);
			for(unsigned i = 0; i < pSkins->u.array.length; i++)
			{
				const json_value *pSkin = json_array_get(pSkins, i);
				SEntry Entry;
				Entry.m_Name = JsonString(pSkin, "name");
				if(Entry.m_Name.empty())
					continue;
				Entry.m_Creator = JsonString(pSkin, "creator");
				Entry.m_Date = JsonString(pSkin, "date");
				Entry.m_Community = str_comp(JsonString(pSkin, "type"), "community") == 0;
				const json_value *pHd = json_object_get(pSkin, "hd");
				const json_value *pUhd = pHd ? json_object_get(pHd, "uhd") : nullptr;
				Entry.m_Hd = pUhd && pUhd->type == json_boolean && pUhd->u.boolean;
				char aEscaped[256], aUrl[512];
				EscapeUrl(aEscaped, Entry.m_Name.c_str());
				str_format(aUrl, sizeof(aUrl), "https://ddnet.org/skins/skin/%s%s.png", Entry.m_Community ? "community/" : "", aEscaped);
				Entry.m_Url = aUrl;
				Entry.m_CacheFile = "fog/skincache/ddnet/" + SafeName(Entry.m_Name.c_str()) + ".png";
				vEntries.push_back(std::move(Entry));
			}
			m_DdnetListDone = true;
			Rebuild();
		}
		else
			str_copy(m_aError, "Could not load the DDNet skin list");
		if(pJson)
			json_value_free(pJson);
	}

	if(m_pTeedataPage && m_pTeedataPage->Done())
	{
		std::shared_ptr<IHttpRequest> pRequest = m_pTeedataPage;
		m_pTeedataPage = nullptr;
		json_value *pJson = pRequest->State() == EHttpState::DONE ? pRequest->ResultJson() : nullptr;
		const json_value *pResult = pJson ? json_object_get(pJson, "result") : nullptr;
		const json_value *pItems = pResult ? json_object_get(pResult, "items") : nullptr;
		if(pItems && pItems->type == json_array)
		{
			const json_value *pTotal = json_object_get(pResult, "totalCount");
			m_TeedataTotal = pTotal && pTotal->type == json_integer ? (int)pTotal->u.integer : 0;
			std::vector<SEntry> &vEntries = m_avEntries[SOURCE_TEEDATA];
			for(unsigned i = 0; i < pItems->u.array.length; i++)
			{
				const json_value *pSkin = json_array_get(pItems, i);
				SEntry Entry;
				Entry.m_Name = JsonString(pSkin, "name");
				const char *pPath = JsonString(pSkin, "file_path");
				if(Entry.m_Name.empty() || !pPath[0])
					continue;
				const json_value *pAuthor = json_object_get(pSkin, "author");
				if(pAuthor && pAuthor->type == json_object)
					Entry.m_Creator = JsonString(pAuthor, "name");
				Entry.m_Date = std::string(JsonString(pSkin, "created_at")).substr(0, 10);
				const json_value *pWidth = json_object_get(pSkin, "width");
				Entry.m_Hd = pWidth && pWidth->type == json_integer && pWidth->u.integer > 256;
				char aEscaped[256], aUrl[512];
				EscapeUrl(aEscaped, Entry.m_Name.c_str());
				str_format(aUrl, sizeof(aUrl), "https://teedata.net/api/skin/resolve/%s", aEscaped);
				Entry.m_Url = aUrl;
				Entry.m_CacheFile = "fog/skincache/teedata/" + SafeName(Entry.m_Name.c_str()) + ".png";
				vEntries.push_back(std::move(Entry));
			}
			m_TeedataEnd = pItems->u.array.length < (unsigned)TEEDATA_PAGE || (int)vEntries.size() >= m_TeedataTotal;
			Rebuild();
		}
		else
		{
			str_copy(m_aError, "Could not reach teedata.net");
			m_TeedataEnd = true;
		}
		if(pJson)
			json_value_free(pJson);
	}

	for(auto &vEntries : m_avEntries)
		for(auto &Entry : vEntries)
			if(Entry.m_State == STATE_DOWNLOADING && Entry.m_pDownload && Entry.m_pDownload->Done())
			{
				const bool Ok = Entry.m_pDownload->State() == EHttpState::DONE;
				Entry.m_pDownload = nullptr;
				m_ActiveDownloads--;
				Entry.m_State = Ok && LoadEntryTextures(Entry) ? STATE_READY : STATE_FAILED;
			}
}

void CFogSkinCatalog::Rebuild()
{
	m_vVisible.clear();
	std::vector<SEntry> &vEntries = m_avEntries[m_Source];
	for(size_t i = 0; i < vEntries.size(); i++)
	{
		const SEntry &Entry = vEntries[i];
		if(m_Source == SOURCE_DDNET)
		{
			if(m_aSearch[0] && !str_find_nocase(Entry.m_Name.c_str(), m_aSearch) && !str_find_nocase(Entry.m_Creator.c_str(), m_aSearch))
				continue;
			if((m_Filter == 1 && Entry.m_Community) || (m_Filter == 2 && !Entry.m_Community) || (m_Filter == 3 && !Entry.m_Hd))
				continue;
		}
		m_vVisible.push_back((int)i);
	}
	if(m_Source == SOURCE_DDNET)
	{
		if(m_Sort == 0)
			std::stable_sort(m_vVisible.begin(), m_vVisible.end(), [&](int a, int b) { return vEntries[a].m_Date > vEntries[b].m_Date; });
		else
			std::stable_sort(m_vVisible.begin(), m_vVisible.end(), [&](int a, int b) { return str_comp_nocase(vEntries[a].m_Name.c_str(), vEntries[b].m_Name.c_str()) < 0; });
	}
}

bool CFogSkinCatalog::LoadEntryTextures(SEntry &Entry)
{
	CImageInfo Image;
	if(!Graphics()->LoadPng(Image, Entry.m_CacheFile.c_str(), IStorage::TYPE_SAVE))
		return false;
	if(Image.m_Format != CImageInfo::FORMAT_RGBA || Image.m_Width < 64 || Image.m_Width != Image.m_Height * 2)
	{
		Image.Free();
		return false;
	}
	CSkin::CSkinTextures &t = Entry.m_Textures;
	t.m_Body = Graphics()->LoadSpriteTexture(Image, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_BODY]);
	t.m_BodyOutline = Graphics()->LoadSpriteTexture(Image, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_BODY_OUTLINE]);
	t.m_Feet = Graphics()->LoadSpriteTexture(Image, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_FOOT]);
	t.m_FeetOutline = Graphics()->LoadSpriteTexture(Image, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_FOOT_OUTLINE]);
	t.m_Hands = Graphics()->LoadSpriteTexture(Image, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_HAND]);
	t.m_HandsOutline = Graphics()->LoadSpriteTexture(Image, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_HAND_OUTLINE]);
	for(size_t i = 0; i < std::size(t.m_aEyes); i++)
		t.m_aEyes[i] = Graphics()->LoadSpriteTexture(Image, std::nullopt, &g_pData->m_aSprites[SPRITE_TEE_EYE_NORMAL + i]);
	Image.Free();
	TrimTextures();
	return true;
}

void CFogSkinCatalog::TrimTextures()
{
	std::vector<SEntry *> vLoaded;
	for(auto &vEntries : m_avEntries)
		for(auto &Entry : vEntries)
			if(Entry.m_State == STATE_READY)
				vLoaded.push_back(&Entry);
	if((int)vLoaded.size() <= MAX_LOADED)
		return;
	std::sort(vLoaded.begin(), vLoaded.end(), [](const SEntry *a, const SEntry *b) { return a->m_LastUsed < b->m_LastUsed; });
	for(size_t i = 0; i + MAX_LOADED < vLoaded.size(); i++)
		UnloadEntry(*vLoaded[i]);
}

const CSkin::CSkinTextures *CFogSkinCatalog::Textures(SEntry &Entry)
{
	Entry.m_LastUsed = Client()->GlobalTime();
	if(Entry.m_State == STATE_READY)
		return &Entry.m_Textures;
	if(Entry.m_State != STATE_NONE)
		return nullptr;
	if(Storage()->FileExists(Entry.m_CacheFile.c_str(), IStorage::TYPE_SAVE))
	{
		Entry.m_State = LoadEntryTextures(Entry) ? STATE_READY : STATE_FAILED;
		return Entry.m_State == STATE_READY ? &Entry.m_Textures : nullptr;
	}
	if(m_ActiveDownloads >= MAX_DOWNLOADS)
		return nullptr;
	Storage()->CreateFolder("fog", IStorage::TYPE_SAVE);
	Storage()->CreateFolder("fog/skincache", IStorage::TYPE_SAVE);
	Storage()->CreateFolder(Entry.m_CacheFile.substr(0, Entry.m_CacheFile.rfind('/')).c_str(), IStorage::TYPE_SAVE);
	Entry.m_pDownload = HttpGetFile(Entry.m_Url.c_str(), Storage(), Entry.m_CacheFile.c_str(), IStorage::TYPE_SAVE);
	Entry.m_pDownload->Timeout(CTimeout{8000, 30000, 500, 10});
	Entry.m_pDownload->MaxResponseSize(4 * 1024 * 1024);
	Http()->Run(Entry.m_pDownload);
	Entry.m_State = STATE_DOWNLOADING;
	m_ActiveDownloads++;
	return nullptr;
}

bool CFogSkinCatalog::Installed(const SEntry &Entry) const
{
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "skins/%s.png", SafeName(Entry.m_Name.c_str()).c_str());
	return Storage()->FileExists(aPath, IStorage::TYPE_ALL);
}

bool CFogSkinCatalog::Install(SEntry &Entry, char *pInstalledName, int NameSize, char *pError, int ErrorSize)
{
	const std::string Name = SafeName(Entry.m_Name.c_str());
	str_copy(pInstalledName, Name.c_str(), NameSize);
	if(Installed(Entry))
		return true;
	void *pData = nullptr;
	unsigned Size = 0;
	if(!Storage()->ReadFile(Entry.m_CacheFile.c_str(), IStorage::TYPE_SAVE, &pData, &Size))
	{
		str_copy(pError, "The picture is not downloaded yet", ErrorSize);
		return false;
	}
	Storage()->CreateFolder("skins", IStorage::TYPE_SAVE);
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "skins/%s.png", Name.c_str());
	IOHANDLE File = Storage()->OpenFile(aPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	bool Ok = false;
	if(File)
	{
		Ok = io_write(File, pData, Size) == Size;
		io_close(File);
	}
	free(pData);
	if(!Ok)
		str_copy(pError, "Could not write into skins/", ErrorSize);
	return Ok;
}

void CFogSkinCatalog::OnRender()
{
	FinishRequests();
}
