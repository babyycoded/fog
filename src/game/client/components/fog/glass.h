#ifndef GAME_CLIENT_COMPONENTS_FOG_GLASS_H
#define GAME_CLIENT_COMPONENTS_FOG_GLASS_H

#include <base/color.h>

#include <engine/graphics.h>
#include <engine/textrender.h>

class CUIRect;
class CUi;

// Fog "liquid glass" panel: soft drop shadow, frosted tinted body, light rim and sheen.
// Alpha fades the whole panel (used by enter/leave animations).
void FogDrawGlass(IGraphics *pGraphics, const CUIRect &Rect, float Rounding, float Alpha = 1.0f);
ColorRGBA FogGlassTint();

void FogGlassInit(IGraphics *pGraphics);
// True while the Fog menu redesign is drawing (the glass hook is installed).
bool FogMenusActive();
// CUIRect::Draw hook that turns every menu rect into glass (fog_ui_menus).
bool FogMenuRectHook(const CUIRect &Rect, const ColorRGBA &Color, int Corners, float Rounding);
// Mist over the menu background map: a cold gradient and slow drifting fog banks.
void FogDrawMenuAtmosphere(IGraphics *pGraphics, const CUIRect &Screen, float Time);


// Fog motion: a damped spring that overshoots a little and settles (bouncy, never jittery).
// X/V are position and velocity; Freq in Hz; Bounce 0 = critically damped .. 0.6 = lively.
// Uses fog_anim, fog_anim_speed and fog_anim_intensity; with animation off it snaps.
void FogSpring(float &X, float &V, float Target, float Dt, float Freq = 5.0f, float Bounce = 0.35f);
// easeOutBack for one-shot entrances (0..1 -> 0..1 with a soft overshoot)
float FogEaseOutBack(float T);

// Album art: a soft shadow, the cover, and a thin frosted frame
void FogDrawAlbumArt(IGraphics *pGraphics, IGraphics::CTextureHandle Texture, const CUIRect &Rect, float Alpha);

// Fog type scale: labels in a display font family (fonts/display) at a given weight.
void FogLabel(CUi *pUi, ITextRender *pTextRender, const CUIRect &Rect, const char *pText, float Size, int Align, ColorRGBA Color, const char *pFamily = "Manrope", int Weight = 600);

#endif
