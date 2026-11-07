#ifndef GAME_CLIENT_COMPONENTS_FOG_PAINTER_H
#define GAME_CLIENT_COMPONENTS_FOG_PAINTER_H

#include <base/color.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/image.h>

#include <game/client/component.h>
#include <game/client/skin.h>

#include <cstdint>
#include <vector>

// Fog tee studio: draw your own tee. You paint the body and the feet of a standard 256x128
// skin; the dark outline is grown from your shapes, the classic soft shading of the default
// tee can be laid over the body, hands take the body's colour and the eyes come from the
// skin you started from. The result is saved to skins/<name>.png like any other skin.
class CFogTeePainter : public CComponent
{
public:
	enum
	{
		PART_BODY = 0,
		PART_FEET,
		NUM_PARTS,
	};
	enum
	{
		TOOL_BRUSH = 0,
		TOOL_ERASER,
		TOOL_FILL,
		TOOL_PICKER,
		NUM_TOOLS,
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnShutdown() override;

	// settings the studio edits directly
	int m_Tool = TOOL_BRUSH;
	int m_Size = 6; // brush diameter in skin pixels
	int m_Hardness = 70; // percent
	int m_Opacity = 100; // percent
	unsigned m_Color = 0; // packed HSL
	int m_Mirror = 1; // paint both halves at once
	int m_LockShape = 1; // paint only inside the tee's shape
	int m_Shading = 1; // lay the default tee's soft shading over the body
	int m_Outline = 3; // outline thickness in skin pixels
	int m_Part = PART_BODY;

	bool Started() const { return m_Started; }
	void Start(const char *pSkinName); // first open: from your current skin
	void NewBlank(); // a plain grey tee to paint on
	void Import(const char *pSkinName); // body, feet and eyes from a skin

	int Width(int Part) const;
	int Height(int Part) const;
	// paint in part pixel coordinates (floats, 0..Width)
	void BeginStroke();
	void Stroke(vec2 From, vec2 To);
	void EndStroke();
	void Fill(vec2 At);
	void Pick(vec2 At);
	void ClearPart();
	bool CanUndo() const { return !m_vUndo.empty(); }
	bool CanRedo() const { return !m_vRedo.empty(); }
	void Undo();
	void Redo();

	// textures: the whole tee for previews, and a sharp (nearest) picture of a part for the canvas
	const CSkin::CSkinTextures &PreviewTextures();
	IGraphics::CTextureHandle CanvasTexture(int Part);
	void MarkDirty() { m_Dirty = true; }

	bool Save(const char *pName, char *pError, int ErrorSize);

private:
	struct SRect
	{
		int x, y, w, h;
	};
	SRect PartRect(int Part) const;
	SRect SpriteRect(int Sprite) const;
	bool LoadSkinImage(const char *pSkinName, CImageInfo &Out);
	void LoadTemplate();
	void Stamp(vec2 At);
	void StampOne(vec2 At);
	CImageInfo Compose() const;
	void Rebuild();
	void PushUndo();

	bool m_Started = false;
	bool m_Dirty = true;
	float m_LastRebuild = -1.0f;
	CImageInfo m_Work; // 256x128, what you painted (+ eyes from the start skin)
	std::vector<float> m_vBodyMask, m_vFeetMask; // the default tee's shapes (0..1)
	std::vector<float> m_vBodyShade; // the default tee's shading (1 = neutral)
	std::vector<std::vector<uint8_t>> m_vUndo, m_vRedo;
	std::vector<float> m_vStrokeCover; // coverage already laid in this stroke (no build-up)
	bool m_InStroke = false;

	CSkin::CSkinTextures m_Preview;
	IGraphics::CTextureHandle m_aCanvas[NUM_PARTS];
};

#endif
