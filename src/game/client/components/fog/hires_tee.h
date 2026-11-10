#ifndef GAME_CLIENT_COMPONENTS_FOG_HIRES_TEE_H
#define GAME_CLIENT_COMPONENTS_FOG_HIRES_TEE_H

#include <game/client/skin.h>

#include <memory>
#include <string>

class IEngine;
class IGraphics;
class CFogHiResJob;

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
	// The skin is read and upscaled on a worker thread: while that runs it returns false too.
	bool Get(IGraphics *pGraphics, IEngine *pEngine, const char *pSkinName, CSkin::CSkinTextures &Original, CSkin::CSkinTextures &Colorable);
	void Unload(IGraphics *pGraphics);

private:
	std::string m_Name;
	bool m_Failed = false;
	CSkin::CSkinTextures m_Original;
	CSkin::CSkinTextures m_Colorable;
	std::shared_ptr<CFogHiResJob> m_pJob;
};

#endif
