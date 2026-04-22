/*
	RenderSnapshot.cpp
	(c)2026 Palestar
*/

#include "World/RenderSnapshot.h"
#include "World/WorldClient.h"		// for WorldClient::sm_bPipelinedSimRender — see setAssertSnapshotCoverage

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

bool assertSnapshotCoverageEnabled()		{ return tl_bAssertSnapshotCoverage; }
void setAssertSnapshotCoverage( bool a_bOn )
{
	// Arm if either: sim is genuinely pipelined (race risk → must avoid
	// live), OR audit mode is on (no race, but we want assertions to
	// surface unmigrated reads without enabling the still-flaky sim thread).
	tl_bAssertSnapshotCoverage = a_bOn &&
		( WorldClient::sm_bPipelinedSimRender || WorldClient::sm_bAssertSnapshotCoverage );
}

//---------------------------------------------------------------------------------------------------

RenderSnapshot::RenderSnapshot() :
	m_CameraPosition( true ),
	m_CameraFrame( Matrix33::IDENTITY ),
	m_Time( 0.0f ),
	m_Tick( 0 )
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

//---------------------------------------------------------------------------------------------------

RenderSnapshotRing::RenderSnapshotRing() :
	m_nLatestPublished( -1 ),
	m_nWriteSlot( 0 ),
	m_nReaderPinned( -1 )
{}

RenderSnapshot & RenderSnapshotRing::writeSlot()
{
	// Pick a slot that's neither pinned by the reader nor the most-recently-
	// published one.  With 3 slots and at most 1 reader + 1 writer, exactly
	// one of the three is always free, so this never has to wait.
	// Re-read pinned at the END to catch a reader that pinned mid-selection;
	// if so, retry with the new pinned value.  Bounded retries — at worst we
	// land on a slot that was just-pinned, in which case reader and writer
	// race on the slot data.  Cheap to avoid.
	for ( int retry = 0; retry < 4; ++retry )
	{
		// Acquire so we see the slot data the reader last finished with
		// before it released, and so we see the latest publish from a
		// prior sim tick (publish is a release on m_nLatestPublished).
		const int pinned    = m_nReaderPinned.load( std::memory_order_acquire );
		const int published = m_nLatestPublished.load( std::memory_order_acquire );

		// m_nWriteSlot is producer-private — only this thread writes it.
		int slot = m_nWriteSlot.load( std::memory_order_relaxed );
		for ( int tries = 0; tries < NUM_SLOTS; ++tries )
		{
			if ( slot != pinned && slot != published )
				break;
			slot = ( slot + 1 ) % NUM_SLOTS;
		}

		// Re-check pinned didn't change while we were picking.
		if ( m_nReaderPinned.load( std::memory_order_acquire ) == pinned )
		{
			// Relaxed: no other thread reads m_nWriteSlot.
			m_nWriteSlot.store( slot, std::memory_order_relaxed );
			return m_Slots[ slot ];
		}
	}

	// Fallback if we keep losing the race — use whatever m_nWriteSlot is.
	return m_Slots[ m_nWriteSlot.load( std::memory_order_relaxed ) ];
}

const RenderSnapshot & RenderSnapshotRing::pinForFrame()
{
	// Pin the latest-published slot for the duration of this render frame.
	// Per-noun lookups will go through readSlot() which returns this same
	// slot, so all nouns rendered in the frame see a consistent snapshot
	// even if sim publishes a newer one mid-frame.
	// Acquire: synchronize-with the release in publish() so the slot's
	// vectors are fully visible before we start reading them.
	int slot = m_nLatestPublished.load( std::memory_order_acquire );
	if ( slot < 0 )
		slot = 0;
	// Release: the writer's next writeSlot() must see this pin before it
	// picks a slot, otherwise it could race on the slot we just pinned.
	m_nReaderPinned.store( slot, std::memory_order_release );
	return m_Slots[ slot ];
}

void RenderSnapshotRing::releaseFrame()
{
	// Release: the writer's next writeSlot() sees the slot as free only
	// after the reader has stopped touching it.
	m_nReaderPinned.store( -1, std::memory_order_release );
}

const RenderSnapshot & RenderSnapshotRing::readSlot() const
{
	// Prefer the pinned slot during a frame; fall back to latest-published
	// for code paths that read outside a pin window (e.g. profiler dumps).
	int slot = m_nReaderPinned.load( std::memory_order_acquire );
	if ( slot < 0 )
	{
		slot = m_nLatestPublished.load( std::memory_order_acquire );
		if ( slot < 0 )
			slot = 0;
	}
	return m_Slots[ slot ];
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
