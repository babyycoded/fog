#include "presence.h"

#include <base/net.h>
#include <base/secure.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>

namespace {

void HexColor(char *pBuf, int Size, unsigned PackedHsl)
{
	const ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(PackedHsl));
	str_format(pBuf, Size, "%02x%02x%02x", (int)(Color.r * 255.0f + 0.5f), (int)(Color.g * 255.0f + 0.5f), (int)(Color.b * 255.0f + 0.5f));
}

ColorRGBA ParseHex(const char *pHex, ColorRGBA Default)
{
	if(!pHex || str_length(pHex) != 6)
		return Default;
	unsigned Value = 0;
	for(int i = 0; i < 6; i++)
	{
		const char c = pHex[i];
		const int Digit = c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1));
		if(Digit < 0)
			return Default;
		Value = Value * 16 + Digit;
	}
	return ColorRGBA(((Value >> 16) & 0xff) / 255.0f, ((Value >> 8) & 0xff) / 255.0f, (Value & 0xff) / 255.0f, 1.0f);
}

} // namespace

SFogNameStyle CFogPresence::OwnStyle() const
{
	SFogNameStyle Style;
	Style.m_Mode = g_Config.m_FogNameStyle;
	Style.m_Color1 = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_FogNameColor1));
	Style.m_Color2 = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_FogNameColor2));
	Style.m_Speed = g_Config.m_FogNameSpeed / 100.0f;
	Style.m_Badge = g_Config.m_FogBadge != 0;
	return Style;
}

const SFogNameStyle *CFogPresence::Find(const char *pName) const
{
	const auto It = m_Players.find(pName);
	return It == m_Players.end() ? nullptr : &It->second;
}

void CFogPresence::OnStateChange(int NewState, int OldState)
{
	if(NewState != IClient::STATE_ONLINE)
		m_Players.clear();
	m_NextHeartbeat = 0.0f; // say hello right after connecting
}

void CFogPresence::OnShutdown()
{
	if(m_pRequest)
		m_pRequest->Abort();
	m_pRequest = nullptr;
}

void CFogPresence::OnRender()
{
	if(m_pRequest && m_pRequest->Done())
		FinishHeartbeat();

	if(!g_Config.m_FogPresence || !g_Config.m_FogPresenceUrl[0] || Client()->State() != IClient::STATE_ONLINE)
		return;

	// a new name or style should reach the others quickly
	if(str_comp(m_aLastName, Client()->PlayerName()) != 0)
	{
		str_copy(m_aLastName, Client()->PlayerName());
		m_NextHeartbeat = 0.0f;
	}
	if(!m_pRequest && Client()->LocalTime() >= m_NextHeartbeat)
		SendHeartbeat();
}

void CFogPresence::SendHeartbeat()
{
	if(!g_Config.m_FogPresenceId[0])
	{
		unsigned char aBytes[16];
		secure_random_fill(aBytes, sizeof(aBytes));
		char aHex[64];
		str_hex(aHex, sizeof(aHex), aBytes, sizeof(aBytes));
		// str_hex writes "aa bb ": keep the hex digits only
		char aId[33];
		int j = 0;
		for(int i = 0; aHex[i] && j < 32; i++)
			if(aHex[i] != ' ')
				aId[j++] = aHex[i];
		aId[j] = '\0';
		str_copy(g_Config.m_FogPresenceId, aId);
	}

	char aC1[8], aC2[8];
	HexColor(aC1, sizeof(aC1), g_Config.m_FogNameColor1);
	HexColor(aC2, sizeof(aC2), g_Config.m_FogNameColor2);
	CJsonStringWriter Writer;
	Writer.BeginObject();
	Writer.WriteAttribute("id");
	Writer.WriteStrValue(g_Config.m_FogPresenceId);
	Writer.WriteAttribute("name");
	Writer.WriteStrValue(Client()->PlayerName());
	// the address as ip:port, so everyone on the same server agrees on it however they connected
	char aServer[NETADDR_MAXSTRSIZE];
	net_addr_str(&Client()->ServerAddress(), aServer, sizeof(aServer), true);
	Writer.WriteAttribute("server");
	Writer.WriteStrValue(aServer);
	Writer.WriteAttribute("style");
	Writer.BeginObject();
	Writer.WriteAttribute("mode");
	Writer.WriteIntValue(g_Config.m_FogNameStyle);
	Writer.WriteAttribute("c1");
	Writer.WriteStrValue(aC1);
	Writer.WriteAttribute("c2");
	Writer.WriteStrValue(aC2);
	Writer.WriteAttribute("speed");
	Writer.WriteIntValue(g_Config.m_FogNameSpeed);
	Writer.WriteAttribute("badge");
	Writer.WriteBoolValue(g_Config.m_FogBadge != 0);
	if(g_Config.m_FogPresenceShareLook)
	{
		const SFogLook Look = SFogLook::FromConfig(false);
		if(!Look.Empty())
		{
			Writer.WriteAttribute("tee");
			Writer.WriteStrValue(Look.Serialize().c_str());
		}
	}
	Writer.EndObject();
	Writer.EndObject();
	const std::string Body = Writer.GetOutputString();

	char aUrl[256];
	str_format(aUrl, sizeof(aUrl), "%s/v1/heartbeat", g_Config.m_FogPresenceUrl);
	std::shared_ptr<IHttpRequest> pRequest = HttpPostJson(aUrl, Body.c_str());
	pRequest->Timeout(CTimeout{5000, 0, 500, 5});
	pRequest->MaxResponseSize(256 * 1024);
	pRequest->FailOnErrorStatus(false);
	pRequest->LogProgress(HTTPLOG::FAILURE);
	m_pRequest = pRequest;
	Http()->Run(pRequest);
	m_NextHeartbeat = Client()->LocalTime() + 20.0f;
}

void CFogPresence::FinishHeartbeat()
{
	std::shared_ptr<IHttpRequest> pRequest = m_pRequest;
	m_pRequest = nullptr;
	if(pRequest->State() != EHttpState::DONE || pRequest->StatusCode() >= 400)
		return;
	json_value *pJson = pRequest->ResultJson();
	if(!pJson)
		return;
	const json_value *pPlayers = json_object_get(pJson, "players");
	if(pPlayers && pPlayers->type == json_array)
	{
		m_Players.clear();
		for(int i = 0; i < json_array_length(pPlayers); i++)
		{
			const json_value *pPlayer = json_array_get(pPlayers, i);
			const json_value *pName = json_object_get(pPlayer, "name");
			const json_value *pStyle = json_object_get(pPlayer, "style");
			if(!pName || pName->type != json_string || !pStyle || pStyle->type != json_object)
				continue;
			SFogNameStyle Style;
			const json_value *pMode = json_object_get(pStyle, "mode");
			const json_value *pSpeed = json_object_get(pStyle, "speed");
			const json_value *pBadge = json_object_get(pStyle, "badge");
			const json_value *pC1 = json_object_get(pStyle, "c1");
			const json_value *pC2 = json_object_get(pStyle, "c2");
			if(pMode && pMode->type == json_integer)
				Style.m_Mode = std::clamp((int)pMode->u.integer, 0, 5);
			if(pSpeed && pSpeed->type == json_integer)
				Style.m_Speed = std::clamp((int)pSpeed->u.integer, 25, 400) / 100.0f;
			if(pBadge && pBadge->type == json_boolean)
				Style.m_Badge = pBadge->u.boolean != 0;
			Style.m_Color1 = ParseHex(pC1 && pC1->type == json_string ? pC1->u.string.ptr : nullptr, Style.m_Color1);
			Style.m_Color2 = ParseHex(pC2 && pC2->type == json_string ? pC2->u.string.ptr : nullptr, Style.m_Color2);
			const json_value *pTee = json_object_get(pStyle, "tee");
			if(pTee && pTee->type == json_string)
				Style.m_HasLook = Style.m_Look.Parse(pTee->u.string.ptr) && !Style.m_Look.Empty();
			m_Players[pName->u.string.ptr] = Style;
		}
	}
	json_value_free(pJson);
}
