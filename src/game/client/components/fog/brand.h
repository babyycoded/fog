#ifndef GAME_CLIENT_COMPONENTS_FOG_BRAND_H
#define GAME_CLIENT_COMPONENTS_FOG_BRAND_H

#include <base/color.h>
#include <base/vmath.h>

class IGraphics;
class ITextRender;
class CUi;
class CUIRect;

// Fog brand: palette, logo mark and the animated "liquid fog" menu background.
namespace FogBrand {

constexpr ColorRGBA INK(0.063f, 0.075f, 0.090f, 1.0f); // #101317
constexpr ColorRGBA SLATE(0.110f, 0.125f, 0.153f, 1.0f); // #1C2027
constexpr ColorRGBA MIST(0.655f, 0.686f, 0.729f, 1.0f); // #A7AFBA
constexpr ColorRGBA FROST(0.910f, 0.922f, 0.937f, 1.0f); // #E8EBEF
// muted iridescent tints the background drifts through
constexpr ColorRGBA IRIS_TEAL(0.62f, 0.72f, 0.79f, 1.0f);
constexpr ColorRGBA IRIS_LILAC(0.71f, 0.66f, 0.80f, 1.0f);
constexpr ColorRGBA IRIS_ROSE(0.78f, 0.69f, 0.72f, 1.0f);

void Init(IGraphics *pGraphics);
// The mark: a tee peeking out of the fog. Center and size in current screen units.
void DrawMark(IGraphics *pGraphics, vec2 Center, float Size, ColorRGBA Color, float Glow = 0.0f);
// Mark plus the "fog" wordmark, left aligned in Rect, vertically centered.
void DrawLockup(IGraphics *pGraphics, ITextRender *pTextRender, CUi *pUi, const CUIRect &Rect, float Height, float Alpha = 1.0f);
// Soft shapes from the brand textures: a round mist patch or a long mist streak.
void DrawMist(IGraphics *pGraphics, vec2 Center, vec2 Size, ColorRGBA Color, float Rotation = 0.0f);
void DrawStreak(IGraphics *pGraphics, vec2 Center, vec2 Size, ColorRGBA Color, float Rotation = 0.0f);
// Full-screen animated background for the menus.
void DrawBackground(IGraphics *pGraphics, const CUIRect &Screen, float Time);

} // namespace FogBrand

#endif
