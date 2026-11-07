#ifndef GAME_CLIENT_COMPONENTS_FOG_ATMOSPHERE_H
#define GAME_CLIENT_COMPONENTS_FOG_ATMOSPHERE_H

#include <game/client/component.h>

// Fog client: real fog over the map. Banks of mist drift with the wind in world space at
// their own depths (parallax): the back layers sit behind the tiles, a thin front layer
// floats over everything and opens up around your own tee.
class CFogAtmosphere : public CComponent
{
public:
	explicit CFogAtmosphere(bool Front) :
		m_Front(Front) {}
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;

private:
	bool m_Front;
};

#endif
