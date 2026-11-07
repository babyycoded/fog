#ifndef GAME_CLIENT_COMPONENTS_FOG_CURSOR_H
#define GAME_CLIENT_COMPONENTS_FOG_CURSOR_H

#include <base/vmath.h>

class CGameClient;
class IGraphics;

// Fog crosshair at a world position (zoom 1 mapping). Speed in world units per second opens it up.
void FogDrawCrosshair(IGraphics *pGraphics, vec2 Pos, float Alpha, float Speed);
// Should the Fog crosshair replace the weapon cursor sprite of the current game skin?
bool FogUseCrosshair();
// The menu cursor; Pos is the hotspot in UI coordinates. Returns false to keep the game arrow.
bool FogDrawMenuCursor(CGameClient *pClient, IGraphics *pGraphics, vec2 Pos, float Size);

#endif
