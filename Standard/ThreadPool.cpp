/*
	ThreadPool.cpp
	(c)2025 Palestar
*/

#include "Standard/ThreadPool.h"
#include "Standard/Atomic.h"
#include "Standard/AutoLock.h"
#include "Debug/Log.h"

#if defined(_WIN32)
#include <windows.h>
#endif

//---------------------------------------------------------------------------------------------------

static int detectWorkerCount( int nRequested )
{
	if ( nRequested > 0 )
		return nRequested;

	int nHW = 4;		// sane default if detection fails
#if defined(_WIN32)
	SYSTEM_INFO si;
	GetSystemInfo( &si );
	if ( si.dwNumberOfProcessors > 0 )
		nHW = (int)si.dwNumberOfProcessors;
#endif

	// Leave one core for the main/render thread.  Clamp — too many workers
	// just add contention on the job dispatch without actually speeding things up
	// for per-frame work sized in the tens of items.
	int nWorkers = nHW - 1;
	if ( nWorkers < 1 ) nWorkers = 1;
	if ( nWorkers > 16 ) nWorkers = 16;
	return nWorkers;
}

//---------------------------------------------------------------------------------------------------

ThreadPool::ThreadPool( int nWorkers /*= -1*/ ) :
	m_JobFn( NULL ),
	m_JobUserData( NULL ),
	m_nJobEnd( 0 ),
	m_nJobNext( 0 ),
	m_nPendingJobs( 0 ),
	m_JobAvailable( false ),		// auto-reset
	m_AllDone( false ),				// auto-reset
	m_bShutdown( 0 ),
	m_nWorkers( detectWorkerCount( nWorkers ) )
{
	LOG_STATUS( "ThreadPool", "Starting with %d workers", m_nWorkers );

	m_Workers.allocate( m_nWorkers );
	for ( int i = 0; i < m_nWorkers; ++i )
	{
		m_Workers[i] = new ThreadPoolWorker( this, i );
		m_Workers[i]->resume();		// Thread starts suspended; kick it off
	}
}

ThreadPool::~ThreadPool()
{
	// Signal shutdown, wake every worker.  Each will see m_bShutdown and exit
	// its run loop.  Then Thread's destructor joins them (via wait()).
	Atomic::swap( &m_bShutdown, 1 );
	for ( int i = 0; i < m_nWorkers; ++i )
		m_JobAvailable.signal();

	// Thread dtor waits on the thread to exit, so just deleting suffices.
	for ( int i = 0; i < m_Workers.size(); ++i )
		delete m_Workers[i];
	m_Workers.release();
}

//---------------------------------------------------------------------------------------------------

void ThreadPool::parallelFor( int begin, int end, JobFunc fn, void * userData )
{
	const int nJobs = end - begin;
	if ( nJobs <= 0 || fn == NULL )
		return;

	// Single-job fast-path: don't bother waking a worker, just run inline.
	// Saves the dispatch + wake + wait round-trip for trivial cases.
	if ( nJobs == 1 )
	{
		fn( begin, userData );
		return;
	}

	// Publish dispatch state.  parallelFor is blocking, so no other thread
	// can enter this function concurrently and there's no contention on these
	// fields at publish time.  Workers read them after m_JobAvailable fires,
	// which is a release/acquire boundary in practice on every supported
	// platform (Windows Event, pthread cond).
	m_JobFn = fn;
	m_JobUserData = userData;
	m_nJobEnd = end;
	Atomic::swap( &m_nJobNext, begin );
	Atomic::swap( &m_nPendingJobs, nJobs );

	// Wake up to min(nJobs, nWorkers) workers.  Signaling more than we have
	// work for is harmless but wastes context switches.
	int nWake = nJobs < m_nWorkers ? nJobs : m_nWorkers;
	for ( int i = 0; i < nWake; ++i )
		m_JobAvailable.signal();

	// Block until the last worker signals completion.  m_AllDone is auto-reset
	// so the next parallelFor call starts with a cleared event.
	m_AllDone.wait();
}

int ThreadPool::grabNextJob()
{
	// Atomically fetch-and-add.  Atomic::add returns the value BEFORE the add,
	// which is exactly the index we should process (or >= end meaning drained).
	int idx = Atomic::add( &m_nJobNext, 1 );
	if ( idx >= m_nJobEnd )
		return -1;
	return idx;
}

void ThreadPool::onJobComplete()
{
	// Atomic::decrement returns the value AFTER the decrement.  When it hits
	// zero, this thread finished the last job of the dispatch — wake the
	// main thread waiting in parallelFor().
	if ( Atomic::decrement( &m_nPendingJobs ) == 0 )
		m_AllDone.signal();
}

ThreadPool & ThreadPool::shared()
{
	// Lazy-init, leak-on-exit.  Process-wide singleton lives until termination —
	// we never want to destroy it while other threads might still be submitting.
	static ThreadPool * s_pShared = new ThreadPool();
	return *s_pShared;
}

//---------------------------------------------------------------------------------------------------

// Per-thread worker index.  Set when a ThreadPoolWorker thread enters run();
// stays -1 on the main thread and on any non-pool thread.  Render code reads
// this via ThreadPool::currentWorkerIndex() to route shared-state mutations
// into per-worker scratch slots during parallelFor.
static thread_local int tl_nWorkerIndex = -1;

int ThreadPool::currentWorkerIndex()
{
	return tl_nWorkerIndex;
}

ThreadPoolWorker::ThreadPoolWorker( ThreadPool * pool, int nWorkerIndex ) :
	Thread( Thread::STANDARD ),
	m_pPool( pool ),
	m_nWorkerIndex( nWorkerIndex )
{}

int ThreadPoolWorker::run()
{
	// Publish our worker index to thread-local storage so any code running
	// on this thread (e.g. DisplayDevice::push during parallel preRender)
	// can distinguish workers from the main thread.
	tl_nWorkerIndex = m_nWorkerIndex;

	while ( true )
	{
		// Sleep until there's work OR the pool is being destroyed.  Event is
		// auto-reset; exactly one waiter wakes per signal().
		m_pPool->m_JobAvailable.wait();

		if ( m_pPool->m_bShutdown != 0 )
			return 0;

		// Drain as many jobs as we can from this dispatch before going back
		// to sleep.  Workers race on the atomic counter — whoever fetches a
		// valid index runs it; losers see -1 and fall through.
		while ( true )
		{
			int idx = m_pPool->grabNextJob();
			if ( idx < 0 )
				break;

			m_pPool->m_JobFn( idx, m_pPool->m_JobUserData );
			m_pPool->onJobComplete();
		}
	}
}

//---------------------------------------------------------------------------------------------------
//EOF
