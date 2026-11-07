#ifndef GAME_CLIENT_COMPONENTS_FOG_MUSIC_WIDGET_H
#define GAME_CLIENT_COMPONENTS_FOG_MUSIC_WIDGET_H

#include <game/client/component.h>

// Small liquid-glass "now playing" card in a corner of the HUD.
class CFogMusicWidget : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	// how far down the screen (0..1) the widget reaches when it sits in the top right corner, 0 otherwise
	float TopRightBottom() const { return m_TopRightBottom; }

private:
	float m_Visible = 0.0f; // 0..1 enter/leave animation
	float m_TopRightBottom = 0.0f;
	int m_SeenRevision = -1;
	float m_TrackFlash = 0.0f; // brief brighten when the track changes
};

#endif
