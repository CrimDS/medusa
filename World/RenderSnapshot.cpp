/*
	RenderSnapshot.cpp
	(c)2026 Palestar
*/

#include "World/RenderSnapshot.h"
#include "World/WorldClient.h"		// for WorldClient::sm_bPipelinedSimRender — see setAssertSnapshotCoverage
#include "Standard/Time.h"			// Option 2: Time::ticks() for local-ship extrap dt
#include "Math/Quat.h"				// frame slerp (materialize + local-ship frame extrap)

#include <math.h>					// atan2f / sinf / cosf in extrapLocalShip

// Defined further down; declared here so extrapLocalShipFrame above the
// definition can reach it.  Numerically-guarded quaternion slerp with
// shortest-path negation and an nlerp fallback for near-identical inputs.
static Matrix33 slerpFrameSafe( const Matrix33 & a, const Matrix33 & b, float t );

//---------------------------------------------------------------------------------------------------

// Per-thread "rendering from snapshot" flag — see header comment.
static thread_local bool tl_bRenderingFromSnapshot = false;

bool isRenderingFromSnapshot()			{ return tl_bRenderingFromSnapshot; }
void setRenderingFromSnapshot( bool a_bOn ) { tl_bRenderingFromSnapshot = a_bOn; }

//---------------------------------------------------------------------------------------------------

// Phase D coverage-assertion flag — see header.  Gated internally on
// sm_bPipelinedSimRender so InterfaceContext can enable unconditionally; the
// macro is also a no-op in Release builds, so this static cost is debug-only.
static thread_local bool tl_bAssertSnapshotCoverage = false;

bool assertSnapshotCoverageEnabled()
{
	// In Debug builds, return the thread-local flag so SNAPSHOT_ASSERT_COVERED
	// (a debug-only macro) fires during audit passes and so the for-render
	// helpers' `return <safe-default>` paths still surface any unmigrated
	// snapshot reads as obviously-wrong HUD values during dev testing.
	//
	// In Release builds, ALWAYS return false.  The "safe defaults" the
	// helpers return on snapshot miss (0 for visibility → ship invisible,
	// 0 for energy → HUD dead, etc.) are strictly worse than falling
	// through to the live value.  For single-scalar reads (float, int,
	// dword) on x86 the "race" with sim is atomic — worst case is a
	// one-tick-stale value, which is invisible on HUD.  A degenerate
	// default is visible and wrong.  Returning false here makes every
	// `if ( assertSnapshotCoverageEnabled() ) return 0;` branch fall
	// through to `return pShip->visibility();` (or whichever live
	// accessor), so a snapshot miss degrades to one-tick-stale live
	// instead of an obvious visual bug.
	//
	// This change only matters when a snapshot miss actually happens.
	// In the normal case (ship in snapshot), `if ( idx >= 0 ) return
	// s.shipVisibility( idx );` fires first and never reaches the
	// assertion check.
#ifdef _DEBUG
	return tl_bAssertSnapshotCoverage;
#else
	return false;
#endif
}
void setAssertSnapshotCoverage( bool a_bOn )
{
	// Arm if either: sim is genuinely pipelined (race risk → must avoid
	// live), OR audit mode is on (no race, but we want assertions to
	// surface unmigrated reads without enabling the still-flaky sim thread).
	// Under the #ifdef _DEBUG gate above, this flag is only observed in
	// debug builds — storing it in release is harmless (no reader) but
	// keeps the setter/getter pair symmetric.
	tl_bAssertSnapshotCoverage = a_bOn &&
		( WorldClient::sm_bPipelinedSimRender || WorldClient::sm_bAssertSnapshotCoverage );
}

//---------------------------------------------------------------------------------------------------

RenderSnapshot::RenderSnapshot() :
	m_CameraPosition( true ),
	m_CameraFrame( Matrix33::IDENTITY ),
	m_Time( 0.0f ),
	m_Tick( 0 ),
	m_CaptureTicks( 0 )
{}

void RenderSnapshot::clear()
{
	// std::vector::clear keeps the allocated capacity.  After the first few
	// frames of combat the vectors stabilise at the max-objects-visible size
	// and capture becomes allocation-free.
	m_Keys.clear();
	m_Positions.clear();
	m_Frames.clear();
	m_WorldPositions.clear();
	m_WorldFrames.clear();
	m_Velocities.clear();
	m_NodeFlags.clear();
	m_ShipEnergy.clear();
	m_ShipMaxEnergy.clear();
	m_ShipDamage.clear();
	m_ShipMaxDamage.clear();
	m_ShipSignature.clear();
	m_ShipVelocity.clear();
	m_ShipMaxVelocity.clear();
	m_ShipView.clear();
	m_ShipVisibility.clear();
	m_ShipSensor.clear();
	m_ShipJumpHasGadget.clear();
	m_ShipJumpEngaged.clear();
	m_ShipJumping.clear();
	m_ShipJumpTime.clear();
	m_ShipCaptureTarget.clear();
	m_ShipFlags.clear();
	m_ShipOutOfCombat.clear();
	m_ShipOOCTimer.clear();
	m_ShipRank.clear();
	m_PlanetControl.clear();
	m_PlanetFlags.clear();
	m_Extrap.clear();
	m_KeyToIndex.clear();
}

void RenderSnapshot::addNoun( WidgetKey nKey,
		const Vector3 & vLocalPosition,
		const Matrix33 & mLocalFrame,
		const Vector3 & vWorldPosition,
		const Matrix33 & mWorldFrame,
		const Vector3 & vVelocity,
		dword nNodeFlags )
{
	const int nIndex = (int)m_Keys.size();
	m_Keys.push_back( nKey );
	m_Positions.push_back( vLocalPosition );
	m_Frames.push_back( mLocalFrame );
	m_WorldPositions.push_back( vWorldPosition );
	m_WorldFrames.push_back( mWorldFrame );
	m_Velocities.push_back( vVelocity );
	m_NodeFlags.push_back( nNodeFlags );
	// Default ship state — overwritten by NounShip::captureSnapshotState
	// for ship nouns; non-ship nouns keep these zeros.
	m_ShipEnergy.push_back( 0 );
	m_ShipMaxEnergy.push_back( 0 );
	m_ShipDamage.push_back( 0 );
	m_ShipMaxDamage.push_back( 0 );
	m_ShipSignature.push_back( 0.0f );
	m_ShipVelocity.push_back( 0.0f );
	m_ShipMaxVelocity.push_back( 0.0f );
	m_ShipView.push_back( 0.0f );
	m_ShipVisibility.push_back( 0.0f );
	m_ShipSensor.push_back( 0.0f );
	m_ShipJumpHasGadget.push_back( 0 );
	m_ShipJumpEngaged.push_back( 0 );
	m_ShipJumping.push_back( 0 );
	m_ShipJumpTime.push_back( 0 );
	m_ShipCaptureTarget.push_back( 0 );
	m_ShipFlags.push_back( 0 );
	m_ShipOutOfCombat.push_back( 0 );
	m_ShipOOCTimer.push_back( 0.0f );
	m_ShipRank.push_back( 0 );
	m_PlanetControl.push_back( 0.0f );
	m_PlanetFlags.push_back( 0 );
	m_Extrap.push_back( 0 );
	m_KeyToIndex[ nKey.m_Id ] = nIndex;
}

void RenderSnapshot::setShipState( int idx,
		int nEnergy, int nMaxEnergy,
		int nDamage, int nMaxDamage,
		float fSignature )
{
	m_ShipEnergy[ idx ]    = nEnergy;
	m_ShipMaxEnergy[ idx ] = nMaxEnergy;
	m_ShipDamage[ idx ]    = nDamage;
	m_ShipMaxDamage[ idx ] = nMaxDamage;
	m_ShipSignature[ idx ] = fSignature;
}

void RenderSnapshot::setShipMotion( int idx,
		float fVelocity, float fMaxVelocity,
		float fView, float fVisibility, float fSensor )
{
	m_ShipVelocity[ idx ]    = fVelocity;
	m_ShipMaxVelocity[ idx ] = fMaxVelocity;
	m_ShipView[ idx ]        = fView;
	m_ShipVisibility[ idx ]  = fVisibility;
	m_ShipSensor[ idx ]      = fSensor;
}

void RenderSnapshot::setNounDamage( int idx, int nDamage, int nMaxDamage )
{
	m_ShipDamage[ idx ]    = nDamage;
	m_ShipMaxDamage[ idx ] = nMaxDamage;
}

void RenderSnapshot::setShipJumpDrive( int idx,
		bool bHasGadget, bool bEngaged, bool bJumping, dword nJumpTime )
{
	m_ShipJumpHasGadget[ idx ] = bHasGadget ? 1 : 0;
	m_ShipJumpEngaged[ idx ]   = bEngaged   ? 1 : 0;
	m_ShipJumping[ idx ]       = bJumping   ? 1 : 0;
	m_ShipJumpTime[ idx ]      = nJumpTime;
}

void RenderSnapshot::setShipCaptureTarget( int idx, WidgetKey nKey )
{
	m_ShipCaptureTarget[ idx ] = nKey.m_Id;
}

void RenderSnapshot::setShipStatus( int idx,
		dword nFlags, bool bOutOfCombat, float fOOCTimer, int nRank )
{
	m_ShipFlags[ idx ]        = nFlags;
	m_ShipOutOfCombat[ idx ]  = bOutOfCombat ? 1 : 0;
	m_ShipOOCTimer[ idx ]     = fOOCTimer;
	m_ShipRank[ idx ]         = nRank;
}

void RenderSnapshot::setPlanetState( int idx, float fControl, dword nFlags )
{
	m_PlanetControl[ idx ] = fControl;
	m_PlanetFlags[ idx ]   = nFlags;
}

int RenderSnapshot::findIndex( const WidgetKey & nKey ) const
{
	std::unordered_map<qword, int>::const_iterator it = m_KeyToIndex.find( nKey.m_Id );
	if ( it == m_KeyToIndex.end() )
		return -1;
	return it->second;
}

void RenderSnapshot::rebuildKeyToIndex()
{
	m_KeyToIndex.clear();
	const int n = (int)m_Keys.size();
	for ( int i = 0; i < n; ++i )
		m_KeyToIndex[ m_Keys[i].m_Id ] = i;
}

void RenderSnapshot::extrapLocalShip( int idx, float fDt )
{
	// Match NounShipControl's horizontal-plane motion exactly:
	//   m_Position += Vector3( sin(h), 0, cos(h) ) * m_fVelocity * dt
	// worldFrame.k nearly equals (sin h, 0, cos h) for a yaw-only ship,
	// but with non-zero pitch (gravity dip) the K-axis picks up a Y
	// component that the sim dynamics doesn't apply — so recover the
	// pure heading via atan2 on the horizontal plane and use that.
	// Cheap (one atan2 + two trig per local-ship frame) and matches
	// the sim-side trajectory exactly.
	const float fV = m_ShipVelocity[ idx ];
	if ( fV == 0.0f )
		return;
	const Vector3 & k = m_WorldFrames[ idx ].k;
	const float fHeading = atan2f( k.x, k.z );
	const float fScale   = fV * fDt;
	const Vector3 d( sinf( fHeading ) * fScale, 0.0f, cosf( fHeading ) * fScale );
	m_Positions[ idx ]      += d;
	m_WorldPositions[ idx ] += d;
}

void RenderSnapshot::copyPoseFrom( const RenderSnapshot & src,
	int srcIdx, int dstIdx )
{
	m_Positions[ dstIdx ]      = src.m_Positions[ srcIdx ];
	m_WorldPositions[ dstIdx ] = src.m_WorldPositions[ srcIdx ];
	m_Frames[ dstIdx ]         = src.m_Frames[ srcIdx ];
	m_WorldFrames[ dstIdx ]    = src.m_WorldFrames[ srcIdx ];
	m_Velocities[ dstIdx ]     = src.m_Velocities[ srcIdx ];
}

void RenderSnapshot::copyBallisticPoseFrom( const RenderSnapshot & src,
	int srcIdx, int dstIdx )
{
	// Position+velocity only — frame is left untouched so the slerp'd
	// rotation from materialize survives.  Used by the projectile extrap
	// path where we want B's un-lagged position (then extrapBallistic
	// forward to "now") but the smooth slerp'd heading rather than B's
	// snapped one (smart projectiles re-aim per tick).
	m_Positions[ dstIdx ]      = src.m_Positions[ srcIdx ];
	m_WorldPositions[ dstIdx ] = src.m_WorldPositions[ srcIdx ];
	m_Velocities[ dstIdx ]     = src.m_Velocities[ srcIdx ];
}

void RenderSnapshot::setExtrap( int idx )
{
	m_Extrap[ idx ] = 1;
}

bool RenderSnapshot::extrap( int idx ) const
{
	return m_Extrap[ idx ] != 0;
}

void RenderSnapshot::extrapBallistic( int idx, float fDt )
{
	// Sim does `m_Position += m_vVelocity * dt` between tick boundaries with
	// velocity held constant within a tick (NounProjectile::simulate).
	// Reproducing that exactly here gives zero client/server divergence —
	// when the next snapshot publishes, the new pose already accounts for
	// the full inter-tick advance.  Apply to both local and world position
	// (zone-direct projectiles' velocity is world-aligned, and DarkSpace
	// zones are static, so the deltas match).
	const Vector3 d = m_Velocities[ idx ] * fDt;
	m_Positions[ idx ]      += d;
	m_WorldPositions[ idx ] += d;
}

void RenderSnapshot::extrapLocalShipFrame( int idx,
	const RenderSnapshot & A, const RenderSnapshot & B, float alpha )
{
	// Local ship frame extrap.  After the local-ship overlay has copied B's
	// pose, we override the frame with slerp(A_frame, B_frame, alpha) where
	// alpha = (now - A.captureTicks)/(B.captureTicks - A.captureTicks).  At
	// alpha=1.0 this reproduces B exactly (sanity check); at alpha=2.0 it
	// rotates one full sim-tick of angular delta beyond B in the A→B
	// direction — i.e. constant-angular-velocity extrap to "now".  Matches
	// the position extrap path so rotation and translation move together
	// with zero input lag.
	const int aIdx = A.findIndex( m_Keys[ idx ] );
	const int bIdx = B.findIndex( m_Keys[ idx ] );
	if ( aIdx < 0 || bIdx < 0 )
		return;					// A or B is missing the ship — leave frame as-is
	m_Frames[ idx ]      = slerpFrameSafe( A.m_Frames[ aIdx ],      B.m_Frames[ bIdx ],      alpha );
	m_WorldFrames[ idx ] = slerpFrameSafe( A.m_WorldFrames[ aIdx ], B.m_WorldFrames[ bIdx ], alpha );
}

// Slerp between two rotation matrices with the numerical guards the engine's
// own Quat::slerp lacks.  The previous "frame LERP off" decision was driven
// by the engine slerp's 1/sin(theta) blow-up when adjacent frames are nearly
// identical (acos(d)≈0 → sin≈0 → NaN), plus the component-wise matrix lerp
// + orthonormalize path which can produce zero-determinant matrices that
// render as invisible/translucent geometry.
//
// This implementation:
//   • Negates qb when dot(qa,qb) < 0 so we always take the short way around.
//   • Falls back to normalized-lerp when |dot| > 0.9995 (near-identical
//     case, where slerp's sin(theta) approaches zero).
//   • Supports t > 1 for extrap (constant-angular-velocity continuation
//     beyond B) — used by the local-ship frame extrap path.
static Matrix33 slerpFrameSafe( const Matrix33 & a, const Matrix33 & b, float t )
{
	Quat qa( a );
	Quat qb( b );
	float d = qa.dot( qb );
	if ( d < 0.0f ) { qb = -qb; d = -d; }

	Quat r;
	if ( d > 0.9995f )
	{
		// Near-identical frames — nlerp is locally accurate and numerically
		// stable.  For extrap (t outside [0,1]) on near-identical frames
		// the rotation per unit t is tiny anyway, so nlerp is fine here too.
		r = qa + ( qb - qa ) * t;
	}
	else
	{
		const float theta    = acosf( d );
		const float invSin   = 1.0f / sinf( theta );
		const float wa       = sinf( ( 1.0f - t ) * theta ) * invSin;
		const float wb       = sinf( t * theta ) * invSin;
		r = qa * wa + qb * wb;
	}
	r.normalize();		// defend against accumulated FP drift
	return r.getMatrix33();
}

void RenderSnapshot::materialize( const RenderSnapshot & A,
	const RenderSnapshot & B, float t )
{
	// Deep-copy B as the base — structural fields (key list, ship scalars,
	// flags, jump state, planet state) are taken wholesale from the newer
	// capture.  Pose data (positions, velocities, frames) is then overridden
	// by an A→B blend below.
	//
	// Frame interpolation is now ON (was off historically — the comment used
	// to warn against it because the engine's Quat::slerp NaNs on identical
	// adjacent frames, and a component-wise matrix lerp was making ships
	// translucent).  slerpFrameSafe above handles both pitfalls, so smart-
	// projectile heading re-aim and ship rotation render smoothly at the
	// snapshot rate instead of stepping at 20 Hz.
	*this = B;

	// Defensive: rebuild m_KeyToIndex from m_Keys after the copy.  With
	// `*this = B` alone, all subsequent findIndex(shipKey) calls were
	// returning -1 for ships that ARE present in m_Keys — driving every
	// for-render helper (shipVisibility, shipEnergy, …) into its
	// assertSnapshotCoverageEnabled() "return 0" safe-default path during
	// scene render, which made ships render at alpha 0 (invisible for
	// remotes, clamped to 0.25 see-through for the local ship via the
	// NounShip::preRender floor).  Initially observed only here in
	// materialize where the lerp-then-findIndex sequence trips MSVC's
	// DLL-exported-class unordered_map copy.  Now applied defensively to
	// every `m_Pinned = X` path in pinForFrame() too because the bug has
	// been observed regressing under different builds — cost is
	// O(nounCount), dwarfed by the assignment.
	rebuildKeyToIndex();

	const int nB = (int)m_Keys.size();
	for ( int i = 0; i < nB; ++i )
	{
		// Nouns absent from A keep B's values (no-lerp — zero lag for a
		// noun that just appeared).  findIndex is a hash lookup on A's
		// m_KeyToIndex.
		const int aIdx = A.findIndex( m_Keys[i] );
		if ( aIdx < 0 )
			continue;

		const float one_minus_t = 1.0f - t;
		m_Positions[i]      = A.m_Positions[aIdx]      * one_minus_t + m_Positions[i]      * t;
		m_WorldPositions[i] = A.m_WorldPositions[aIdx] * one_minus_t + m_WorldPositions[i] * t;
		m_Velocities[i]     = A.m_Velocities[aIdx]     * one_minus_t + m_Velocities[i]     * t;
		m_Frames[i]         = slerpFrameSafe( A.m_Frames[aIdx],      m_Frames[i],      t );
		m_WorldFrames[i]    = slerpFrameSafe( A.m_WorldFrames[aIdx], m_WorldFrames[i], t );
	}
}

//---------------------------------------------------------------------------------------------------

// Option 2 — local-ship key.  Zero until WorldClient registers self.
std::atomic<qword> RenderSnapshotRing::sm_LocalShipKey( 0 );

// Option 3 — default-on snapshot-pair interpolation, 50 ms playout delay.
bool  RenderSnapshotRing::sm_bRenderInterpolation  = true;
float RenderSnapshotRing::sm_fPlayoutDelaySeconds  = 0.05f;

void RenderSnapshotRing::setLocalShipKey( const WidgetKey & nKey )
{
	// Release-store so pinForFrame's acquire-load sees the write alongside
	// any slot content the sim thread published just before self-assignment.
	sm_LocalShipKey.store( nKey.m_Id, std::memory_order_release );
}

RenderSnapshotRing::RenderSnapshotRing() :
	m_nLatestPublished( -1 ),
	m_nWriteSlot( 0 ),
	m_nReaderPinMask( 0 )
{}

RenderSnapshot & RenderSnapshotRing::writeSlot()
{
	// Pick a slot that's neither in the reader's pin bitmask nor the most-
	// recently-published slot.  With 4 slots and at most 2 reader pins + 1
	// latest, at least 1 slot is always free.  Re-read the pin mask at the
	// END in case the reader pinned mid-selection; retry if so.  Bounded
	// retries — fallback uses the most recent m_nWriteSlot if the reader
	// keeps us racing.
	for ( int retry = 0; retry < 4; ++retry )
	{
		const int pinMask   = m_nReaderPinMask.load( std::memory_order_acquire );
		const int published = m_nLatestPublished.load( std::memory_order_acquire );

		int slot = m_nWriteSlot.load( std::memory_order_relaxed );
		for ( int tries = 0; tries < NUM_SLOTS; ++tries )
		{
			const bool bPinned = ( pinMask & ( 1 << slot ) ) != 0;
			if ( !bPinned && slot != published )
				break;
			slot = ( slot + 1 ) % NUM_SLOTS;
		}

		if ( m_nReaderPinMask.load( std::memory_order_acquire ) == pinMask )
		{
			m_nWriteSlot.store( slot, std::memory_order_relaxed );
			return m_Slots[ slot ];
		}
	}

	return m_Slots[ m_nWriteSlot.load( std::memory_order_relaxed ) ];
}

const RenderSnapshot & RenderSnapshotRing::pinForFrame()
{
	// Option 3 — snapshot-pair interpolation with playout delay.
	//
	// Target: wall-clock "now" minus sm_fPlayoutDelaySeconds (≈ 50 ms =
	// 1 sim tick).  The reader shows the world as it was that far ago
	// so interpolation always has a valid forward bracket.  Find the
	// two published slots that bracket the target, LERP them into
	// m_Pinned, then overlay the client-local ship's pose from the
	// NEWEST slot (+ Option 2 velocity-extrap forward to "now") so the
	// player's own ship has zero input lag while remote nouns ride the
	// delay-buffered smooth interpolation.
	//
	// The bracket pair is pinned via m_nReaderPinMask during the read so
	// sim's writeSlot() doesn't overwrite either under us.  After
	// materialize, m_Pinned is a self-contained copy — pins are released
	// immediately, not at end-of-frame.  releaseFrame() is a no-op under
	// this design but kept for API symmetry.
	const int latest = m_nLatestPublished.load( std::memory_order_acquire );
	if ( latest < 0 )
	{
		// No history yet (first frame after start-up).  Leave m_Pinned as
		// whatever it was; default-constructed it's empty and findIndex
		// returns -1 for every noun — callers fall through to live reads.
		return m_Pinned;
	}

	const int prev = ( latest - 1 + NUM_SLOTS ) % NUM_SLOTS;
	const int pinMask = ( 1 << latest ) | ( 1 << prev );
	// Release: sim's next writeSlot() must see these pins before picking a
	// slot, otherwise it could race us by writing to `prev` (or `latest`).
	m_nReaderPinMask.store( pinMask, std::memory_order_release );

	const RenderSnapshot & A = m_Slots[ prev ];
	const RenderSnapshot & B = m_Slots[ latest ];

	if ( !sm_bRenderInterpolation
		 || A.m_CaptureTicks == 0
		 || B.m_CaptureTicks == 0
		 || A.m_CaptureTicks >= B.m_CaptureTicks )
	{
		// Fallback: only one valid publish, or interpolation disabled, or
		// captureTicks got reordered (slot wrapped mid-race).  Use B as-is.
		m_Pinned = B;
		m_Pinned.rebuildKeyToIndex();	// defensive — see RenderSnapshot.cpp materialize() comment
	}
	else
	{
		const qword nTicksPerSec = Time::ticksPerSecond();
		const qword nNow         = Time::ticks();
		const qword nDelay       = qword( double( sm_fPlayoutDelaySeconds )
		                                  * double( nTicksPerSec ) );
		const qword nTarget      = nNow > nDelay ? nNow - nDelay : 0;

		if ( nTarget >= B.m_CaptureTicks )
		{
			// Target is at or past the newest publish — sim hasn't produced
			// a forward bracket yet (running slower than delay).  Use B
			// un-interpolated; Option 2's local-ship extrap below still
			// advances the player's own ship.
			m_Pinned = B;
			m_Pinned.rebuildKeyToIndex();	// defensive
		}
		else if ( nTarget <= A.m_CaptureTicks )
		{
			// Target is older than our history pair — shouldn't happen in
			// steady state, but can on initial warm-up.  Use A.
			m_Pinned = A;
			m_Pinned.rebuildKeyToIndex();	// defensive
		}
		else
		{
			const float alpha = float( double( nTarget - A.m_CaptureTicks )
			                         / double( B.m_CaptureTicks - A.m_CaptureTicks ) );
			m_Pinned.materialize( A, B, alpha );
		}
	}

	// Option 2 overlay — replace the local ship's pose with the NEWEST slot
	// plus velocity-forward extrap to "now", so the player's own ship has
	// zero visual input lag.  Remote nouns stay on the interpolated
	// trajectory (50 ms smoothed playout).  Source is always B (latest)
	// because its pose is the freshest authoritative ship state — if the
	// materialized m_Pinned was built from A+B at some alpha, we discard
	// the ship's interpolated position in favor of B's un-delayed value.
	// (When materialize fell through to `m_Pinned = B` on a fallback path,
	// the copy below is a no-op before extrap — also correct.)
	const qword nLocalKey = sm_LocalShipKey.load( std::memory_order_acquire );
	if ( nLocalKey != 0 && B.m_CaptureTicks != 0 )
	{
		const int pinnedIdx = m_Pinned.findIndex( WidgetKey( nLocalKey ) );
		const int bIdx      = B.findIndex( WidgetKey( nLocalKey ) );
		if ( pinnedIdx >= 0 && bIdx >= 0 )
		{

			m_Pinned.copyPoseFrom( B, bIdx, pinnedIdx );
			const qword nNow = Time::ticks();
			if ( nNow > B.m_CaptureTicks )
			{
				float fDt = float( double( nNow - B.m_CaptureTicks )
				                 / double( Time::ticksPerSecond() ) );
				if ( fDt > 0.05f ) fDt = 0.05f;
				if ( fDt > 0.0f )
					m_Pinned.extrapLocalShip( pinnedIdx, fDt );
			}

			// Rotation extrap: slerp A→B beyond B by (now - A.ticks)/(B - A).
			// At alpha=1.0 this is exactly B (un-lagged); at alpha=2.0 it's
			// one full sim-tick of angular continuation past B.  Clamped at
			// 2.0 so a sim stall doesn't fling the ship — when the next
			// publish arrives the new B clips back to authoritative.  This
			// is the rotational analogue of extrapLocalShip and is what
			// closes the "position smooth, rotation steps at 20 Hz" gap
			// that made the local ship feel less than fully smooth.
			if ( A.m_CaptureTicks != 0 && B.m_CaptureTicks > A.m_CaptureTicks )
			{
				const double frameSpan = double( B.m_CaptureTicks - A.m_CaptureTicks );
				const double sinceA    = double( nNow - A.m_CaptureTicks );
				float alpha = float( sinceA / frameSpan );
				if ( alpha < 0.0f ) alpha = 0.0f;
				if ( alpha > 2.0f ) alpha = 2.0f;
				m_Pinned.extrapLocalShipFrame( pinnedIdx, A, B, alpha );
			}

		}
	}

	// Ballistic-extrap overlay — projectiles (NounProjectile et al) opt in
	// via captureSnapshotState/setExtrap.  Same pattern as the local ship:
	// override the LERP'd pose with B's freshest value, then advance by the
	// noun's own velocity vector * fDt.  Closes the muzzle-gap on outgoing
	// projectiles (which used to render 50 ms behind the firing ship) and
	// keeps them in lock-step with sim — server still owns authoritative
	// position, but since `m_Position += m_vVelocity * dt` is exactly what
	// sim does between tick boundaries, the next published snapshot lands
	// where extrap already had us.  No client/server desync.
	if ( B.m_CaptureTicks != 0 )
	{
		const qword nNow = Time::ticks();
		if ( nNow > B.m_CaptureTicks )
		{
			float fDt = float( double( nNow - B.m_CaptureTicks )
			                 / double( Time::ticksPerSecond() ) );
			if ( fDt > 0.05f ) fDt = 0.05f;
			if ( fDt > 0.0f )
			{
				const int nCount = m_Pinned.nounCount();
				for ( int i = 0; i < nCount; ++i )
				{
					if ( !m_Pinned.extrap( i ) )
						continue;
					const int bIdx = B.findIndex( m_Pinned.nounKey( i ) );
					if ( bIdx < 0 )
						continue;   // present in pinned but not B — leave alone
					// Position-only copy from B (NOT copyPoseFrom — that
					// would clobber the slerp'd frame from materialize, and
					// smart projectiles re-aim per tick so we WANT the
					// smoothed heading, not B's snapped one).  Velocity
					// from B is fresher for the extrap step.
					m_Pinned.copyBallisticPoseFrom( B, bIdx, i );
					m_Pinned.extrapBallistic( i, fDt );
				}
			}
		}
	}

	// Release pins — source slots are free for sim to reuse.  m_Pinned is
	// self-contained and stays valid until the next pinForFrame call.
	m_nReaderPinMask.store( 0, std::memory_order_release );
	return m_Pinned;
}

void RenderSnapshotRing::releaseFrame()
{
	// No-op under the materialize-and-copy-out design: pinForFrame already
	// releases the source-slot pins.  Kept for API symmetry and as an
	// extension point should we later switch to ref-counted in-place reads.
}

const RenderSnapshot & RenderSnapshotRing::readSlot() const
{
	// Under the Option 3 materialize design, m_Pinned is the authoritative
	// per-frame view.  It's populated by pinForFrame; before the first
	// pinForFrame of the session m_Pinned is default-empty (nounCount == 0,
	// findIndex returns -1 for every key).  Callers that poll readSlot()
	// outside a pin/release pair see the most recently materialized view.
	return m_Pinned;
}

void RenderSnapshotRing::publish()
{
	// Release: all snapshot writes must complete before the index update
	// becomes visible.  Pairs with pinForFrame()'s acquire load and
	// readSlot()'s acquire fallback.
	m_nLatestPublished.store( m_nWriteSlot.load( std::memory_order_relaxed ),
							  std::memory_order_release );
}

RenderSnapshotRing & RenderSnapshotRing::instance()
{
	// Process-wide singleton.  Safe to leak on exit — the ring serves for
	// the entire lifetime of the client, and destroying it during teardown
	// could race with threads still holding a slot.
	static RenderSnapshotRing * s_pInstance = new RenderSnapshotRing();
	return *s_pInstance;
}

//---------------------------------------------------------------------------------------------------
//EOF
