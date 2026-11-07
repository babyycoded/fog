#ifndef GAME_CLIENT_COMPONENTS_FOG_HIRES_TEE_H
#define GAME_CLIENT_COMPONENTS_FOG_HIRES_TEE_H

#include <game/client/skin.h>

#include <string>

class IGraphics;

// A big, sharp tishka for the menus: the skin PNG is upscaled on the CPU (bicubic with
// premultiplied alpha and a light unsharp mask) and cut into parts like the game does, so a
// 256x128 skin still looks clean at several hundred pixels.
class CImageInfo;

// skin image helpers shared with the tee studio
void FogUpscaleSkin(const CImageInfo &Src, int Factor, CImageInfo &Dst);
void FogMakeColorable(CImageInfo &Image); // grayscale + body weighting like custom colours

class CFogHiResTee
{
public:
	// Returns false when the skin file can't be read (then use the normal skin textures).
	bool Get(IGraphics *pGraphics, const char *pSkinName, CSkin::CSkinTextures &Original, CSkin::CSkinTextures &Colorable);
	void Unload(IGraphics *pGraphics);

private:
	std::string m_Name;
	bool m_Failed = false;
	CSkin::CSkinTextures m_Original;
	CSkin::CSkinTextures m_Colorable;
};

#endif
