#ifndef GAME_CLIENT_COMPONENTS_FOG_ACCESSORIES_H
#define GAME_CLIENT_COMPONENTS_FOG_ACCESSORIES_H

#include <base/vmath.h>

#include <engine/graphics.h>

#include <game/client/component.h>

#include "look.h"

#include <map>
#include <string>
#include <vector>

// One kind of accessory in the catalogue (art in data/fog/accessories/<id>.png)
struct SFogAccessoryDef
{
	enum
	{
		ANIM_NONE = 0,
		ANIM_SWAY, // swings with your movement and settles with a little bounce
		ANIM_BOB, // floats up and down
		ANIM_FLAP, // wings beat slowly
		ANIM_PULSE, // breathes in size
		ANIM_FLICKER, // glows and now and then flickers like a dying lamp
		ANIM_SMOKE, // an ember and smoke drifting up from the tip
		ANIM_TEARS, // drops run off and fall
		ANIM_RAIN, // rain falls from it onto your head
		ANIM_ORBIT, // circles around the head, in front and behind
	};
	const char *m_pId;
	const char *m_pName;
	float m_X, m_Y; // default place, in tee sizes from the body centre
	float m_Size; // default size, in tee sizes
	int m_Layer; // 0 behind the body, 1 in front
	int m_Anim;
	bool m_FollowEyes; // moves with the eyes (glasses)
	bool m_FlipWithLook; // mirrors when the tee looks left (cap brim)
	unsigned m_Color; // default colour, packed HSL
	unsigned m_GlowColor; // default glow colour, packed HSL
	float m_Glow; // glow strength: soft haze for shapes, strong for light sources
};

// One worn accessory
struct SFogAccessory
{
	int m_Def = 0;
	float m_X = 0.0f, m_Y = 0.0f, m_Size = 0.5f, m_Angle = 0.0f; // angle in degrees
	unsigned m_Color = 0;
	unsigned m_GlowColor = 0;
	bool m_Animate = true;
	bool m_Mirror = false;
};

// Fog tee studio: accessories on your own tee and dummy. They are stored in fog_tee_accessories
// / fog_dummy_accessories and drawn by CRenderTools::RenderTee for tees with m_FogAccessorySlot.
// Only this client draws them.
class CFogAccessories : public CComponent
{
public:
	enum
	{
		MAX_WORN = 8,
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnRender() override;
	void OnShutdown() override;

	static const SFogAccessoryDef *Catalogue(int *pNum);
	IGraphics::CTextureHandle Texture(int Def) const;

	// Slot 0 is you, 1 your dummy. A synced dummy wears your set (inverted colours when asked).
	// Returns -1 when the tee wears nothing, so callers can leave m_FogAccessorySlot unset.
	int SlotFor(int Slot) const;
	// The editable list of a slot (the synced dummy edits yours). Save() writes it back.
	std::vector<SFogAccessory> &Worn(int Slot);
	void Save(int Slot);
	SFogAccessory Make(int Def, float X, float Y) const;
	// a catalogue picture: glow and silhouette in the default colours
	void DrawIcon(int Def, vec2 Center, float Size, float Alpha);

	// a white silhouette of any player's tee (rim light, effects); false while the skin is missing
	bool DrawPlayerSilhouette(int ClientId, vec2 Pos, float Size, vec2 Shift, ColorRGBA Color);
	// other Fog players: their look from the presence server; slot = REMOTE_SLOT + client id
	enum
	{
		REMOTE_SLOT = 2,
	};
	int SetRemote(int ClientId, const SFogLook &Look); // returns the slot to draw, or -1
	// where a slot's tee was last drawn (for picking in the studio)
	vec2 LastBodyPos(int Slot) const { return m_aLast[Slot].m_BodyPos; }
	float LastBaseSize(int Slot) const { return m_aLast[Slot].m_BaseSize; }
	// the screen rect of an accessory as last drawn
	bool LastRect(int Slot, int Index, vec2 &Center, float &Size) const;

private:
	static void RenderHook(void *pUser, int Slot, int Layer, const class CAnimState *pAnim, vec2 Pos, vec2 BodyPos, float BaseSize, float BodyAngle, vec2 Dir, float Alpha);
	// auras and the coloured outline, behind the tee
	void RenderAura(int Slot, const class CAnimState *pAnim, vec2 Pos, vec2 BodyPos, float BaseSize, float BodyAngle, vec2 Dir, float Alpha);
	bool LoadSilhouette(int Slot);
	void DrawSilhouette(int Slot, const class CAnimState *pAnim, vec2 Pos, vec2 BodyPos, float BaseSize, float BodyAngle, float Scale, vec2 Shift, ColorRGBA Color);
	struct SSilhouette
	{
		std::string m_Skin;
		bool m_Failed = false;
		IGraphics::CTextureHandle m_Body, m_Feet;
	};
	std::map<int, SSilhouette> m_Silhouettes;
	IGraphics::CTextureHandle m_RingTexture;
	void Render(int Slot, int Layer, vec2 BodyPos, float BaseSize, float BodyAngle, vec2 Dir, float Alpha);
	void Parse(int Slot);
	char *ConfigString(int Slot) const;

	std::vector<IGraphics::CTextureHandle> m_vTextures;
	std::vector<IGraphics::CTextureHandle> m_vGlowTextures;
	IGraphics::CTextureHandle m_PuffTexture;
	IGraphics::CTextureHandle m_DropTexture;
	void DrawSprite(IGraphics::CTextureHandle Texture, vec2 Center, float W, float H, float Angle, bool Flip, ColorRGBA Color);
	// the glow (additive, its own colour) and then the silhouette
	void DrawPiece(int Def, vec2 Center, float W, float H, float Angle, bool Flip, ColorRGBA Color, ColorRGBA Glow);
	std::vector<SFogAccessory> m_avWorn[2];
	struct SRemote
	{
		SFogLook m_Look;
		std::string m_Parsed;
		std::vector<SFogAccessory> m_vWorn;
		float m_LastUsed = 0.0f;
	};
	std::map<int, SRemote> m_Remote;
	void ParseList(const char *pStr, std::vector<SFogAccessory> &vOut) const;
	const std::vector<SFogAccessory> &WornFor(int Slot);
	SFogLook LookFor(int Slot) const;
	std::string m_aParsed[2];
	bool m_aParsedOnce[2] = {false, false};

	struct SMotion
	{
		vec2 m_LastPos = vec2(0.0f, 0.0f);
		vec2 m_Vel = vec2(0.0f, 0.0f);
		float m_LastTime = -1.0f;
		float m_aAngle[MAX_WORN] = {};
		float m_aAngleVel[MAX_WORN] = {};
	};
	std::map<int, SMotion> m_Motion;

	struct SLast
	{
		vec2 m_BodyPos = vec2(0.0f, 0.0f);
		float m_BaseSize = 0.0f;
		vec2 m_aCenter[MAX_WORN];
		float m_aSize[MAX_WORN] = {};
		int m_Num = 0;
	};
	SLast m_aLast[2];
};

#endif
