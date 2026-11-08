#ifndef GAME_CLIENT_COMPONENTS_FOG_FX_H
#define GAME_CLIENT_COMPONENTS_FOG_FX_H

#include <base/vmath.h>

#include <game/client/component.h>

// 0..1 beat while music plays (fog_music_pulse); fog, glow and auras breathe with it
float FogMusicPulse();
// the soft camera's offset (fog_camera_lag)
vec2 FogCameraLag();

// Fog visuals and fun touches in the world, client side only. Two instances: the back one
// (behind the tees: reflections, shadows, footprints, dust) also runs the simulation; the
// front one draws weather, time of day, the flashlight, frost, thoughts, confetti, notes and
// the speed vignette.
class CFogFx : public CComponent
{
public:
	explicit CFogFx(bool Front) :
		m_Front(Front) {}
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnRender() override;
	void OnMapLoad() override;

	// a tee died here (from the death event)
	static void OnDeath(vec2 Pos);

private:
	bool m_Front;
	void Update(float Dt);
	void RenderBack();
	void RenderFront();
	void RenderReflections();
	void RenderShadows();
};

// Marks the end of the world in the frame (right before the HUD): motion blur stays on the
// world and never smears the interface
class CFogWorldMark : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
};

#endif
