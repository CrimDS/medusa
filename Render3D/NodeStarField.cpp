/*
	NodeStarField.cpp
	(c)2005 Palestar Inc, Richard Lyle

	See header for the design.  Summary: motes are stationary in world
	space inside a sphere around the camera; each frame we transform the
	mote's world position into view space ("head") and remember last
	frame's view-space position ("prev view") to use as the tail.  Camera
	motion (translation OR rotation) makes head differ from prev view, so
	the streak just emerges — no jump flag, no per-star doppler.
*/

#define RENDER3D_DLL
#include "Debug/Assert.h"
#include "Math/Helpers.h"
#include "Render3D/NodeStarField.h"
#include "Display/PrimitiveSetTransform.h"

//----------------------------------------------------------------------------

Color NodeStarField::s_StarColor( 255, 255, 255, 255 );
float NodeStarField::s_StarSize = 0.001f;
float NodeStarField::s_DopplerVelocity = 500.0f;	// legacy; unused

//----------------------------------------------------------------------------

IMPLEMENT_FACTORY( NodeStarField, BaseNode );
REGISTER_FACTORY_KEY(  NodeStarField, 4400228791173987159LL );

BEGIN_PROPERTY_LIST( NodeStarField, BaseNode )
	ADD_PROPERTY( m_ParticleCount );
	ADD_PROPERTY( m_Front );
	ADD_PROPERTY( m_Back );
END_PROPERTY_LIST();

//----------------------------------------------------------------------------

NodeStarField::NodeStarField()
	: m_bActive( true )
	, m_bTrailActive( true )
	, m_bJumpActive( false )
	, m_bHaveLastCamera( false )
{
	m_ParticleCount = 150;
	m_Front = 1.0f;
	m_Back = 500.0f;
}

//----------------------------------------------------------------------------

void NodeStarField::render( RenderContext &context,
		const Matrix33 & frame,
		const Vector3 & position )
{
	if (! m_bActive )
		return;

	DisplayDevice * pDisplay = context.display();
	ASSERT( pDisplay );

	if ( m_Particles.size() != m_ParticleCount )
	{
		int nPreviousCount = m_Particles.size();
		m_Particles.realloc( m_ParticleCount );
		for(int i=nPreviousCount;i<m_ParticleCount;i++)
			createParticle( context, i );
	}

	// Camera-teleport detection: if the camera moved more than the follow
	// radius in one frame, our cached prev-view positions are nonsense and
	// would draw scene-spanning streaks.  Reset the tails this frame.
	const Vector3 vCamPos = context.position();
	bool bCameraTeleported = false;
	if ( m_bHaveLastCamera )
	{
		Vector3 vCamDelta = vCamPos - m_vLastCameraPos;
		if ( vCamDelta.magnitude2() > (m_Back * m_Back) )
			bCameraTeleported = true;
	}
	m_vLastCameraPos = vCamPos;
	m_bHaveLastCamera = true;

	const Vector3 N(0,0,-1.0f);
	const float fBackSq = m_Back * m_Back;

	PrimitiveTriangleListDL::Ref pTriangleList =
		PrimitiveTriangleListDL::create( pDisplay, m_ParticleCount );

	VertexL * pVertex = (VertexL *)pTriangleList->lock();
	for(int i=0;i<m_Particles.size();i++)
	{
		Particle & p = m_Particles[ i ];

		// Respawn motes that have drifted outside the follow-shell.  We
		// re-spawn in the camera's leading hemisphere so motion through
		// space looks like the camera is entering fresh dust ahead.
		Vector3 vToParticle = p.m_vWorldPos - vCamPos;
		if ( vToParticle.magnitude2() > fBackSq )
		{
			Vector3 vsp( RandomFloat( -1.0f, 1.0f ),
			             RandomFloat( -1.0f, 1.0f ),
			             RandomFloat(  0.5f, 1.0f ) );	// forward-biased
			vsp.normalize();
			vsp *= RandomFloat( m_Front, m_Back );
			p.m_vWorldPos = context.viewToWorld( vsp );
			p.m_vPrevView = context.worldToView( p.m_vWorldPos );	// no spurious streak this frame
			p.m_fBrightness = RandomFloat( 0.4f, 1.0f );
		}

		Vector3 head( context.worldToView( p.m_vWorldPos ) );
		Vector3 tail( bCameraTeleported ? head : p.m_vPrevView );
		p.m_vPrevView = head;

		// Cull motes behind the near plane: write a degenerate triangle
		// at the origin so vertex count stays in lockstep with particle
		// count (the dynamic VB is allocated for m_ParticleCount tris).
		if ( head.z <= m_Front )
		{
			for(int v=0;v<3;v++)
			{
				pVertex->position = Vector3( 0, 0, 0 );
				pVertex->diffuse = Color( 0, 0, 0, 0 );
				pVertex->normal = N;
				pVertex->u = 0.0f;
				pVertex->v = 0.0f;
				pVertex++;
			}
			continue;
		}

		// Per-particle brightness modulates the base tint.  Alpha fades
		// with depth so motes wink out at the back of the shell instead
		// of clipping abruptly when they exit and respawn.
		Color color = s_StarColor;
		color.r = (u8)( color.r * p.m_fBrightness );
		color.g = (u8)( color.g * p.m_fBrightness );
		color.b = (u8)( color.b * p.m_fBrightness );
		float fAlpha = Clamp<float>( 1.0f - (head.z / m_Back), 0.0f, 1.0f );
		color.a = (u8)( 255.0f * fAlpha );

		// Size scales linearly with view-space depth so projected pixel
		// size stays roughly constant under perspective (s_StarSize is
		// a per-Z scale, not a world-space size).
		float size = s_StarSize * head.z;

		pVertex->position = head + Vector3( 0, size, 0 );
		pVertex->diffuse = color;
		pVertex->normal = N;
		pVertex->u = 0.0f;
		pVertex->v = 0.0f;
		pVertex++;

		pVertex->position = head + Vector3( size, 0, 0 );
		pVertex->diffuse = color;
		pVertex->normal = N;
		pVertex->u = 0.0f;
		pVertex->v = 0.0f;
		pVertex++;

		if ( m_bTrailActive )
		{
			// Streak vertex: previous-frame view-space position, faded
			// to black so additive Gouraud gives a tail that dims with
			// length.  Streak length scales naturally with camera speed.
			pVertex->position = tail;
			pVertex->diffuse = BLACK;
		}
		else
		{
			pVertex->position = head + Vector3( -size, 0, 0 );
			pVertex->diffuse = color;
		}
		pVertex->normal = N;
		pVertex->u = 0.0f;
		pVertex->v = 0.0f;
		pVertex++;
	}

	pTriangleList->unlock();

	// Submit in camera-local space: pushTransform(camFrame, camPos) makes
	// the world matrix equal to the camera-to-world transform, so vertices
	// authored in view-space coords transform back to view-space at draw
	// time and the field stays locked to the camera.
	context.push( PrimitiveMaterial::create( pDisplay, PrimitiveMaterial::ADDITIVE, false, true ) );
	context.pushTransform( context.frame(), context.position() );
	context.push( pTriangleList );

	BaseNode::render( context, frame, position );
}

//----------------------------------------------------------------------------

void NodeStarField::setActive( bool bActive )
{
	m_bActive = bActive;
	if (! bActive )
		m_bHaveLastCamera = false;	// next activation reseeds tails cleanly
}

void NodeStarField::setTrailActive( bool bTrailActive )
{
	m_bTrailActive = bTrailActive;
}

void NodeStarField::setJumpActive( bool bActive )
{
	// Streak length is now velocity-driven (jump = high cam velocity =
	// long streaks automatically), so this is a no-op for visuals.
	// Retained because GadgetJumpDrive still toggles it as a state flag.
	m_bJumpActive = bActive;
}

void NodeStarField::initialize( int particles, float front, float back )
{
	m_bActive = true;
	m_ParticleCount = particles;
	m_Front = front;
	m_Back = back;
}

void NodeStarField::setParticleCount( int particles )
{
	m_ParticleCount = particles;
}

//----------------------------------------------------------------------------

void NodeStarField::createParticle( RenderContext & context, int n )
{
	// Initial spawn: forward-biased direction in view space at a random
	// depth in the [front, back] shell, transformed to world space so the
	// mote is stationary in world thereafter.
	Vector3 vsp( RandomFloat( -1.0f, 1.0f ),
	             RandomFloat( -1.0f, 1.0f ),
	             RandomFloat(  0.5f, 1.0f ) );
	vsp.normalize();
	vsp *= RandomFloat( m_Front, m_Back );

	Particle & p = m_Particles[ n ];
	p.m_vWorldPos = context.viewToWorld( vsp );
	p.m_vPrevView = context.worldToView( p.m_vWorldPos );	// tail = head -> no streak frame 1
	p.m_fBrightness = RandomFloat( 0.4f, 1.0f );
}

//----------------------------------------------------------------------------
// EOF
