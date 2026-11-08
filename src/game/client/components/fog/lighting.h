#ifndef GAME_CLIENT_COMPONENTS_FOG_LIGHTING_H
#define GAME_CLIENT_COMPONENTS_FOG_LIGHTING_H

#include <engine/graphics.h>

#include <game/client/component.h>

// Fog graphics that know the map (toggles in Visuals -> Graphics). The back instance draws behind
// the tees (ambient occlusion, light shafts, rim light); the front one draws over the map
// (dynamic lights with tile shadows, wall shadows, freeze shimmer, glow on light sources).
class CFogLighting : public CComponent
{
public:
	explicit CFogLighting(bool Front) :
		m_Front(Front) {}
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnRender() override;

private:
	bool m_Front;
	IGraphics::CTextureHandle m_Soft;
	void RenderAmbientOcclusion();
	void RenderGodRays();
	void RenderRimLight();
	void RenderLights();
	void RenderWallShadows();
	void RenderFreezeShimmer();
	void RenderSourceGlow();
};

#endif
