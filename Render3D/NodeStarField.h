/*
	NodeStarField.h
	(c)2005 Palestar Inc, Richard Lyle

	Near-field particle ("space dust") effect: small motes stationary in
	world space within a sphere around the camera.  Camera motion produces
	streaks automatically (head = current view-space position, tail =
	previous view-space position of the same world-space mote), so the
	effect needs no doppler / jump-state hack to look right at warp speeds.

	Class name retained for source compatibility — the legacy "starfield"
	terminology is a misnomer (parallax never worked at stellar distances)
	but it's wired in across DarkSpace by name.
*/

#ifndef NODESTARFIELD_H
#define NODESTARFIELD_H

#include "Render3D/BaseNode.h"
#include "Display/PrimitiveTriangleList.h"
#include "Render3D/Render3dDll.h"

//----------------------------------------------------------------------------

class DLL NodeStarField : public BaseNode
{
public:
	DECLARE_WIDGET_CLASS();
	DECLARE_PROPERTY_LIST();

	// Construction
	NodeStarField();

	// Node interface
    void				render( RenderContext &context,
							const Matrix33 & frame,
							const Vector3 & position );
	// Accessors
	bool				active() const;
	bool				isTrailActive() const;
	bool				isJumpActive() const;
	int					particleCount() const;
	float				front() const;			// inner radius of follow-shell
	float				back() const;			// outer radius of follow-shell

	// Mutators
	void				setActive( bool bActive );
	void				setTrailActive( bool bTrailActive );
	void				setJumpActive( bool bActive );	// kept for source compat (no-op now: streaks are velocity-driven)
	void				initialize(int particles, float front, float back );
	void				setParticleCount( int particles );

	// Static tuning knobs
	static Color		s_StarColor;			// per-particle base tint
	static float		s_StarSize;				// per-particle size scale (world units / view-z)
	static float		s_DopplerVelocity;		// legacy; unused (kept for source compat)

protected:
	// Types
	struct Particle
	{
		Vector3			m_vWorldPos;			// stationary in world (until respawn)
		Vector3			m_vPrevView;			// last-frame view-space position (drives trail)
		float			m_fBrightness;			// per-particle brightness 0.4..1.0
	};

	// Data
	int					m_ParticleCount;			// number of particles
	float				m_Front;
	float				m_Back;

	// non-serialized
	bool				m_bActive;
	bool				m_bTrailActive;
	bool				m_bJumpActive;
	Array< Particle >	m_Particles;
	Vector3				m_vLastCameraPos;
	bool				m_bHaveLastCamera;

	// Mutators
	void				createParticle( RenderContext & context, int n );
};

//----------------------------------------------------------------------------

inline bool NodeStarField::active() const
{
	return m_bActive;
}

inline bool NodeStarField::isTrailActive() const
{
	return m_bTrailActive;
}

inline bool NodeStarField::isJumpActive() const
{
	return m_bJumpActive;
}

inline int NodeStarField::particleCount() const
{
	return( m_ParticleCount );
}

inline float NodeStarField::front() const
{
	return( m_Front );
}

inline float NodeStarField::back() const
{
	return( m_Back );
}

//----------------------------------------------------------------------------



#endif

//----------------------------------------------------------------------------
// EOF
