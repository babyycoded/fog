#ifndef GAME_MAP_FOG_TINT_H
#define GAME_MAP_FOG_TINT_H

#include <base/color.h>

#include <algorithm>
#include <cstdint>

// Fog client: pulls the whole game map (textures, layer colours, quad colours) into a dark,
// cool monochrome. The game client sets the strength before it loads and draws map layers;
// tools and the editor leave it at 0, so they see the original colours.
namespace FogMapTint {

inline float g_Strength = 0.0f; // 0 = original colours, 1 = full fog
inline float g_Brightness = 1.0f; // how light the fogged world is

// colour channels in 0..1
inline void Apply(float &r, float &g, float &b)
{
	if(g_Strength <= 0.0f)
		return;
	const float Luma = 0.299f * r + 0.587f * g + 0.114f * b;
	// lifted shadows, softened highlights and a slight blue cast: the look of things seen through mist
	const float Fogged = (0.05f + Luma * 0.88f) * g_Brightness;
	const float TargetR = Fogged * 0.94f, TargetG = Fogged * 0.98f, TargetB = Fogged * 1.05f;
	r = std::clamp(r + (TargetR - r) * g_Strength, 0.0f, 1.0f);
	g = std::clamp(g + (TargetG - g) * g_Strength, 0.0f, 1.0f);
	b = std::clamp(b + (TargetB - b) * g_Strength, 0.0f, 1.0f);
}

inline ColorRGBA Apply(ColorRGBA Color)
{
	Apply(Color.r, Color.g, Color.b);
	return Color;
}

// for colour multipliers (envelopes): drain the colour but keep the brightness,
// so animated tints still pulse without darkening the already fogged world twice
inline ColorRGBA ApplyMultiplier(ColorRGBA Color)
{
	if(g_Strength <= 0.0f)
		return Color;
	const float Luma = 0.299f * Color.r + 0.587f * Color.g + 0.114f * Color.b;
	Color.r += (Luma - Color.r) * g_Strength;
	Color.g += (Luma - Color.g) * g_Strength;
	Color.b += (Luma - Color.b) * g_Strength;
	return Color;
}

inline void ApplyBytes(uint8_t &r, uint8_t &g, uint8_t &b)
{
	float fr = r / 255.0f, fg = g / 255.0f, fb = b / 255.0f;
	Apply(fr, fg, fb);
	r = (uint8_t)(fr * 255.0f + 0.5f);
	g = (uint8_t)(fg * 255.0f + 0.5f);
	b = (uint8_t)(fb * 255.0f + 0.5f);
}

inline void ApplyImage(uint8_t *pRgba, size_t NumPixels)
{
	if(g_Strength <= 0.0f)
		return;
	for(size_t p = 0; p < NumPixels; p++)
		ApplyBytes(pRgba[p * 4 + 0], pRgba[p * 4 + 1], pRgba[p * 4 + 2]);
}

} // namespace FogMapTint

#endif
