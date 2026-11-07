#ifndef GAME_CLIENT_COMPONENTS_FOG_GLOW_ATLAS_H
#define GAME_CLIENT_COMPONENTS_FOG_GLOW_ATLAS_H

#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/textrender.h>

#include <vector>

// Soft light for text: each glyph's coverage mask is Gaussian-blurred on the CPU and packed into
// one RGBA texture (white, blur in alpha). Drawn additively it gives a real bloom that falls off
// smoothly away from the letters, instead of a swollen copy of the glyph.
class CFogGlowAtlas
{
public:
	struct SEntry
	{
		float m_aUVs[4] = {0, 0, 0, 0}; // normalized u0, v0, u1, v1
		vec2 m_Size = vec2(0, 0); // quad size in drawing units, centered on the glyph
		bool m_Valid = false;
	};

	// Glyphs are in drawing units (their size maps to their atlas texels). BlurRadius is the
	// Gaussian sigma in drawing units. Returns one entry per input glyph.
	void Build(IGraphics *pGraphics, ITextRender *pTextRender, const std::vector<STextGlyph> &vGlyphs, float BlurRadius);
	void Unload(IGraphics *pGraphics);

	IGraphics::CTextureHandle Texture() const { return m_Texture; }
	const SEntry &Entry(int Index) const { return m_vEntries[Index]; }
	bool Valid() const { return m_Texture.IsValid(); }

private:
	IGraphics::CTextureHandle m_Texture;
	std::vector<SEntry> m_vEntries;
};

#endif
