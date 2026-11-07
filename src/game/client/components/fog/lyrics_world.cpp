#include "lyrics_world.h"

#include "music.h"

#include <base/math.h>
#include <base/time.h>

#include <engine/graphics.h>
#include <engine/image.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/client/ui.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr float PI = 3.14159265358979f;

float Saturate(float x) { return std::clamp(x, 0.0f, 1.0f); }
float SmoothStep(float x)
{
	x = Saturate(x);
	return x * x * (3.0f - 2.0f * x);
}
float EaseOutCubic(float x)
{
	x = 1.0f - Saturate(x);
	return 1.0f - x * x * x;
}
vec2 Rotate(vec2 v, float Angle)
{
	const float c = std::cos(Angle), s = std::sin(Angle);
	return vec2(v.x * c - v.y * s, v.x * s + v.y * c);
}
// Stable pseudo random in [0,1) per glyph, so dissolving letters drift consistently.
float Hash01(int a, int b)
{
	unsigned h = (unsigned)a * 374761393u + (unsigned)b * 668265263u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return (float)((h ^ (h >> 16)) & 0xffff) / 65536.0f;
}
ColorRGBA ConfigColor(unsigned Packed)
{
	return color_cast<ColorRGBA>(ColorHSLA(Packed));
}
ColorRGBA MixColor(const ColorRGBA &a, const ColorRGBA &b, float t)
{
	return ColorRGBA(mix(a.r, b.r, t), mix(a.g, b.g, t), mix(a.b, b.b, t), mix(a.a, b.a, t));
}

} // namespace

float CFogLyricsWorld::AnimSpeed() const
{
	return g_Config.m_FogAnim ? g_Config.m_FogAnimSpeed / 100.0f : 1000.0f;
}

float CFogLyricsWorld::AnimIntensity() const
{
	return g_Config.m_FogAnim ? g_Config.m_FogAnimIntensity / 100.0f : 0.0f;
}

void CFogLyricsWorld::OnInit()
{
	// gaussian radial falloff: one smooth quad of mist, no visible steps or rings
	constexpr int SIZE = 128;
	std::vector<uint8_t> vPixels(SIZE * SIZE * 4);
	for(int y = 0; y < SIZE; y++)
		for(int x = 0; x < SIZE; x++)
		{
			const float dx = (x + 0.5f) / SIZE * 2.0f - 1.0f, dy = (y + 0.5f) / SIZE * 2.0f - 1.0f;
			const float r2 = dx * dx + dy * dy;
			const float v = r2 >= 1.0f ? 0.0f : std::exp(-r2 * 3.2f) * (1.0f - r2);
			uint8_t *p = &vPixels[(y * SIZE + x) * 4];
			p[0] = p[1] = p[2] = 255;
			p[3] = (uint8_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f);
		}
	CImageInfo Image;
	Image.m_Width = SIZE;
	Image.m_Height = SIZE;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.m_pData = vPixels.data();
	m_VeilTexture = Graphics()->LoadTextureRaw(Image, 0, "fog_veil");
	Image.m_pData = nullptr;
}

void CFogLyricsWorld::OnReset()
{
	m_AnchorValid = false;
}

void CFogLyricsWorld::OnWindowResize()
{
	ClearCache();
}

void CFogLyricsWorld::OnShutdown()
{
	ClearCache();
	if(m_VeilTexture.IsValid())
		Graphics()->UnloadTexture(&m_VeilTexture);
}

void CFogLyricsWorld::ReleaseLine(SLineCache &Line)
{
	for(SWordCache &Word : Line.m_vWords)
		TextRender()->DeleteTextContainer(Word.m_Container);
	Line.m_Glow.Unload(Graphics());
}

void CFogLyricsWorld::ClearCache()
{
	for(SLineCache &Line : m_vLineCache)
		ReleaseLine(Line);
	m_vLineCache.clear();
}

CFogLyricsWorld::SLineCache *CFogLyricsWorld::GetLine(int Key, const SLyricLine &Line, float FontSize)
{
	const float ScreenWidth = Graphics()->GetScreen().Width();
	for(SLineCache &Cache : m_vLineCache)
	{
		// rebuild when the zoom changed enough to make the glyph atlas blurry or wasteful
		if(Cache.m_Key == Key && Cache.m_FontSize == FontSize && std::abs(Cache.m_ScreenWidth / ScreenWidth - 1.0f) < 0.25f)
		{
			Cache.m_LastUsedFrame = m_Frame;
			return &Cache;
		}
	}

	if((int)m_vLineCache.size() >= MAX_CACHED_LINES)
	{
		auto Oldest = std::min_element(m_vLineCache.begin(), m_vLineCache.end(), [](const SLineCache &a, const SLineCache &b) { return a.m_LastUsedFrame < b.m_LastUsedFrame; });
		ReleaseLine(*Oldest);
		m_vLineCache.erase(Oldest);
	}
	// drop stale versions of the same line
	for(auto It = m_vLineCache.begin(); It != m_vLineCache.end();)
	{
		if(It->m_Key == Key)
		{
			ReleaseLine(*It);
			It = m_vLineCache.erase(It);
		}
		else
			++It;
	}

	SLineCache Cache;
	Cache.m_Key = Key;
	Cache.m_FontSize = FontSize;
	Cache.m_ScreenWidth = ScreenWidth;
	Cache.m_LastUsedFrame = m_Frame;
	TextRender()->SetDisplayFont(g_Config.m_FogLyricsFont, g_Config.m_FogLyricsWeight);
	TextRender()->SetFontPreset(EFontPreset::DISPLAY_FONT);
	const float RasterSize = FontSize * OVERSAMPLE;
	Cache.m_SpaceWidth = TextRender()->TextWidth(RasterSize, " ") / OVERSAMPLE * 1.05f;
	float X = 0.0f;
	for(const SLyricWord &Word : Line.m_vWords)
	{
		SWordCache WordCache;
		CTextCursor Cursor;
		Cursor.SetPosition(vec2(0.0f, 0.0f));
		Cursor.m_FontSize = RasterSize;
		TextRender()->CreateTextContainer(WordCache.m_Container, &Cursor, Word.m_Text.c_str());
		if(WordCache.m_Container.Valid())
		{
			TextRender()->GetTextContainerGlyphs(WordCache.m_Container, WordCache.m_vGlyphs);
			// back to drawing units; the atlas keeps the larger raster
			for(STextGlyph &Glyph : WordCache.m_vGlyphs)
			{
				Glyph.m_TopLeft /= OVERSAMPLE;
				Glyph.m_Size /= OVERSAMPLE;
			}
			WordCache.m_Width = TextRender()->GetBoundingBoxTextContainer(WordCache.m_Container).m_W / OVERSAMPLE;
		}
		X += WordCache.m_Width + Cache.m_SpaceWidth;
		Cache.m_vWords.push_back(std::move(WordCache));
	}
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

	float MinY = 1e9f, MaxY = -1e9f;
	for(const SWordCache &Word : Cache.m_vWords)
		for(const STextGlyph &Glyph : Word.m_vGlyphs)
		{
			MinY = std::min(MinY, Glyph.m_TopLeft.y);
			MaxY = std::max(MaxY, Glyph.m_TopLeft.y + Glyph.m_Size.y);
		}
	Cache.m_CenterY = MinY < MaxY ? (MinY + MaxY) * 0.5f : FontSize * 0.6f;
	Cache.m_Width = std::max(0.0f, X - Cache.m_SpaceWidth);

	std::vector<STextGlyph> vAllGlyphs;
	for(const SWordCache &Word : Cache.m_vWords)
		vAllGlyphs.insert(vAllGlyphs.end(), Word.m_vGlyphs.begin(), Word.m_vGlyphs.end());
	Cache.m_Glow.Build(Graphics(), TextRender(), vAllGlyphs, FontSize * 0.13f * g_Config.m_FogLyricsGlowSize / 100.0f);

	m_vLineCache.push_back(std::move(Cache));
	return &m_vLineCache.back();
}

void CFogLyricsWorld::UpdateAnchor(float Dt)
{
	vec2 Target = GameClient()->m_Camera.m_Center;
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId >= 0 && GameClient()->m_Snap.m_pLocalCharacter && !GameClient()->m_Snap.m_SpecInfo.m_Active)
		Target = GameClient()->m_aClients[LocalId].m_RenderPos;
	Target += vec2((float)g_Config.m_FogLyricsOffsetX, (float)g_Config.m_FogLyricsOffsetY);

	if(!m_AnchorValid || distance(m_Anchor, Target) > 1200.0f)
	{
		m_Anchor = Target;
		m_AnchorValid = true;
		return;
	}
	// critically damped follow: higher fog_lyrics_follow trails further behind
	const float Stiffness = mix(40.0f, 2.2f, g_Config.m_FogLyricsFollow / 100.0f);
	m_Anchor = mix(m_Anchor, Target, 1.0f - std::exp(-Dt * Stiffness));
}

void CFogLyricsWorld::OnRender()
{
	// screen karaoke always draws in the front pass, over the whole world
	if(g_Config.m_FogLyricsLayer == 0 && g_Config.m_FogLyricsMode != 2)
		Render(false);
}

void CFogLyricsWorld::RenderFrontPass()
{
	if(g_Config.m_FogLyricsLayer == 1 || g_Config.m_FogLyricsMode == 2)
		Render(true);
}

void CFogLyricsFront::OnRender()
{
	GameClient()->m_FogLyrics.RenderFrontPass();
}

void CFogLyricsWorld::Render(bool FrontPass)
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	m_Frame++;
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	const int64_t NowNs = time_get_nanoseconds().count();
	const CFogMusic &Music = GameClient()->m_FogMusic;

	char aFontKey[96];
	str_format(aFontKey, sizeof(aFontKey), "%s/%d/%d", g_Config.m_FogLyricsFont, g_Config.m_FogLyricsWeight, g_Config.m_FogLyricsGlowSize);
	const int FontKey = (int)str_quickhash(aFontKey);
	if(FontKey != m_CachedFontKey)
	{
		m_CachedFontKey = FontKey;
		ClearCache();
	}
	if(Music.Revision() != m_CachedRevision)
	{
		m_CachedRevision = Music.Revision();
		ClearCache();
		m_CurrentLine = -1;
		m_vTrail.clear();
		m_TrackChangeTimeNs = NowNs;

		// title card: shown in the world for a few seconds when a track without lyrics starts
		m_TitleLine = SLyricLine();
		if(Music.HasTrack())
		{
			std::string Text = Music.Media().m_Title;
			const char *p = Text.c_str();
			int64_t Cursor = 0;
			while(*p)
			{
				while(*p == ' ')
					p++;
				const char *pStart = p;
				while(*p && *p != ' ')
					p++;
				if(p > pStart)
				{
					SLyricWord Word;
					Word.m_Text.assign(pStart, p - pStart);
					Word.m_NumLetters = std::max(1, (int)Word.m_Text.size());
					Word.m_StartMs = Cursor;
					Word.m_EndMs = Cursor + 60 * Word.m_NumLetters + 120;
					Cursor = Word.m_EndMs;
					m_TitleLine.m_vWords.push_back(Word);
				}
			}
			m_TitleLine.m_EndMs = Cursor;
		}
	}

	const bool Enabled = g_Config.m_FogLyrics && Music.HasTrack();
	const bool HasLyrics = Music.LyricsStatus() == CFogMusic::ELyricsStatus::READY;
	const float TargetPresence = Enabled ? 1.0f : 0.0f;
	m_Presence = mix(m_Presence, TargetPresence, 1.0f - std::exp(-Dt * 4.0f * std::min(AnimSpeed(), 50.0f)));
	if(m_Presence < 0.005f)
	{
		m_AnchorValid = false;
		return;
	}

	UpdateAnchor(Dt);

	const int Mode = g_Config.m_FogLyricsMode;
	float FontSize;
	vec2 Origin;
	const CScreenRect WorldScreen = Graphics()->GetScreen();
	if(Mode == 2)
	{
		// karaoke in screen space: lower third, unaffected by the camera
		Ui()->MapScreen();
		const CUIRect *pScreen = Ui()->Screen();
		FontSize = 30.0f * (g_Config.m_FogLyricsSize / 100.0f);
		Origin = vec2(pScreen->w * 0.5f, pScreen->h * 0.74f);
	}
	else
	{
		const float Parallax = Mode == 1 ? 0.0f : g_Config.m_FogLyricsParallax / 100.0f;
		Origin = mix(m_Anchor, GameClient()->m_Camera.m_Center, Parallax);
		// further away also means smaller
		FontSize = 46.0f * (g_Config.m_FogLyricsSize / 100.0f) * (1.0f - Parallax * 0.35f);
	}

	const float Speed = AnimSpeed();

	if(HasLyrics)
	{
		const CLyrics &Lyrics = Music.Lyrics();
		const int64_t TimeMs = Music.LyricsTimeMs();
		const int Current = Lyrics.LineAt(TimeMs);
		if(Current != m_CurrentLine)
		{
			m_CurrentLine = Current;
			m_LineChangeTimeNs = NowNs;
		}
		if(Mode == 1)
		{
			RenderTrail(Current, TimeMs, NowNs, FontSize);
		}
		else
		{
			// 0 right after a line change, 1 when the slide into place has finished
			const float Transition = EaseOutCubic((NowNs - m_LineChangeTimeNs) / 1e9f * Speed / 0.55f);

			const auto &vLines = Lyrics.Lines();
			for(int i = Current + 2; i >= Current - 1; i--)
			{
				if(i < 0 || i >= (int)vLines.size())
					continue;
				const SLyricLine &Line = vLines[i];
				if(Line.m_vWords.empty())
					continue;
				// slot moves continuously: the next line slides up into place as it starts
				float Slot = (float)(i - Current) + (1.0f - Transition);
				if(i > Current && (!g_Config.m_FogLyricsNextLine || g_Config.m_FogLyricsStyle == 2))
					continue;
				if(i == Current)
				{
					// long instrumental gap: let the finished line sink into the fog
					const float Idle = SmoothStep((TimeMs - (Line.m_EndMs + 2500)) / 900.0f);
					const float Gap = (float)(Line.m_NextStartMs - Line.m_EndMs);
					if(Gap > 5000.0f)
						Slot -= Idle;
				}
				if(Slot > 2.0f || Slot < -1.0f)
					continue;
				SLineCache *pCache = GetLine(i, Line, FontSize);
				RenderLine(*pCache, Line, Slot, m_Presence, TimeMs, Origin, FontSize);
			}
		}
	}
	else if(!m_TitleLine.m_vWords.empty())
	{
		// no lyrics: the title rises out of the fog for a moment, then sinks back
		const float Age = (NowNs - m_TrackChangeTimeNs) / 1e9f;
		const float Shown = 5.0f;
		if(Age < Shown + 1.5f)
		{
			const float Enter = EaseOutCubic(Age * Speed / 0.8f);
			const float Leave = SmoothStep((Age - Shown) / 1.2f);
			const float Slot = (1.0f - Enter) - Leave;
			SLineCache *pCache = GetLine(TITLE_KEY, m_TitleLine, FontSize * 0.8f);
			RenderLine(*pCache, m_TitleLine, Slot, m_Presence, (int64_t)(Age * 1000.0f), Origin, FontSize * 0.8f);
		}
	}

	if(Mode == 2)
		Graphics()->MapScreen(WorldScreen);
}

void CFogLyricsWorld::RenderTrail(int Current, int64_t TimeMs, int64_t NowNs, float FontSize)
{
	const auto &vLines = GameClient()->m_FogMusic.Lyrics().Lines();

	// a new line is stamped where you are when it starts, then stays in the map
	if(Current >= 0 && Current < (int)vLines.size() && !vLines[Current].m_vWords.empty() &&
		(m_vTrail.empty() || m_vTrail.back().m_Line != Current))
	{
		vec2 Pos = m_Anchor;
		// keep lines from stacking when you stand still: step up until the spot is free
		for(int Try = 0; Try < MAX_TRAIL_LINES; Try++)
		{
			const bool Taken = std::any_of(m_vTrail.begin(), m_vTrail.end(), [&](const STrailLine &Other) {
				return std::abs(Other.m_Pos.y - Pos.y) < FontSize * 1.5f && std::abs(Other.m_Pos.x - Pos.x) < FontSize * 8.0f;
			});
			if(!Taken)
				break;
			Pos.y -= FontSize * 1.6f;
		}
		m_vTrail.push_back({Current, Pos, NowNs});
		if((int)m_vTrail.size() > MAX_TRAIL_LINES)
			m_vTrail.erase(m_vTrail.begin());
	}

	const float Speed = AnimSpeed();
	const float Hold = (float)g_Config.m_FogLyricsTrailTime;
	for(size_t t = 0; t < m_vTrail.size();)
	{
		const STrailLine &Trail = m_vTrail[t];
		if(Trail.m_Line >= (int)vLines.size())
		{
			m_vTrail.erase(m_vTrail.begin() + t);
			continue;
		}
		const SLyricLine &Line = vLines[Trail.m_Line];
		const float Age = (NowNs - Trail.m_StampNs) / 1e9f;
		// the stamp emerges from the fog, holds, then dissolves upward
		const float Enter = EaseOutCubic(Age * Speed / 0.45f);
		const float SungFor = std::max(0.0f, (TimeMs - Line.m_EndMs) / 1000.0f);
		const float Leave = Trail.m_Line == Current ? 0.0f : SmoothStep((SungFor - Hold) / 1.4f);
		if(Leave >= 1.0f)
		{
			m_vTrail.erase(m_vTrail.begin() + t);
			continue;
		}
		const float Slot = (1.0f - Enter) * 0.6f - Leave;
		SLineCache *pCache = GetLine(Trail.m_Line, Line, FontSize);
		RenderLine(*pCache, Line, Slot, m_Presence, TimeMs, Trail.m_Pos, FontSize);
		t++;
	}
}

void CFogLyricsWorld::RenderVeil(vec2 Center, float Width, float Height, float Alpha)
{
	if(Alpha <= 0.002f || !m_VeilTexture.IsValid())
		return;
	const ColorRGBA Veil = ConfigColor(g_Config.m_FogLyricsVeilColor);
	const float Tilt = g_Config.m_FogLyricsTilt * PI / 180.0f;
	Graphics()->TextureSet(m_VeilTexture);
	Graphics()->QuadsBegin();
	Graphics()->QuadsSetRotation(Tilt);
	// a wide faint bank of mist and a denser core right behind the words
	Graphics()->SetColor(Veil.WithAlpha(Alpha * 0.22f));
	IGraphics::CQuadItem Wide(Center, vec2(Width * 1.5f, Height * 3.2f));
	Graphics()->QuadsDraw(&Wide, 1);
	Graphics()->SetColor(Veil.WithAlpha(Alpha * 0.18f));
	IGraphics::CQuadItem Core(Center, vec2(Width * 1.1f, Height * 1.7f));
	Graphics()->QuadsDraw(&Core, 1);
	Graphics()->QuadsEnd();
	Graphics()->QuadsSetRotation(0.0f);
}

void CFogLyricsWorld::RenderLine(const SLineCache &Cache, const SLyricLine &Line, float Slot, float Fade, int64_t TimeMs, vec2 Origin, float FontSize)
{
	const float Intensity = AnimIntensity();
	const float Speed = AnimSpeed();
	const float Glow = g_Config.m_FogLyricsGlow / 100.0f;
	const int Extrude = g_Config.m_FogLyricsExtrude;
	const int HighlightMode = g_Config.m_FogLyricsHighlight;

	// slot properties: upcoming lines wait small and faint in the fog below,
	// finished lines rise, swell and dissolve
	const float Upcoming = Saturate(Slot);
	const float Gone = Saturate(-Slot);
	const float LineScale = mix(1.0f, 0.64f, Upcoming) * (1.0f + 0.18f * Gone);
	float LineAlpha = Fade * (Slot >= 0.0f ? mix(1.0f, 0.5f, Upcoming) * (1.0f - Saturate(Slot - 1.0f)) : 1.0f - SmoothStep(Gone));
	if(LineAlpha <= 0.003f)
		return;
	const vec2 LineOffset = vec2(FontSize * 0.25f * Slot, (Slot >= 0.0f ? FontSize * 1.45f * Slot : FontSize * 1.6f * Slot));

	const float Tilt = g_Config.m_FogLyricsTilt * PI / 180.0f;
	const float Bend = g_Config.m_FogLyricsCurve / 100.0f * 0.85f;
	const float Persp = g_Config.m_FogLyricsPerspective / 100.0f * 0.55f;
	const float W = std::max(Cache.m_Width, 1.0f);
	const float TimeSec = TimeMs / 1000.0f;

	const vec2 Center = Origin + Rotate(LineOffset, Tilt);
	RenderVeil(Center, W * LineScale, FontSize * LineScale * 1.3f, LineAlpha * g_Config.m_FogLyricsVeil / 100.0f * (1.0f - Upcoming * 0.5f));

	const ColorRGBA ColText = ConfigColor(g_Config.m_FogLyricsColor);
	const ColorRGBA ColGlow = ConfigColor(g_Config.m_FogLyricsGlowColor);
	const ColorRGBA ColDim = ConfigColor(g_Config.m_FogLyricsDimColor);
	const ColorRGBA ColExtrude = ConfigColor(g_Config.m_FogLyricsExtrudeColor);

	struct SGlyphState
	{
		STextGlyph m_Glyph;
		vec2 m_Pos;
		float m_Rot;
		float m_Scale;
		float m_Lit;
		float m_Active;
		int m_Index; // into the line's glow atlas
		float m_Alpha; // per-letter visibility from the animation style
	};
	std::vector<SGlyphState> vGlyphs;
	vGlyphs.reserve(64);

	float WordX = 0.0f;
	int GlyphSerial = 0;
	for(size_t w = 0; w < Cache.m_vWords.size() && w < Line.m_vWords.size(); w++)
	{
		const SWordCache &WordCache = Cache.m_vWords[w];
		const SLyricWord &Word = Line.m_vWords[w];
		const int NumGlyphs = (int)WordCache.m_vGlyphs.size();

		// a word "pops" with a damped overshoot the moment it starts
		const float WordAge = (TimeMs - Word.m_StartMs) / 1000.0f * Speed;
		const float Pop = (WordAge > 0.0f && WordAge < 0.6f) ? std::sin(WordAge / 0.6f * PI) * std::exp(-WordAge * 5.0f) * 0.35f * Intensity : 0.0f;

		for(int g = 0; g < NumGlyphs; g++, GlyphSerial++)
		{
			const STextGlyph &Glyph = WordCache.m_vGlyphs[g];
			const int Letter = std::min(g, Word.m_NumLetters - 1);
			const float Raw = CLyrics::LetterProgress(Word, Letter, TimeMs);
			float Lit;
			if(HighlightMode == 0)
				Lit = SmoothStep(CLyrics::WordProgress(Word, TimeMs) * 6.0f);
			else if(HighlightMode == 1)
				Lit = SmoothStep(Raw * 1.6f);
			else
				Lit = SmoothStep((CLyrics::WordProgress(Word, TimeMs) * Word.m_NumLetters - Letter + 1.2f) / 2.4f);
			const float Active = (Raw > 0.0f && Raw < 1.0f) ? std::sin(Raw * PI) : 0.0f;

			// position along the baseline, then perspective-warped: start of the line is nearer
			const float S = WordX + Glyph.m_TopLeft.x + Glyph.m_Size.x * 0.5f;
			const float SWarp = S - 0.5f * Persp * (S * S / W - S);
			const float U = 2.0f * S / W - 1.0f;
			const float PerspScale = 1.0f - 0.5f * Persp * U;
			const float GlyphY = (Glyph.m_TopLeft.y + Glyph.m_Size.y * 0.5f - Cache.m_CenterY) * PerspScale;

			// bend along a circular arc (arch for positive curve)
			const float A = SWarp - W * 0.5f;
			vec2 Pos;
			float Rot;
			if(std::abs(Bend) > 0.001f)
			{
				const float R = W / Bend;
				const float Phi = A / R;
				Pos = vec2(R * std::sin(Phi), R * (1.0f - std::cos(Phi)));
				Pos += vec2(-std::sin(Phi), std::cos(Phi)) * GlyphY;
				Rot = Phi;
			}
			else
			{
				Pos = vec2(A, GlyphY);
				Rot = 0.0f;
			}

			// letter animation style
			float Scale = PerspScale;
			float StyleAlpha = 1.0f;
			switch(g_Config.m_FogLyricsStyle)
			{
			case 1: // wave: a slow swell runs along the line, sung letters ride higher
				Pos.y -= std::sin(TimeSec * 3.2f * Speed - S / FontSize * 0.9f) * FontSize * 0.09f * Intensity * (0.4f + 0.6f * Lit);
				Pos.y -= FontSize * 0.08f * Active * Intensity;
				Scale *= 1.0f + 0.1f * Active * Intensity;
				break;
			case 2: // typewriter: letters appear only when they are sung
			{
				const float Appear = SmoothStep(Raw * 2.5f + (Lit > 0.99f ? 1.0f : 0.0f));
				StyleAlpha = Upcoming > 0.0f ? 0.0f : Appear;
				Scale *= mix(1.6f, 1.0f, EaseOutCubic(Appear)) * (1.0f + 0.1f * Active * Intensity);
				Pos.y -= FontSize * 0.25f * (1.0f - Appear) * Intensity;
				break;
			}
			case 3: // bounce: the sung letter jumps and lands with a little squash
			{
				const float Jump = Raw > 0.0f && Raw < 1.0f ? std::sin(Raw * PI) : 0.0f;
				const float LandAge = (TimeMs - Word.m_StartMs) / 1000.0f * Speed - (float)Letter / std::max(1, Word.m_NumLetters) * (Word.m_EndMs - Word.m_StartMs) / 1000.0f;
				const float Settle = LandAge > 0.0f && LandAge < 0.7f ? std::sin(LandAge * 14.0f) * std::exp(-LandAge * 7.0f) : 0.0f;
				Pos.y -= FontSize * 0.32f * Jump * Intensity;
				Scale *= 1.0f + (0.12f * Jump + 0.08f * Settle) * Intensity;
				break;
			}
			case 4: // calm: no motion at all, only light
				break;
			default: // glide: the sung letter lifts and swells, words pop in
				Pos.y -= FontSize * 0.14f * Active * Intensity;
				Scale *= 1.0f + 0.16f * Active * Intensity + Pop;
				break;
			}

			// dissolving lines: letters drift apart upward like breath in cold air
			if(Gone > 0.0f)
			{
				const float h1 = Hash01(GlyphSerial, Line.m_StartMs & 0xffff);
				const float h2 = Hash01(GlyphSerial + 911, (int)(Line.m_StartMs >> 3));
				Pos += vec2((h1 - 0.5f) * FontSize * 1.2f, -(0.4f + h2) * FontSize * 1.1f) * (Gone * Gone);
				Rot += (h1 - 0.5f) * 0.6f * Gone;
			}
			// gentle breathing so the line never feels pasted on
			if(g_Config.m_FogLyricsStyle != 4)
				Pos.y += std::sin(TimeSec * 1.3f + GlyphSerial * 0.45f) * FontSize * 0.025f * Intensity;

			SGlyphState State;
			State.m_Glyph = Glyph;
			State.m_Pos = Center + Rotate(Pos * LineScale, Tilt);
			State.m_Rot = Rot + Tilt;
			State.m_Scale = Scale * LineScale;
			State.m_Lit = Lit * (1.0f - Upcoming);
			State.m_Active = Active * (1.0f - Upcoming);
			State.m_Index = GlyphSerial;
			State.m_Alpha = StyleAlpha;
			vGlyphs.push_back(State);
		}
		WordX += WordCache.m_Width + Cache.m_SpaceWidth;
	}

	if(vGlyphs.empty())
		return;

	auto Emit = [&](bool Outline) {
		TextRender()->RenderGlyphs(m_vDraws.data(), (int)m_vDraws.size(), Outline);
		m_vDraws.clear();
	};
	auto Push = [&](const SGlyphState &State, vec2 Offset, float ScaleMul, ColorRGBA Color) {
		STextGlyphDraw Draw;
		Draw.m_Glyph = State.m_Glyph;
		Draw.m_Center = State.m_Pos + Offset;
		Draw.m_Rotation = State.m_Rot;
		Draw.m_Scale = State.m_Scale * ScaleMul;
		Draw.m_Color = Color.WithMultipliedAlpha(State.m_Alpha);
		m_vDraws.push_back(Draw);
	};
	// blurred glyphs from the line's glow atlas, drawn as regular textured quads
	const CFogGlowAtlas &GlowAtlas = Cache.m_Glow;
	auto DrawBlur = [&](vec2 Offset, float ScaleMul, auto &&ColorOf) {
		if(!GlowAtlas.Valid())
			return;
		Graphics()->TextureSet(GlowAtlas.Texture());
		Graphics()->QuadsBegin();
		for(const SGlyphState &State : vGlyphs)
		{
			const CFogGlowAtlas::SEntry &Entry = GlowAtlas.Entry(State.m_Index);
			const ColorRGBA Color = ColorOf(State).WithMultipliedAlpha(State.m_Alpha);
			if(!Entry.m_Valid || Color.a <= 0.002f)
				continue;
			Graphics()->SetColor(Color);
			Graphics()->QuadsSetSubset(Entry.m_aUVs[0], Entry.m_aUVs[1], Entry.m_aUVs[2], Entry.m_aUVs[3]);
			Graphics()->QuadsSetRotation(State.m_Rot);
			IGraphics::CQuadItem Quad(State.m_Pos + Offset, Entry.m_Size * State.m_Scale * ScaleMul);
			Graphics()->QuadsDraw(&Quad, 1);
		}
		Graphics()->QuadsEnd();
		Graphics()->QuadsSetRotation(0.0f);
	};

	const float Shadow = g_Config.m_FogLyricsShadow / 100.0f;
	const float Breath = 1.0f + 0.06f * std::sin(TimeSec * 2.1f) * Intensity;

	// 1. soft shadow: lifts the words off the map and keeps them readable on bright tiles
	if(Shadow > 0.0f)
	{
		const ColorRGBA ShadowColor = MixColor(ColExtrude, ColorRGBA(0, 0, 0, 1), 0.6f);
		DrawBlur(vec2(0.0f, FontSize * 0.09f * LineScale), 1.04f, [&](const SGlyphState &State) {
			return ShadowColor.WithAlpha(LineAlpha * Shadow * 0.85f * mix(0.6f, 1.0f, State.m_Lit));
		});
	}

	// 2. glow: a wide faint haze far from the letters, then a soft halo around them
	if(Glow > 0.0f)
	{
		// tinted veil with normal blending: the glow color still reads on bright maps,
		// where pure additive light would just clip to white
		DrawBlur(vec2(0, 0), 1.25f * Breath, [&](const SGlyphState &State) {
			return ColGlow.WithAlpha(LineAlpha * Glow * 0.30f * (State.m_Lit * 0.8f + State.m_Active * 0.4f));
		});
		Graphics()->BlendAdditive();
		DrawBlur(vec2(0, 0), 1.75f * Breath, [&](const SGlyphState &State) {
			return ColGlow.WithAlpha(LineAlpha * Glow * 0.5f * (State.m_Lit * 0.8f + State.m_Active * 0.5f));
		});
		DrawBlur(vec2(0, 0), 1.0f, [&](const SGlyphState &State) {
			return ColGlow.WithAlpha(LineAlpha * Glow * 0.95f * (State.m_Lit * 0.75f + State.m_Active * 0.6f));
		});
		Graphics()->BlendNormal();
	}

	// 3. extrusion: thin slabs in a darker shade of the letter, back to front
	if(Extrude > 0)
	{
		const vec2 Step = Rotate(vec2(0.25f, 1.0f), Tilt * 0.5f) * (FontSize * 0.016f * LineScale);
		for(int Layer = Extrude; Layer >= 1; Layer--)
		{
			const float Depth = Layer / (float)Extrude;
			for(const SGlyphState &State : vGlyphs)
			{
				const ColorRGBA Face = MixColor(ColDim, MixColor(ColText, ColGlow, 0.5f), State.m_Lit);
				ColorRGBA Col = MixColor(Face, ColExtrude, mix(0.45f, 0.8f, Depth));
				Col.a = LineAlpha * mix(0.9f, 0.6f, Depth) * mix(0.35f, 1.0f, State.m_Lit);
				Push(State, Step * (float)Layer, 1.0f, Col);
			}
		}
		Emit(false);
	}

	// 4. top light edge peeking above the face, like a bevel catching light
	for(const SGlyphState &State : vGlyphs)
		Push(State, Rotate(vec2(0.0f, -FontSize * 0.022f * State.m_Scale), State.m_Rot), 1.0f, ColorRGBA(1, 1, 1, LineAlpha * 0.4f * State.m_Lit));
	Emit(false);

	// 5. face: sung letters solid and bright, the rest a pale translucent promise
	for(const SGlyphState &State : vGlyphs)
	{
		ColorRGBA Col = MixColor(ColDim, ColText, State.m_Lit);
		Col = MixColor(Col, ColorRGBA(1, 1, 1, 1), State.m_Active * 0.45f);
		Col.a = LineAlpha * mix(0.55f, 1.0f, State.m_Lit);
		Push(State, vec2(0, 0), 1.0f, Col);
	}
	Emit(false);

	// 6. the letter being sung gets a quick inner shine
	if(Intensity > 0.0f)
	{
		Graphics()->BlendAdditive();
		for(const SGlyphState &State : vGlyphs)
			if(State.m_Active > 0.01f)
				Push(State, vec2(0, 0), 1.0f, ColGlow.WithAlpha(LineAlpha * 0.35f * State.m_Active * Intensity));
		Emit(false);
		Graphics()->BlendNormal();
	}
}
