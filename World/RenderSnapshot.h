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

	// World-state snapshot fields (populated by WorldContext::captureRenderSnapshot).
	Vector3					m_CameraPosition;
	Matrix33				m_CameraFrame;
	float					m_Time;
	dword					m_Tick;

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

	// Index into the parallel arrays by noun key.  Populated by addNoun,
	// cleared by clear().  Lets the render path map a live Noun back to
	// its snapshot slot without scanning.
	std::unordered_map<qword, int>	m_KeyToIndex;
};

//---------------------------------------------------------------------------------------------------

// 3-slot ring buffer.  Sim thread writes into a slot, publishes it, then
// rotates to the next.  Render thread "pins" the latest published slot for
// the duration of one frame so per-noun reads see a STABLE snapshot — without
// pinning, two nouns rendered in the same frame can see different snapshots
// if sim publishes mid-frame, which causes torn positions and streak artefacts.
//
// Why 3 slots: with 2 it's possible for sim to publish twice during a render
// frame; the second publish would target the slot render is pinning.  With 3,
// sim writes to slot != pinned && != latest_published, guaranteeing it never
// overwrites the slot the reader holds.
class DLL RenderSnapshotRing
{
public:
	RenderSnapshotRing();

	// Sim-side: get the slot to write into.  Always returns a slot that is
	// neither pinned by the reader nor the most-recently-published slot.
	RenderSnapshot &		writeSlot();

	// Render-side: pin the latest-published snapshot for the duration of one
	// frame.  All Noun::preRender lookups during the frame consult readSlot()
	// which returns the pinned slot — guaranteeing consistency across the
	// whole render even if sim publishes mid-frame.  Match every pinForFrame
	// with one releaseFrame.
	const RenderSnapshot &	pinForFrame();
	void					releaseFrame();

	// Render-side: returns the currently pinned snapshot, or the latest
	// published one if the reader hasn't pinned (e.g. early-out paths).
	const RenderSnapshot &	readSlot() const;

	// Sim-side: publish the slot last returned by writeSlot(), advancing the
	// "latest published" pointer.  Render frames started after this call see
	// the new snapshot; in-progress frames keep their pinned snapshot.
	void					publish();

	// Render-side: release the slot we were reading (legacy entry point).
	// Equivalent to releaseFrame.
	void					releaseRead() { releaseFrame(); }

	// Process-wide singleton.
	static RenderSnapshotRing & instance();

private:
	enum { NUM_SLOTS = 3 };
	RenderSnapshot			m_Slots[NUM_SLOTS];
	// C++11 atomics with explicit memory_order on every access.  volatile
	// is not a barrier under MSVC's default model; it just happens to work
	// on x86/x64 because InterlockedExchange is seq-cst.  Promoting to
	// std::atomic documents the handshake and is correct on weakly-ordered
	// architectures.  See per-site ordering comments in the .cpp.
	std::atomic<int>		m_nLatestPublished;	// index sim last published, -1 if none
	std::atomic<int>		m_nWriteSlot;		// index sim currently writing into
	std::atomic<int>		m_nReaderPinned;	// index reader pinned for frame, -1 if idle
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

//---------------------------------------------------------------------------------------------------

#endif

//---------------------------------------------------------------------------------------------------
// EOF
