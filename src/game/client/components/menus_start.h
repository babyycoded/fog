/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_MENUS_START_H
#define GAME_CLIENT_COMPONENTS_MENUS_START_H

#include <game/client/component.h>
#include <game/client/ui_rect.h>

class CMenusStart : public CComponentInterfaces
{
public:
	void RenderStartMenu(CUIRect MainView);
	// Fog client start screen (fog/start_screen.cpp)
	void RenderFogStart(CUIRect MainView);

private:
	bool CheckHotKey(int Key) const;

	float m_LastFogRender = -100.0f;
	float m_FogIntroStart = 0.0f;
	float m_FogHoverY = -1.0f;
	float m_FogHoverAlpha = 0.0f;
	float m_FogHoverV = 0.0f;
};

#endif
