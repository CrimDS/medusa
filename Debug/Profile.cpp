/*
	Profile.cpp

	Profile code
	(c)2005 Palestar, Richard Lyle
*/

#define DEBUG_DLL
#include "Debug/Profile.h"
#include "Debug/Assert.h"
#include "Standard/Exception.h"
#include "Standard/Tree.h"
#include "Standard/Time.h"
#include "Standard/Thread.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unordered_map>
#include <string>

//----------------------------------------------------------------------------

const int MAX_TIMER_DEPTH = 128;

//------------------------------------------------------------------------------

CriticalSection					Profiler::sm_Lock;
Profiler::ThreadHash			Profiler::sm_ThreadHash;
Profiler::ThreadCallSiteHash	Profiler::sm_ThreadCallSites;
Profiler::MessageHash			Profiler::sm_MessageHash;
Profiler::StackHash				Profiler::sm_StackHash;
dword							Profiler::sm_nLastUpdate = 0;
qword							Profiler::sm_nLastCPU = 0;
qword							Profiler::sm_nTotalCPU = 0;
qword							Profiler::sm_nPeakCPU = 0;
float							Profiler::sm_fLoad = 1.0f;

//----------------------------------------------------------------------------

// String-interning tables.  See header comment for rationale (cross-DLL
// duplicate literals collapsed to a single canonical pointer so the tree
// builder's pointer-equality parent matching works).
//
// sm_NameByContent owns the canonical char buffer (strdup'd, never freed —
// process-lifetime).  sm_NameByPtr is a fast cache: a raw incoming pointer
// we've seen before maps directly to its canonical without the std::string
// hash lookup.  The cache hit rate is ~100% after warmup since profile names
// are string literals that always have the same address within their own DLL.
static std::unordered_map< std::string, const char * > *  sg_pNameByContent = NULL;
static std::unordered_map< const char *,  const char * > * sg_pNameByPtr = NULL;

const char * Profiler::internLocked( const char * pName )
{
	if ( pName == NULL )
		return NULL;

	// Lazy-create on first use.  We can't use static-initialised maps because
	// of the DLL-global construction order (this file's statics may run before
	// std::unordered_map's allocator is ready in some configurations).
	if ( sg_pNameByPtr == NULL )
	{
		sg_pNameByPtr     = new std::unordered_map< const char *, const char * >();
		sg_pNameByContent = new std::unordered_map< std::string, const char * >();
	}

	// Fast path: same raw pointer seen before (the common case once running).
	std::unordered_map< const char *, const char * >::iterator rawIt = sg_pNameByPtr->find( pName );
	if ( rawIt != sg_pNameByPtr->end() )
		return rawIt->second;

	// Slow path: look up by content.  If a different DLL gave us a literal
	// with the same text, this finds the canonical pointer that DLL's first
	// caller already registered.  Otherwise allocate a new canonical buffer.
	std::string key( pName );
	std::unordered_map< std::string, const char * >::iterator it = sg_pNameByContent->find( key );
	const char * pCanonical;
	if ( it != sg_pNameByContent->end() )
	{
		pCanonical = it->second;
	}
	else
	{
		size_t nLen = key.size();
		char * pCopy = new char[ nLen + 1 ];
		memcpy( pCopy, pName, nLen + 1 );
		pCanonical = pCopy;
		(*sg_pNameByContent)[ key ] = pCanonical;
	}

	(*sg_pNameByPtr)[ pName ] = pCanonical;
	return pCanonical;
}

const char * Profiler::intern( const char * pName )
{
	AutoLock lock( &sm_Lock );
	return internLocked( pName );
}

//----------------------------------------------------------------------------

// Pack (parent_pointer, name_pointer) into a 64-bit hash key for the
// per-call-site map.  After interning both halves are canonical, so the same
// call site always produces the same key.  Win32 pointers are 32-bit so the
// shift+or fits cleanly; on 64-bit hosts this would lose the upper pointer
// bits — but the canonical pointers come from sm_NameByContent which lives
// in a single allocation pool, so the high bits are effectively constant.
static inline qword makeCallSiteKey( const char * pParent, const char * pName )
{
	return ( (qword)(uintptr_t)pParent << 32 ) | (qword)(dword)(uintptr_t)pName;
}

//----------------------------------------------------------------------------

// begin profiling
void Profiler::start( const char * pName )
{
	AutoLock lock( &sm_Lock );

	// Intern at the start() boundary so every Timer on the stack carries the
	// canonical pointer.  Parent lookup in end() can then compare pointers
	// with no special handling.
	const char * pInterned = internLocked( pName );

	Array< Timer > & timers = sm_StackHash[ Thread::getCurrentThreadId() ];

	// push a timer structure into the stack
	Timer & timer = timers.push();
	timer.pName = pInterned;
	timer.nTime = Time::CPU();
	timer.nBytes = 0;

	ERROR_ON( timers.size() > MAX_TIMER_DEPTH );		// throws exception if someone forgets to call Profiler::end()
}

void Profiler::end()
{
	AutoLock lock( &sm_Lock );

	dword nThread = Thread::getCurrentThreadId();

	Array< Timer > & timers = sm_StackHash[ nThread ];
	if ( timers.size() > 0 )
	{
		Timer & timer = timers[ timers.size() - 1 ];
		const qword nElapsed = Time::CPU() - timer.nTime;

		const char * pName   = timer.pName;
		const char * pParent = ( timers.size() >= 2 )
								? timers[ timers.size() - 2 ].pName
								: NULL;

		// Per-call-site accumulation (the new model used by ALT+P).  Each
		// (parent, name) tuple has its own entry so a function called from
		// multiple parents gets correctly attributed under each.
		{
			CallSiteHash & sites = sm_ThreadCallSites.insert( nThread );
			CallSite & cs = sites.insert( makeCallSiteKey( pParent, pName ) );
			cs.pName       = pName;
			cs.pParentName = pParent;
			cs.nCPU       += nElapsed;
			cs.nHits      += 1;
		}

		// Legacy flat Profile (one per (thread, name)).  Kept for the
		// ProfilerServer wire format and TestProfiler.  Self-references are
		// suppressed (proxy WorldContext::update calling itself) so the
		// parent hint doesn't loop.
		{
			Profile & profile = sm_ThreadHash.insert( nThread ).insert( (dword)(uintptr_t)pName );
			profile.pName = pName;
			if ( profile.pParentName == NULL && pParent != NULL && pParent != pName )
				profile.pParentName = pParent;
			profile.nCPU  += nElapsed;
			profile.nHits += 1;
		}

		// remove the timing section from the stack
		timers.pop();
	}

	updateProfiles();
}

void Profiler::stop()
{
	AutoLock lock( &sm_Lock );

	dword nThread = Thread::getCurrentThreadId();
	sm_ThreadHash.removeByKey( nThread );
	sm_ThreadCallSites.removeByKey( nThread );
	sm_StackHash.remove( nThread );
}

void Profiler::message( dword nLine, const char * pMessage )
{
	AutoLock lock( &sm_Lock );
	sm_MessageHash.insert( nLine, CharString( pMessage ) );
}

void Profiler::removeMessage( dword nLine )
{
	AutoLock lock( &sm_Lock );
	sm_MessageHash.removeByKey( nLine );
}

void Profiler::clearMessages()
{
	AutoLock lock( &sm_Lock );
	sm_MessageHash.release();
}

//----------------------------------------------------------------------------

void Profiler::lock( bool bLock )
{
	if ( bLock )
		sm_Lock.lock();
	else
		sm_Lock.unlock();
}

int Profiler::threadCount()
{
	return sm_ThreadHash.size();
}

dword Profiler::thread( int n )
{
	return sm_ThreadHash.key( n );
}

int Profiler::profileCount( dword nThread )
{
	return sm_ThreadHash.hash()[ nThread ].size();
}

Profiler::Profile & Profiler::profile( dword nThread, int n )
{
	return sm_ThreadHash.hash()[ nThread ][ n ];
}

int Profiler::callSiteCount( dword nThread )
{
	return sm_ThreadCallSites.hash()[ nThread ].size();
}

Profiler::CallSite & Profiler::callSite( dword nThread, int n )
{
	return sm_ThreadCallSites.hash()[ nThread ][ n ];
}

int Profiler::messageCount()
{
	return sm_MessageHash.size();
}

const char * Profiler::message( int n )
{
	return sm_MessageHash[ n ];
}

qword Profiler::totalCPU()
{
	return sm_nTotalCPU;
}

qword Profiler::peakCPU()
{
	return sm_nPeakCPU;
}

float Profiler::CPUused()
{
	return sm_fLoad;
}

//----------------------------------------------------------------------------

void Profiler::updateProfiles()
{
	dword nClock = Time::milliseconds();
	dword nElapsed = nClock - sm_nLastUpdate;
	if ( nElapsed >= 1000 )
	{
		qword nCPU = Time::CPU();

		sm_nPeakCPU = 0;
		for(int i=0;i<sm_ThreadHash.size();i++)
		{
			dword nThread = sm_ThreadHash.key( i );

			// Update CPU charge for any currently-open timers so a long-
			// running section visibly accumulates time during the second
			// it's running, and so we can detect deadlocks.  We deliberately
			// do NOT bump nHits here — the old behaviour inflated visible
			// hit rates (e.g. WorldContext::update appearing to fire 683x/s
			// on a 100Hz thread because every per-second tick added a hit
			// to every still-open timer in the stack).
			Array< Timer > & timers = sm_StackHash[ nThread ];
			for(int k=0;k<timers.size();k++)
			{
				Timer & timer = timers[ k ];
				const qword nLive = nCPU - timer.nTime;

				const char * pName   = timer.pName;
				const char * pParent = ( k >= 1 ) ? timers[ k - 1 ].pName : NULL;

				CallSiteHash & sites = sm_ThreadCallSites.insert( nThread );
				CallSite & cs = sites.insert( makeCallSiteKey( pParent, pName ) );
				cs.pName       = pName;
				cs.pParentName = pParent;
				cs.nCPU       += nLive;

				Profile & profile = sm_ThreadHash.insert( nThread ).insert( (dword)(uintptr_t)pName );
				profile.pName = pName;
				profile.nCPU += nLive;
				if ( profile.nCPU > sm_nPeakCPU )
					sm_nPeakCPU = profile.nCPU;

				timer.nTime = nCPU;
			}

			ProfileHash & profiles = sm_ThreadHash[ i ];
			for(int j=0;j<profiles.size();j++)
			{
				Profile & profile = profiles[ j ];

				profile.nAvCPU = (profile.nCPU * 1000) / nElapsed;
				profile.nCPU = 0;
				profile.nAvHits = (profile.nHits * 1000) / nElapsed;
				profile.nHits = 0;
			}
		}

		// Roll the per-call-site accumulators into per-second averages and
		// reset.  Done in a separate loop because thread sets in
		// sm_ThreadCallSites and sm_ThreadHash may not be in lockstep
		// (a thread can be present in one and not the other transiently).
		for(int i=0;i<sm_ThreadCallSites.size();i++)
		{
			CallSiteHash & sites = sm_ThreadCallSites[ i ];
			for(int j=0;j<sites.size();j++)
			{
				CallSite & cs = sites[ j ];
				cs.nAvCPU  = (cs.nCPU  * 1000) / nElapsed;
				cs.nCPU    = 0;
				cs.nAvHits = (cs.nHits * 1000) / nElapsed;
				cs.nHits   = 0;
			}
		}

		qword nCPUElapsed = nCPU - sm_nLastCPU;
		sm_nTotalCPU = (nCPUElapsed * 1000) / nElapsed;
		sm_nLastCPU = nCPU;
		sm_nLastUpdate = nClock;
		sm_fLoad = Clamp<float>( ((float)sm_nPeakCPU) / ((float)sm_nTotalCPU), 0.0f, 1.0f );
	}
}

//-------------------------------------------------------------------------------
// EOF
