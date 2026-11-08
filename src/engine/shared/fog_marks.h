#ifndef ENGINE_SHARED_FOG_MARKS_H
#define ENGINE_SHARED_FOG_MARKS_H

#include <atomic>

// Fog: what the client marked in this frame's alpha channel, read by the Vulkan post pass.
//   FOG_MARK_WORLD: the world ended (alpha scaled down; everything after is interface)
//   FOG_MARK_BACKGROUND: the far background ended (alpha 0 there; nearer layers raise it)
enum
{
	FOG_MARK_WORLD = 1 << 0,
	FOG_MARK_BACKGROUND = 1 << 1,
};
inline std::atomic<int> g_FogFrameMarks{0};

#endif
