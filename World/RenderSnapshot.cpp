/*
	RenderSnapshot.cpp
	(c)2026 Palestar
*/

#include "World/RenderSnapshot.h"
#include "Standard/Atomic.h"

//---------------------------------------------------------------------------------------------------

// Per-thread "rendering from snapshot" flag — see header comment.
static thread_local bool tl_bRenderingFromSnapshot = false;

bool isRenderingFromSnapshot()			{ return tl_bRenderingFromSnapshot; }
void setRenderingFromSnapshot( bool a_bOn ) { tl_bRenderingFromSnapshot = a_bOn; }

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
	m_KeyToIndex[ nKey.m_Id ] = nIndex;
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
		const int pinned    = m_nReaderPinned;
		const int published = m_nLatestPublished;

		int slot = m_nWriteSlot;
		for ( int tries = 0; tries < NUM_SLOTS; ++tries )
		{
			if ( slot != pinned && slot != published )
				break;
			slot = ( slot + 1 ) % NUM_SLOTS;
		}

		// Re-check pinned didn't change while we were picking.
		if ( m_nReaderPinned == pinned )
		{
			Atomic::swap( (volatile int *)&m_nWriteSlot, slot );
			return m_Slots[ slot ];
		}
	}

	// Fallback if we keep losing the race — use whatever m_nWriteSlot is.
	return m_Slots[ m_nWriteSlot ];
}

const RenderSnapshot & RenderSnapshotRing::pinForFrame()
{
	// Pin the latest-published slot for the duration of this render frame.
	// Per-noun lookups will go through readSlot() which returns this same
	// slot, so all nouns rendered in the frame see a consistent snapshot
	// even if sim publishes a newer one mid-frame.
	int slot = m_nLatestPublished;
	if ( slot < 0 )
		slot = 0;
	Atomic::swap( (volatile int *)&m_nReaderPinned, slot );
	return m_Slots[ slot ];
}

void RenderSnapshotRing::releaseFrame()
{
	Atomic::swap( (volatile int *)&m_nReaderPinned, -1 );
}

const RenderSnapshot & RenderSnapshotRing::readSlot() const
{
	// Prefer the pinned slot during a frame; fall back to latest-published
	// for code paths that read outside a pin window (e.g. profiler dumps).
	int slot = m_nReaderPinned;
	if ( slot < 0 )
	{
		slot = m_nLatestPublished;
		if ( slot < 0 )
			slot = 0;
	}
	return m_Slots[ slot ];
}

void RenderSnapshotRing::publish()
{
	// Release-store the just-filled slot index so a future render frame
	// pinForFrame() picks it up.  Reader's currently-pinned slot is unaffected.
	Atomic::swap( (volatile int *)&m_nLatestPublished, m_nWriteSlot );
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
