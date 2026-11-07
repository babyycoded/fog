#include "music_widget.h"

#include "glass.h"
#include "music.h"

#include <base/math.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/gameclient.h>
#include <game/client/ui.h>

#include <algorithm>
#include <cmath>

static void FormatTime(char *pBuf, int Size, int64_t Ms)
{
	const int Seconds = (int)(std::max<int64_t>(Ms, 0) / 1000);
	str_format(pBuf, Size, "%d:%02d", Seconds / 60, Seconds % 60);
}

void CFogMusicWidget::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	const CFogMusic &Music = GameClient()->m_FogMusic;
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	const float Speed = g_Config.m_FogAnim ? g_Config.m_FogAnimSpeed / 100.0f : 1000.0f;
	const float Intensity = g_Config.m_FogAnim ? g_Config.m_FogAnimIntensity / 100.0f : 0.0f;

	const bool Want = g_Config.m_FogMusicWidget && Music.HasTrack() && !GameClient()->m_Menus.IsActive();
	m_Visible = std::clamp(m_Visible + (Want ? 1.0f : -1.0f) * Dt * 3.5f * Speed, 0.0f, 1.0f);
	if(Music.Revision() != m_SeenRevision)
	{
		m_SeenRevision = Music.Revision();
		m_TrackFlash = 1.0f;
	}
	m_TrackFlash = std::max(0.0f, m_TrackFlash - Dt * 1.2f * std::min(Speed, 10.0f));
	if(m_Visible <= 0.0f)
		m_TopRightBottom = 0.0f;
	if(m_Visible <= 0.0f)
		return;

	const SMediaState &Media = Music.Media();
	const float Ease = 1.0f - std::pow(1.0f - m_Visible, 3.0f);

	Ui()->MapScreen();
	const CUIRect *pScreen = Ui()->Screen();
	const float Width = 168.0f, Height = 40.0f, Margin = 8.0f;
	const int Corner = g_Config.m_FogMusicWidgetCorner;
	const bool Right = Corner == 1 || Corner == 3;
	const bool Bottom = Corner >= 2;

	CUIRect Card;
	Card.w = Width;
	Card.h = Height;
	Card.x = Right ? pScreen->w - Width - Margin : Margin;
	Card.y = Bottom ? pScreen->h - Height - Margin : Margin;
	// stay clear of the FPS counter in the top right
	if(!Bottom && Right && g_Config.m_ClShowfps)
		Card.y += 16.0f;
	m_TopRightBottom = (Right && !Bottom) ? (Card.y + Card.h) / pScreen->h * Ease : 0.0f;
	// glide in from the nearest edge
	Card.x += (Right ? 1.0f : -1.0f) * (1.0f - Ease) * 18.0f;
	const float Alpha = Ease;

	FogDrawGlass(Graphics(), Card, 9.0f, Alpha);

	CUIRect Content, Bars, Text, Progress;
	Card.Margin(vec2(8.0f, 6.0f), &Content);
	Content.HSplitBottom(6.0f, &Content, &Progress);
	const bool HasArt = Music.HasAlbumArt();
	Content.VSplitLeft(HasArt ? Content.h : 14.0f, &Bars, &Text);
	Text.VSplitLeft(HasArt ? 7.0f : 5.0f, nullptr, &Text);
	if(HasArt)
		FogDrawAlbumArt(Graphics(), Music.AlbumArt(), Bars, Alpha);

	// equalizer: three bars that breathe while the track plays, rest when paused (the cover takes their place)
	if(!HasArt)
	{
		const float Time = Client()->GlobalTime();
		const float BarW = 2.6f, Gap = 1.6f;
		const float BaseY = Bars.y + Bars.h * 0.5f + 6.0f;
		Graphics()->TextureClear();
		for(int i = 0; i < 3; i++)
		{
			float Level = 0.25f;
			if(Media.m_Playing)
				Level = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(Time * (5.3f + i * 1.7f) + i * 2.1f)) * std::max(Intensity, 0.15f);
			const float H = 12.0f * Level;
			Graphics()->DrawRect(Bars.x + 1.0f + i * (BarW + Gap), BaseY - H, BarW, H, ColorRGBA(0.86f, 0.89f, 0.94f, 0.85f * Alpha), IGraphics::CORNER_ALL, BarW / 2.0f);
		}
	}

	// title and artist
	CUIRect TitleRect, ArtistRect;
	Text.HSplitTop(Text.h * 0.55f, &TitleRect, &ArtistRect);
	SLabelProperties Props;
	Props.m_EllipsisAtEnd = true;
	Props.m_MaxWidth = TitleRect.w;
	const float Flash = m_TrackFlash * Intensity;
	TextRender()->TextColor(ColorRGBA(0.92f + 0.08f * Flash, 0.93f + 0.07f * Flash, 0.95f + 0.05f * Flash, Alpha));
	Ui()->DoLabel(&TitleRect, Media.m_Title.c_str(), 8.5f, TEXTALIGN_ML, Props);
	TextRender()->TextColor(ColorRGBA(0.62f, 0.66f, 0.72f, Alpha));
	Ui()->DoLabel(&ArtistRect, Media.m_Artist.c_str(), 7.0f, TEXTALIGN_ML, Props);

	// progress: hairline with a soft glowing head, time on the right
	const int64_t PosMs = Music.PositionMs();
	const int64_t LenMs = Media.m_LengthUs / 1000;
	char aPos[16], aLen[16], aTime[40];
	FormatTime(aPos, sizeof(aPos), PosMs);
	FormatTime(aLen, sizeof(aLen), LenMs);
	str_format(aTime, sizeof(aTime), "%s / %s", aPos, aLen);
	CUIRect Line, TimeLabel;
	Progress.VSplitRight(44.0f, &Line, &TimeLabel);
	TextRender()->TextColor(ColorRGBA(0.58f, 0.62f, 0.68f, Alpha));
	Ui()->DoLabel(&TimeLabel, aTime, 5.5f, TEXTALIGN_MR);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	const float Fraction = LenMs > 0 ? std::clamp((float)PosMs / (float)LenMs, 0.0f, 1.0f) : 0.0f;
	const float LineY = Line.y + Line.h * 0.5f;
	Graphics()->TextureClear();
	Graphics()->DrawRect(Line.x, LineY - 0.5f, Line.w - 4.0f, 1.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.14f * Alpha), IGraphics::CORNER_ALL, 0.5f);
	Graphics()->DrawRect(Line.x, LineY - 0.5f, (Line.w - 4.0f) * Fraction, 1.0f, ColorRGBA(0.88f, 0.91f, 0.96f, 0.85f * Alpha), IGraphics::CORNER_ALL, 0.5f);
	const float HeadX = Line.x + (Line.w - 4.0f) * Fraction;
	Graphics()->BlendAdditive();
	for(int i = 3; i >= 1; i--)
		Graphics()->DrawRect(HeadX - i * 1.6f, LineY - i * 1.6f, i * 3.2f, i * 3.2f, ColorRGBA(0.78f, 0.82f, 0.92f, 0.18f * Alpha / i), IGraphics::CORNER_ALL, i * 1.6f);
	Graphics()->BlendNormal();
}
