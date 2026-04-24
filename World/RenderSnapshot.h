/*
	RenderSnapshot.h

	Immutable per-frame snapshot of simulation state that the render thread
	consumes.  Published by the sim thread at the end of each tick; read by
	the render thread for the duration of one frame.  This is the handoff
	point for the sim/render pipeline split — see Docs/Phase3_SimRenderSplit.md.

	Stage 3.1 (current): snapshot captures on the main thread after simulate,
	but is not yet consumed by render.  Serves as measurement + scaffolding
	for the full pipeline.

	Stage 3.2: render reads from snapshot instead of live state.
	Stage 3.3: sim runs on a dedicated thread, publishes snapshots to render.

	(c)2026 Palestar
*/

#ifndef RENDER_SNAPSHOT_H
#define RENDER_SNAPSHOT_H

#include "Standard/Types.h"
#include "Debug/Assert.h"
#include "Factory/WidgetKey.h"
#include "Math/Vector3.h"
#include "Math/Matrix33.h"
#include "WorldDll.h"

#include <atomic>
#include <vector>
#include <unordered_map>

//---------------------------------------------------------------------------------------------------

// Per-thread "rendering from snapshot" flag.  Wrapped in DLL functions
// because `thread_local` + `__declspec(dllexport)` is fragile on MSVC and
// the World/Gui3d DLL boundary needs a stable interface.  Render thread
// (InterfaceContext::render) sets true around the scene-render block; when
// set, Noun::calculateWorld() consults the snapshot instead of walking the
// live parent chain (which would race with sim-thread mutations on
// ancestors).  Sim/network threads never set this — they read live state.
DLL bool				isRenderingFromSnapshot();
DLL void				setRenderingFromSnapshot( bool a_bOn );

//---------------------------------------------------------------------------------------------------

// Phase D coverage assertion.  When sim is pipelined onto a separate thread
// (sm_bPipelinedSimRender == true), any render-side read of live world state
// is a race waiting to happen — the post-mortem on the first sim-thread
// attempt traced visible streak corruption to a long tail of unmigrated
// reads (HUD energy/hull, target pointers, gadget state, etc).
//
// The plan to close those: instrument render-side accessors of live state
// with SNAPSHOT_ASSERT_COVERED("description"), then run with the assertion
// flag on.  Any unmigrated read fires the assertion at file:line; we add a
// snapshot field to cover it, redirect the read to consult the snapshot,
// and move on.  Once no assertion fires under realistic play, sm_bPipelined-
// SimRender is safe to enable.
//
// The macro is debug-only (zero overhead in Release).  setAssertSnapshot-
// Coverage gates internally on sm_bPipelinedSimRender, so InterfaceContext
// can unconditionally enable it around scene render — it only actually arms
// when sim runs on a separate thread.  When sim is on the main thread the
// snapshot was captured microseconds before render, so live reads are race-
// free and the assertion would just spam.
DLL bool				assertSnapshotCoverageEnabled();
DLL void				setAssertSnapshotCoverage( bool a_bOn );

#ifdef _DEBUG
#define SNAPSHOT_ASSERT_COVERED( reason ) \
	do { if ( assertSnapshotCoverageEnabled() ) \
	     ASSERT_ERR( false, "RenderSnapshot coverage gap: " reason ); } while( 0 )
#else
#define SNAPSHOT_ASSERT_COVERED( reason ) ((void)0)
#endif

//---------------------------------------------------------------------------------------------------

// Per-noun compact snapshot data.  Parallel arrays indexed 0..nounCount-1 —
// cache-friendly for the render side's iteration.  Grown lazily via std::vector
// reserve, cleared (not freed) between frames so capacity stabilises after the
// first few frames of play.
class DLL RenderSnapshot
{
public:
	RenderSnapshot();

	// Reset the arrays without freeing underlying capacity.  Called by the
	// sim side at the start of each capture so we don't realloc every frame.
	void					clear();

	// Sim-side: append one noun's render-relevant state.  Both LOCAL and
	// WORLD transforms are captured: local for swap-and-restore in
	// Noun::preRender (so the render tree descent uses snapshot values),
	// world for direct lookups via Noun::calculateWorld()/worldPosition()/
	// worldFrame() that bypass the preRender chain (HUD code, camera math,
	// shadow focus, contact rendering).
	void					addNoun( WidgetKey nKey,
								const Vector3 & vLocalPosition,
								const Matrix33 & mLocalFrame,
								const Vector3 & vWorldPosition,
								const Matrix33 & mWorldFrame,
								const Vector3 & vVelocity,
								dword nNodeFlags );

	// Accessors for the render side.
	int						nounCount() const;
	WidgetKey				nounKey( int i ) const;
	const Vector3 &			position( int i ) const;		// local (zone-relative)
	const Matrix33 &		frame( int i ) const;			// local
	const Vector3 &			worldPosition( int i ) const;	// world space
	const Matrix33 &		worldFrame( int i ) const;		// world space
	const Vector3 &			velocity( int i ) const;
	dword					nodeFlags( int i ) const;

	// Stage 3.2 — O(1) lookup from WidgetKey back to snapshot index.
	// Returns -1 if the key isn't in this snapshot (noun was added after
	// capture, or wasn't captured because it's not in a locked zone).
	int						findIndex( const WidgetKey & nKey ) const;

	// Phase D — per-ship combat state.  Written by NounShip::captureSnapshot-
	// State (called from WorldContext::captureRenderSnapshot immediately after
	// addNoun).  Non-ship slots keep the default zeros pushed by addNoun.
	void					setShipState( int idx,
								int nEnergy, int nMaxEnergy,
								int nDamage, int nMaxDamage,
								float fSignature );

	// Phase D D.4 — per-ship motion + sensor scalars.  Same lifecycle as
	// setShipState (sim-side write after addNoun, render-side read by
	// shipVelocityForRender etc.).  Separate setter to keep argument lists
	// readable as the per-ship state grows.
	void					setShipMotion( int idx,
								float fVelocity, float fMaxVelocity,
								float fView, float fVisibility, float fSensor );

	// Phase D D.5 — per-noun damage.  Used by both NounShip (alongside
	// setShipState) and NounGadget.  Writes to the same m_ShipDamage /
	// m_ShipMaxDamage arrays — they're per-noun-slot, not ship-specific.
	void					setNounDamage( int idx,
								int nDamage, int nMaxDamage );

	// Phase D D.6 — jump drive chain + capture target.  jumpDrive() can be
	// NULL (no gadget); bJumpHasGadget mirrors that.  Capture target is
	// stored as a WidgetKey so the render side can resolve via findIndex/
	// findNoun without dereferencing a sim-mutable pointer.
	void					setShipJumpDrive( int idx,
								bool bHasGadget, bool bEngaged, bool bJumping,
								dword nJumpTime );
	void					setShipCaptureTarget( int idx, WidgetKey nKey );

	// Phase D D.7 — ship status batch (flags + OOC + rank).  Read every
	// frame by the HUD — the live accessors mutate per sim tick.
	void					setShipStatus( int idx,
								dword nFlags, bool bOutOfCombat,
								float fOOCTimer, int nRank );

	// Phase D D.8 — planet state.  control() is a racy float (capture
	// progress); flags() is read by ~10 HUD/UI sites.  Writes to per-noun-
	// slot arrays — non-planet slots stay at zero.
	void					setPlanetState( int idx,
								float fControl, dword nFlags );

	int						shipEnergy( int idx ) const;
	int						shipMaxEnergy( int idx ) const;
	int						shipDamage( int idx ) const;
	int						shipMaxDamage( int idx ) const;
	float					shipSignature( int idx ) const;
	float					shipVelocity( int idx ) const;
	float					shipMaxVelocity( int idx ) const;
	float					shipView( int idx ) const;
	float					shipVisibility( int idx ) const;
	float					shipSensor( int idx ) const;

	// D.6
	bool					shipJumpHasGadget( int idx ) const;
	bool					shipJumpEngaged( int idx ) const;
	bool					shipJumping( int idx ) const;
	dword					shipJumpTime( int idx ) const;
	WidgetKey				shipCaptureTarget( int idx ) const;

	// D.7
	dword					shipFlags( int idx ) const;
	bool					shipIsOutOfCombat( int idx ) const;
	float					shipOOCTimer( int idx ) const;
	int						shipRank( int idx ) const;

	// D.8
	float					planetControl( int idx ) const;
	dword					planetFlags( int idx ) const;

	// Option 2 — render-time linear extrapolation of the client-local ship.
	// Advances both local and world position at `idx` by the ship's
	// horizontal heading direction * shipVelocity(idx) * fDt (heading is
	// recovered via atan2(worldFrame.k.x, worldFrame.k.z) to match the
	// sim-side dynamics in NounShipControl::updateDynamics exactly).
	// Used only for the local player's own ship (remote ships are
	// Smoother-routed sim-side).  With Option 3 active, called on the
	// materialized m_Pinned once per pin (not on a source slot), so
	// per-slot applied-dt tracking is no longer needed.
	void					extrapLocalShip( int idx, float fDt );

	// Option 3 — materialize this snapshot as the linear interpolation of
	// two history slots A (older) and B (newer) at fractional time t ∈ [0, 1].
	// Starts with `*this = B` (structural data — key list, ship scalars,
	// frames — come from the newer capture) and overrides positions +
	// velocities with a linear blend between A and B for each noun that
	// exists in BOTH.  Nouns in B but not A retain B's values (newly
	// visible, zero lag).  Frames are NOT interpolated — rotation stepping
	// at 20 Hz is visually imperceptible for typical yaw rates, and a
	// component-wise matrix lerp of frames caused mesh rendering to go
	// invisible/translucent in the initial roll-out.  If frame interp is
	// revisited later, use quaternion slerp and validate before re-enable.
	void					materialize( const RenderSnapshot & A,
									const RenderSnapshot & B,
									float t );

	// Option 3 — copy one noun's pose (local+world position, local+world
	// frame, velocity) from src[srcIdx] into (*this)[dstIdx].  Used to
	// overlay the client-local ship's freshest (un-interpolated) pose on
	// top of the materialized interpolated view before applying
	// extrapLocalShip.  Structural fields (key, ship scalars) stay at
	// whatever materialize left them.
	void					copyPoseFrom( const RenderSnapshot & src,
									int srcIdx, int dstIdx );

	// World-state snapshot fields (populated by WorldContext::captureRenderSnapshot).
	Vector3					m_CameraPosition;
	Matrix33				m_CameraFrame;
	float					m_Time;
	dword					m_Tick;
	// Wall-clock (QPC) ticks stamped at capture time.  The ring's pinForFrame
	// uses (Time::ticks() - m_CaptureTicks) to decide how much to extrapolate
	// the local ship between sim publishes.  Zero until the first capture.
	qword					m_CaptureTicks;

private:
	// Using std::vector (not medusa's Array) because we want reserve + clear
	// semantics — zero free-and-realloc per frame once capacity settles.
	std::vector<WidgetKey>	m_Keys;
	std::vector<Vector3>	m_Positions;
	std::vector<Matrix33>	m_Frames;
	std::vector<Vector3>	m_WorldPositions;
	std::vector<Matrix33>	m_WorldFrames;
	std::vector<Vector3>	m_Velocities;
	std::vector<dword>		m_NodeFlags;

	// Phase D — per-ship combat state.  Parallel to the noun arrays above;
	// non-ship slots stay at zero.  See setShipState / shipEnergy / etc.
	// Despite the "Ship" prefix, m_ShipDamage / m_ShipMaxDamage are reused
	// by NounGadget::captureSnapshotState (D.5) — they're per-noun-slot,
	// not ship-specific.  Helper naming (shipDamage(idx), gadgetDamageFor-
	// Render) reflects the caller's view of the slot.
	std::vector<int>		m_ShipEnergy;
	std::vector<int>		m_ShipMaxEnergy;
	std::vector<int>		m_ShipDamage;
	std::vector<int>		m_ShipMaxDamage;
	std::vector<float>		m_ShipSignature;

	// Phase D D.4 — per-ship motion + sensor scalars.  Same parallel-array
	// pattern; non-ship slots stay at zero.
	std::vector<float>		m_ShipVelocity;
	std::vector<float>		m_ShipMaxVelocity;
	std::vector<float>		m_ShipView;
	std::vector<float>		m_ShipVisibility;
	std::vector<float>		m_ShipSensor;

	// Phase D D.6 — jump drive + capture target.  Booleans stored as int
	// (vector<bool> is the proxy-bit-vector specialization, awkward to use).
	std::vector<int>		m_ShipJumpHasGadget;
	std::vector<int>		m_ShipJumpEngaged;
	std::vector<int>		m_ShipJumping;
	std::vector<dword>		m_ShipJumpTime;
	std::vector<qword>		m_ShipCaptureTarget;	// WidgetKey-as-qword; 0 = none

	// Phase D D.7 — ship status batch.
	std::vector<dword>		m_ShipFlags;
	std::vector<int>		m_ShipOutOfCombat;
	std::vector<float>		m_ShipOOCTimer;
	std::vector<int>		m_ShipRank;

	// Phase D D.8 — planet state.  Per-noun-slot; non-planet slots stay zero.
	std::vector<float>		m_PlanetControl;
	std::vector<dword>		m_PlanetFlags;

	// Index into the parallel arrays by noun key.  Populated by addNoun,
	// cleared by clear().  Lets the render path map a live Noun back to
	// its snapshot slot without scanning.
	std::unordered_map<qword, int>	m_KeyToIndex;
};

//---------------------------------------------------------------------------------------------------

// 4-slot history ring with snapshot-pair render interpolation (Option 3).
//
// Sim thread publishes captures at the sim-tick rate (20 Hz by default).
// Render thread, at pinForFrame time, computes a target wall-clock time
// `now - sm_fPlayoutDelaySeconds` (≈ 50 ms = 1 sim tick by default), finds
// the two published slots that bracket the target, and LERPs them into
// m_Pinned.  All render reads (Noun::calculateWorld snapshot short-circuit,
// shipHeadingForRender and friends) consult m_Pinned via readSlot().
//
// Effect: remote nouns move smoothly at render rate (60–144 fps) without
// extrapolation noise; local ship rides on top via Option 2's velocity-
// forward extrap from the newest slot for zero input lag.
//
// Race model: reader pins two slots (the bracket pair) via an atomic
// bitmask during materialize, then releases.  Writer's writeSlot() picks
// a slot that's neither pinned nor the most-recently-published.  With 4
// slots, at least 1 is always free even when reader holds 2 and the
// "latest" flag marks a third — pre-materialize writes never race reader
// reads.  After materialize, m_Pinned is self-contained and the source
// slots are free to be overwritten; the reader releases its pins
// immediately, not at end-of-frame.
class DLL RenderSnapshotRing
{
public:
	RenderSnapshotRing();

	// Sim-side: get the slot to write into.  Always returns a slot that is
	// neither in the reader's pin bitmask nor the most-recently-published slot.
	RenderSnapshot &		writeSlot();

	// Render-side: materialize the interpolated view into m_Pinned and return
	// a reference.  All per-noun reads during the frame go through readSlot()
	// which returns the same m_Pinned, guaranteeing consistency across the
	// whole render.  The source-slot pins are released internally before
	// return — m_Pinned is a self-contained copy.  Match every pinForFrame
	// with one releaseFrame for API symmetry (releaseFrame is a no-op with
	// the copy-out design but reserved for future extension).
	const RenderSnapshot &	pinForFrame();
	void					releaseFrame();

	// Render-side: returns the currently materialized m_Pinned.  Empty until
	// the first pinForFrame call.
	const RenderSnapshot &	readSlot() const;

	// Sim-side: publish the slot last returned by writeSlot(), advancing the
	// "latest published" pointer.  Render frames started after this call see
	// the new snapshot; in-progress frames keep their pinned snapshot.
	void					publish();

	// Render-side: release the slot we were reading (legacy entry point).
	// Equivalent to releaseFrame.
	void					releaseRead() { releaseFrame(); }

	// Option 2 — tell the ring which noun is the client-local player ship.
	// Set by WorldClient when self is assigned/cleared.  Zero key (default)
	// disables local-ship extrapolation.  Only the local ship is
	// extrapolated; remote ships keep their sim-side Smoother output and
	// ride Option 3's inter-tick interpolation instead.
	static void				setLocalShipKey( const WidgetKey & nKey );

	// Process-wide singleton.
	static RenderSnapshotRing & instance();

	// Option 3 — snapshot-pair interpolation master switch.  Default true.
	// When false, pinForFrame falls back to "use the newest published slot
	// as-is" (plus local-ship extrap), matching the Option 2 behaviour for
	// quick comparison / bisection.
	static bool				sm_bRenderInterpolation;
	// Option 3 — playout delay in wall-clock seconds.  Render shows the
	// world as it was `sm_fPlayoutDelaySeconds` ago so interpolation always
	// has a valid forward bracket (target between two known snapshots).
	// 0.05 s = one 20 Hz sim tick — the minimum to guarantee forward data.
	// Bump to ~0.1 s if the sim cadence is jittery and interpolation runs
	// off the end of the bracket (visible as brief freezes).  Local-ship
	// extrap layers on top from the newest slot, so this delay affects
	// remote nouns only.
	static float			sm_fPlayoutDelaySeconds;

private:
	enum { NUM_SLOTS = 4 };
	RenderSnapshot			m_Slots[NUM_SLOTS];
	// The interpolated composite returned by pinForFrame / readSlot.
	// Written only by the render thread, read by the render thread and
	// any other thread that happens to call readSlot() (rare; profiler
	// dumps etc.).  Not synchronized — callers that read outside a
	// pin/release pair see whatever was last materialized.
	RenderSnapshot			m_Pinned;

	// C++11 atomics with explicit memory_order on every access.  See per-
	// site ordering comments in the .cpp.
	std::atomic<int>		m_nLatestPublished;	// index sim last published, -1 if none
	std::atomic<int>		m_nWriteSlot;		// index sim currently writing into
	// Bitmask: bit i = reader pinned slot i.  Set by pinForFrame before it
	// reads the two bracket slots, cleared after materialize.  writeSlot
	// avoids any bit that's set.
	std::atomic<int>		m_nReaderPinMask;

	// WidgetKey (as qword) of the client's own ship, or 0 when no self is
	// set (pre-login, after logout, or on the server).  Atomic because
	// WorldClient sets this on the network-message thread while pinForFrame
	// reads from the render thread.
	static std::atomic<qword>	sm_LocalShipKey;
};

//---------------------------------------------------------------------------------------------------

// Inline accessors — defined here so the hot path doesn't pay a call overhead.

inline int RenderSnapshot::nounCount() const
{
	return (int)m_Keys.size();
}

inline WidgetKey RenderSnapshot::nounKey( int i ) const
{
	return m_Keys[i];
}

inline const Vector3 & RenderSnapshot::position( int i ) const
{
	return m_Positions[i];
}

inline const Matrix33 & RenderSnapshot::frame( int i ) const
{
	return m_Frames[i];
}

inline const Vector3 & RenderSnapshot::worldPosition( int i ) const
{
	return m_WorldPositions[i];
}

inline const Matrix33 & RenderSnapshot::worldFrame( int i ) const
{
	return m_WorldFrames[i];
}

inline const Vector3 & RenderSnapshot::velocity( int i ) const
{
	return m_Velocities[i];
}

inline dword RenderSnapshot::nodeFlags( int i ) const
{
	return m_NodeFlags[i];
}

inline int RenderSnapshot::shipEnergy( int i ) const
{
	return m_ShipEnergy[i];
}

inline int RenderSnapshot::shipMaxEnergy( int i ) const
{
	return m_ShipMaxEnergy[i];
}

inline int RenderSnapshot::shipDamage( int i ) const
{
	return m_ShipDamage[i];
}

inline int RenderSnapshot::shipMaxDamage( int i ) const
{
	return m_ShipMaxDamage[i];
}

inline float RenderSnapshot::shipSignature( int i ) const
{
	return m_ShipSignature[i];
}

inline float RenderSnapshot::shipVelocity( int i ) const
{
	return m_ShipVelocity[i];
}

inline float RenderSnapshot::shipMaxVelocity( int i ) const
{
	return m_ShipMaxVelocity[i];
}

inline float RenderSnapshot::shipView( int i ) const
{
	return m_ShipView[i];
}

inline float RenderSnapshot::shipVisibility( int i ) const
{
	return m_ShipVisibility[i];
}

inline float RenderSnapshot::shipSensor( int i ) const
{
	return m_ShipSensor[i];
}

inline bool RenderSnapshot::shipJumpHasGadget( int i ) const
{
	return m_ShipJumpHasGadget[i] != 0;
}

inline bool RenderSnapshot::shipJumpEngaged( int i ) const
{
	return m_ShipJumpEngaged[i] != 0;
}

inline bool RenderSnapshot::shipJumping( int i ) const
{
	return m_ShipJumping[i] != 0;
}

inline dword RenderSnapshot::shipJumpTime( int i ) const
{
	return m_ShipJumpTime[i];
}

inline WidgetKey RenderSnapshot::shipCaptureTarget( int i ) const
{
	return WidgetKey( m_ShipCaptureTarget[i] );
}

inline dword RenderSnapshot::shipFlags( int i ) const
{
	return m_ShipFlags[i];
}

inline bool RenderSnapshot::shipIsOutOfCombat( int i ) const
{
	return m_ShipOutOfCombat[i] != 0;
}

inline float RenderSnapshot::shipOOCTimer( int i ) const
{
	return m_ShipOOCTimer[i];
}

inline int RenderSnapshot::shipRank( int i ) const
{
	return m_ShipRank[i];
}

inline float RenderSnapshot::planetControl( int i ) const
{
	return m_PlanetControl[i];
}

inline dword RenderSnapshot::planetFlags( int i ) const
{
	return m_PlanetFlags[i];
}

//---------------------------------------------------------------------------------------------------

#endif

//---------------------------------------------------------------------------------------------------
// EOF
