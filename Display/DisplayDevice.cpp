/*
	DisplayDevice.cpp
	(c)2005 Palestar, Richard Lyle
*/

#define DISPLAY_DLL
#include "Display/DisplayDevice.h"
#include "Standard/Library.h"
#include "Factory/FactoryTypes.h"
#include "Debug/Trace.h"

//----------------------------------------------------------------------------

// Display options
bool					DisplayDevice::sm_bUseFixedFunction = false;					// if true, then the fixed function pipeline will always be used
bool					DisplayDevice::sm_bWaitVB = true;								// wait for verticle blank
bool					DisplayDevice::sm_bLightSecondaryPass = false;					// see header — default off, skip per-light render for SECONDARY pass
bool					DisplayDevice::sm_bResizeSuspended = false;						// platform sets this during interactive window drags
int						DisplayDevice::sm_nShaderDetail = DisplayDevice::SHADER_DETAIL_HIGH;	// shaderDetail config (LOW/MEDIUM/HIGH/EXTREME) — set in DarkSpaceClient init
dword					DisplayDevice::sm_nTrianglesRendered = 0;						// total number of triangles rendered via lists
dword					DisplayDevice::sm_nLinesRendered = 0;
DisplayDevice *			DisplayDevice::sm_pCacheDevice = NULL;							// device for used to precache graphics assets...

CharString				DisplayDevice::sm_sShadersPath;									// base directory of shader files

#if defined(_DEBUG)
bool					DisplayDevice::sm_bEnableShaderDebug = true;			
#else
bool					DisplayDevice::sm_bEnableShaderDebug = false;				
#endif

//-------------------------------------------------------------------------------

void LoadDisplayLibs()
{
#ifdef _DEBUG
	static Library		LIB_DISPLAYD3D( "DisplayD3DD.dll" );			// Direct3D
	static Library		LIB_DISPLAYD3D12( "DisplayD3D12D.dll" );	// Direct3D12 (debug)
	static Library		LIB_DISPLAYGL( "DisplayGLD.dll" );			// OpenGL
#else
	static Library		LIB_DISPLAYD3D( "DisplayD3D.dll" );			// Direct3D
	static Library		LIB_DISPLAYD3D12( "DisplayD3D12.dll" );	// Direct3D12 (release)
	static Library		LIB_DISPLAYGL( "DisplayGL.dll" );			// OpenGL
#endif
}

//----------------------------------------------------------------------------

IMPLEMENT_ABSTRACT_FACTORY( DisplayDevice, Widget );

DisplayDevice::DisplayDevice()
	: m_vSunWorldPos( Vector3::ZERO )
	, m_fSunRadius( 0.0f )
	, m_fSunDistSq( 3.4028235e+38f )	// FLT_MAX
	, m_bSunCandidateValid( false )
	, m_nStarCount( 0 )
	, m_nOccluderCount( 0 )
{}

//----------------------------------------------------------------------------
// Sun candidate tracking — see DisplayDevice.h comments.  Closest submission
// wins.  Note that m_bSunCandidateValid is STICKY across frames: once any
// sun has been submitted, it remains valid forever (until cross-scene
// transitions wipe state).  This is required because bindPerFrameCB runs
// BEFORE the scene's NounStar::render submits the current frame's sun, so
// shaders that consume m_vSunWorldPos at CB-fill time would otherwise see
// "no sun" every frame.  resetSunCandidate clears only the per-frame
// distance comparator so the closest-wins logic still works each frame.
// One frame of staleness is invisible for celestial-scale geometry.
//----------------------------------------------------------------------------

void DisplayDevice::submitSunCandidate( const Vector3 & worldPos, float radius, float distanceSq )
{
	// Closest-sun tracker (LimbGlow / CBPerFrame consumers).
	if ( distanceSq < m_fSunDistSq )
	{
		m_vSunWorldPos       = worldPos;
		m_fSunRadius         = radius;
		m_fSunDistSq         = distanceSq;
		m_bSunCandidateValid = true;
	}

	// Top-N list (DisplayEffectLensFlare consumer).  Append until full,
	// then replace the furthest entry whenever a closer one arrives.
	if ( m_nStarCount < MAX_STARS )
	{
		m_Stars[ m_nStarCount ].worldPos = worldPos;
		m_Stars[ m_nStarCount ].radius   = radius;
		m_Stars[ m_nStarCount ].distSq   = distanceSq;
		++m_nStarCount;
	}
	else
	{
		int   furthestIdx  = 0;
		float furthestDist = m_Stars[ 0 ].distSq;
		for ( int i = 1; i < MAX_STARS; ++i )
		{
			if ( m_Stars[ i ].distSq > furthestDist )
			{
				furthestDist = m_Stars[ i ].distSq;
				furthestIdx  = i;
			}
		}
		if ( distanceSq < furthestDist )
		{
			m_Stars[ furthestIdx ].worldPos = worldPos;
			m_Stars[ furthestIdx ].radius   = radius;
			m_Stars[ furthestIdx ].distSq   = distanceSq;
		}
	}
}

void DisplayDevice::resetSunCandidate()
{
	// Reset only the per-frame distance comparator.  m_vSunWorldPos and
	// m_bSunCandidateValid persist so consumers polling at bindPerFrameCB
	// time (before any NounStar::render runs) get last frame's sun rather
	// than zero / a stale flag.
	m_fSunDistSq = 3.4028235e+38f;	// FLT_MAX

	// Star list is consumed at postRender time (DisplayEffectLensFlare),
	// AFTER all NounStar::render calls have submitted, so it's safe to
	// rebuild from scratch each frame — no need for the stickiness the
	// closest-sun tracker has.
	m_nStarCount = 0;
}

bool DisplayDevice::getSunCandidate( Vector3 & outWorldPos ) const
{
	if ( !m_bSunCandidateValid )
		return false;
	outWorldPos = m_vSunWorldPos;
	return true;
}

bool DisplayDevice::getSunCandidate( Vector3 & outWorldPos, float & outRadius ) const
{
	if ( !m_bSunCandidateValid )
		return false;
	outWorldPos = m_vSunWorldPos;
	outRadius   = m_fSunRadius;
	return true;
}

int DisplayDevice::getStarCount() const
{
	return m_nStarCount;
}

const DisplayDevice::StarInfo & DisplayDevice::getStar( int idx ) const
{
	return m_Stars[ idx ];
}

//----------------------------------------------------------------------------
// Celestial occluder list — top-N by angular size (radius/distance) so the
// most visually-significant bodies always make it through.  Reset per-frame
// via RenderContext::beginScene, populated during scene render by
// NounPlanet::render etc.
//----------------------------------------------------------------------------

// Distance cutoff for occluder submission — bodies further than this from
// the camera don't go into the list.  At our typical FOV / view-Z ratios
// these bodies project to tangent discs <0.005 units which can't visually
// contribute a meaningful shadow column anyway, and they crowd out the
// near-camera bodies that actually matter.  Threshold is generous (covers
// any planet inside the current orbit cluster a player would see).
//
// Heuristic: angular radius (radius / distance) must exceed
// MIN_ANGULAR_SIZE.  At MIN_ANGULAR_SIZE = 0.002 (~0.1°), a r=644 body
// is admissible up to distance 322000 wu, while a r=43 moon is admissible
// only up to 21500 wu.  This naturally scales the cutoff with body size.
namespace {
	const float MIN_OCCLUDER_ANGULAR_SIZE = 0.002f;
}

void DisplayDevice::submitOccluder( const Vector3 & worldPos, float radius, float distanceToCamera, const char * pName )
{
	if ( radius <= 0.0f || distanceToCamera <= 0.0f )
		return;

	// Cull: too small to cast a visible shadow at this camera distance.
	// radius / distance is the angular radius in radians; below threshold
	// the body's tangent disc and shadow cylinder are sub-pixel and
	// invisible — but keeping it would consume a slot, evict a closer
	// body via priority sort, and add a no-op iteration to every shader
	// loop body.
	if ( radius < distanceToCamera * MIN_OCCLUDER_ANGULAR_SIZE )
		return;

	const float priority = radius / distanceToCamera;	// angular-size proxy

	// Deduplicate.  Same NounPlanet renders multiple times per frame
	// (once for the main scene, once per shadow cascade) and each call
	// re-submits with a slightly different `position` value because the
	// scene-graph traversal accumulates float error and the shadow pass
	// uses a different viewpoint origin.  worldPos compared exactly will
	// miss those near-equal entries — match by name pointer first (the
	// noun's m_Name buffer is a stable address per-noun, so a pointer
	// equality is a true identity check).  Fall back to worldPos+radius
	// for occluders submitted without a name.
	for ( int i = 0; i < m_nOccluderCount; ++i )
	{
		const bool sameName  = ( pName != NULL ) && ( m_Occluders[i].pName == pName );
		const bool samePosR  = ( m_Occluders[i].worldPos == worldPos &&
								 m_Occluders[i].radius   == radius );
		if ( sameName || samePosR )
		{
			// Already submitted this planet — keep the higher priority
			// (camera-relative typically wins over light-relative).
			if ( priority > m_OccluderPriority[i] )
				m_OccluderPriority[i] = priority;
			if ( pName && !m_Occluders[i].pName )
				m_Occluders[i].pName = pName;
			// Latch the most recent worldPos for the dominant-priority
			// submission — sub-unit drift is harmless but the latest
			// camera-relative one is most representative.
			if ( priority >= m_OccluderPriority[i] )
				m_Occluders[i].worldPos = worldPos;
			return;
		}
	}

	if ( m_nOccluderCount < MAX_OCCLUDERS )
	{
		// Free slot — append.
		const int idx = m_nOccluderCount++;
		m_Occluders[idx].worldPos  = worldPos;
		m_Occluders[idx].radius    = radius;
		m_Occluders[idx].pName     = pName;
		m_OccluderPriority[idx]    = priority;
		return;
	}

	// All slots full — find weakest and replace if this one outranks it.
	int weakest = 0;
	for ( int i = 1; i < MAX_OCCLUDERS; ++i )
		if ( m_OccluderPriority[i] < m_OccluderPriority[weakest] )
			weakest = i;

	if ( priority > m_OccluderPriority[weakest] )
	{
		m_Occluders[weakest].worldPos  = worldPos;
		m_Occluders[weakest].radius    = radius;
		m_Occluders[weakest].pName     = pName;
		m_OccluderPriority[weakest]    = priority;
	}
}

void DisplayDevice::resetOccluders()
{
	m_nOccluderCount = 0;
}

int DisplayDevice::getOccluderCount() const
{
	return m_nOccluderCount;
}

const DisplayDevice::OccluderInfo & DisplayDevice::getOccluder( int idx ) const
{
	return m_Occluders[idx];
}

//----------------------------------------------------------------------------

DisplayDevice * DisplayDevice::create( const char * pClass )
{
	// load the DLL's
	LoadDisplayLibs();
	// return the created object, or NULL on failure
	return WidgetCast<DisplayDevice>( Factory::createNamedWidget( pClass ) );
}

DisplayDevice * DisplayDevice::create()
{
	// load the DLL's
	LoadDisplayLibs();
	// if found, create the first available class type derived from this object
	if ( Factory::typeCount( classKey() ) > 0 )
		return WidgetCast<DisplayDevice>( Factory::createWidget( Factory::type( classKey(), 0 ) ) );

	return NULL;
}

const char * DisplayDevice::describeFSAA( FSAA eFSAA )
{
	static const char * TEXT[] = 
	{
		"FSAA_NONE",
		"FSAA_NONMASKABLE",
		"FSAA_2_SAMPLES",
		"FSAA_3_SAMPLES",
		"FSAA_4_SAMPLES",
		"FSAA_5_SAMPLES",
		"FSAA_6_SAMPLES",
		"FSAA_7_SAMPLES",
		"FSAA_8_SAMPLES",
		"FSAA_9_SAMPLES",
		"FSAA_10_SAMPLES",
		"FSAA_11_SAMPLES",
		"FSAA_12_SAMPLES",
		"FSAA_13_SAMPLES",
		"FSAA_14_SAMPLES",
		"FSAA_15_SAMPLES",
		"FSAA_16_SAMPLES",
	};

	return TEXT[ eFSAA ];
}

DisplayDevice::FSAA DisplayDevice::findFSAA( const char * pText )
{
	for(int i=0;i<FSAA_COUNT;++i)
	{
		if ( strcmp( describeFSAA( (FSAA)i ), pText ) == 0 )
			return (FSAA)i;
	}

	return FSAA_NONE;
}

//----------------------------------------------------------------------------
// EOF
