/*
	Broker.h

	This object is used to store / load widgets
	(c)2005 Palestar, Richard Lyle
*/

#define FACTORY_DLL
#include "Broker.h"
#include "Standard/Time.h"
#include "Standard/Thread.h"
#include "Debug/Trace.h"
#include "Debug/Assert.h"
#include "Debug/SafeThread.h"
#include "Widget.h"

#include <time.h>
#include <atomic>
#include <thread>
#include <utility>
#include <vector>

// how many items to keep cached...
const int MAX_WIDGET_CACHE	= 1024 * 2;

// Cap on the worker pool.  More than this rarely helps — we're disk-IO bound
// for the cold path and Factory::createWidget contention starts to dominate.
const unsigned MAX_LOADER_WORKERS = 8;

//----------------------------------------------------------------------------

bool						Broker::sm_bEnableWidgetCache = false;
bool						Broker::sm_bEagerPrefetch    = true;

CriticalSection				Broker::sm_Lock;
Broker::BrokerHash			Broker::sm_BrokerHash;
Array< WidgetKey >			Broker::sm_AutoLoadList;		// list of widgets to autoload
Hash< WidgetKey, Widget * >	Broker::sm_AutoLoaded;			// hash of loaded widgets

bool						Broker::sm_bLoadingThreadActive = false;
Event						Broker::sm_LoadThreadEvent;		// signaled when new load requests are queued
Broker::LoadList			Broker::sm_LoadList;
Broker::LoadRequestHash		Broker::sm_LoadRequestHash;
Broker::RequestHash			Broker::sm_RequestHash;
Broker::ThreadPool			Broker::sm_LoadingPool;

namespace {
	// Per-thread "I am a Broker pool worker" flag.  Set by the worker entry
	// point and never cleared (the thread exits when the pool stops).  This
	// is what inLoadingThread() returns; it also drives the unified
	// requestLoad path (worker-on-worker dedup, cycle detection, work-stealing
	// wait).  thread_local on a primitive in an anonymous namespace is clean
	// across DLL boundaries — the variable lives in medusa.dll's TLS and is
	// read by medusa.dll's own code regardless of the calling DLL.
	thread_local bool tg_bIsLoadingWorker = false;

	// Live count of threads currently inside pBroker->load(). Read by
	// Broker::pendingLoadCount() so UI can gate on "load tree drained".
	std::atomic<int> sg_nActiveLoaders { 0 };

	// Wraps pBroker->load with the existing defensive try/catch and tracks
	// the active-loader count for pendingLoadCount.  Returns NULL on
	// failure or NULL broker.  Used by every pBroker->load() call site so
	// the active counter reflects all load activity, not just one path.
	Widget * loadSafely( Broker * pBroker, const WidgetKey & key )
	{
		if ( pBroker == nullptr )
			return nullptr;

		sg_nActiveLoaders.fetch_add( 1, std::memory_order_relaxed );
		Widget * pWidget = nullptr;
		try { pWidget = pBroker->load( key ); }
		catch ( ... ) { pWidget = nullptr; }
		sg_nActiveLoaders.fetch_sub( 1, std::memory_order_relaxed );

		return pWidget;
	}
}

//---------------------------------------------------------------------------------------------------

Broker::LoadingThread::LoadingThread() : SafeThread( Thread::STANDARD )
{}

//---------------------------------------------------------------------------------------------------

Broker::Request::Request() : m_bAttached( false )
{}

Broker::Request::~Request()
{
	detach();
}

void Broker::Request::attach()
{
	if (! m_bAttached )
	{
		AutoLock lock( &sm_Lock );
		sm_RequestHash.insert( requestID(), this );
		m_bAttached = true;
	}
}

void Broker::Request::detach()
{
	if ( m_bAttached )
	{
		AutoLock lock( &sm_Lock );
		sm_RequestHash.remove( requestID() );
		m_bAttached = false;
	}
}

//----------------------------------------------------------------------------

bool Broker::isLoadingThreadActive()
{
	return sm_bLoadingThreadActive;
}

bool Broker::startLoadingThread()
{
	AutoLock lock( &sm_Lock );
	if ( sm_bLoadingThreadActive )
		return false;		// pool already active!

	sm_bLoadingThreadActive = true;

	// Pool size: hardware_concurrency clamped to [2, MAX_LOADER_WORKERS].
	// hardware_concurrency() returns 0 on weird platforms; floor at 2.
	unsigned hw = std::thread::hardware_concurrency();
	if ( hw < 2 ) hw = 2;
	if ( hw > MAX_LOADER_WORKERS ) hw = MAX_LOADER_WORKERS;

	sm_LoadingPool.reserve( hw );
	for ( unsigned i = 0; i < hw; i++ )
	{
		LoadingThread * t = new LoadingThread();
		sm_LoadingPool.push_back( t );
		t->resume();
	}

	return true;
}

bool Broker::stopLoadingThread()
{
	ThreadPool pool;
	{
		AutoLock lock( &sm_Lock );
		if (! sm_bLoadingThreadActive )
			return false;

		sm_bLoadingThreadActive = false;
		pool.swap( sm_LoadingPool );
	}

	// Auto-reset event: signaling once wakes one waiter, but the workers also
	// poll on a 100ms timeout, so any worker that doesn't catch the signal
	// will notice sm_bLoadingThreadActive == false on its next tick and exit.
	// Signal once per worker to minimize shutdown latency in the common case.
	for ( size_t i = 0; i < pool.size(); i++ )
		sm_LoadThreadEvent.signal();

	// ~Thread() joins (or kills on KILL_TIMEOUT).
	for ( LoadingThread * t : pool )
		delete t;

	flushCache();
	return true;
}

void Broker::flushCache()
{
	AutoLock lock( &sm_Lock );
	widgetCache().clear();
}

int Broker::pendingLoadCount()
{
	int queued;
	{
		AutoLock lock( &sm_Lock );
		queued = sm_LoadList.size();
	}
	int active = sg_nActiveLoaders.load( std::memory_order_relaxed );
	return queued + active;
}

bool Broker::inLoadingThread()
{
	return tg_bIsLoadingWorker;
}

//! This starts an asynchronous load of the widget in the background, it will invoked the onLoaded() virtual
//! function in the LoadRequest object once the widget has been loaded...
//
// Unified entry: top-level external callers AND nested calls from within a
// worker's pBroker->load() chain go through the SAME state machine.  The
// in-flight gate (sm_LoadRequestHash) mediates every request, eliminating the
// double-load failure mode the per-thread blocking-set design hit.
//
// Cases handled (under sm_Lock):
//   1. Widget already in WidgetMap          → onLoaded + signal, done.
//   2. Hash miss + worker + bBlocking=true  → self-claim (loaderTID=me),
//                                             drop lock, inline-load,
//                                             loadNotify.  Avoids the
//                                             queue+signal+wait round-trip
//                                             on synchronous nested calls
//                                             where there's nothing to
//                                             parallelize anyway.
//   3. Hash miss + (non-worker OR           → insert entry (loaderTID=0),
//      bBlocking=false)                       queue in sm_LoadList, signal
//                                             pool, then wait or return.
//                                             Non-blocking calls from
//                                             workers MUST go this route,
//                                             because that's how operator>>
//                                             prefetch fans out across the
//                                             pool — if a worker self-claimed
//                                             a non-blocking request, every
//                                             prefetched child dep would
//                                             chain onto the same worker.
//   4. Hash hit + loaderTID == me           → recursion cycle, log + return
//                                             false.  Replaces the old
//                                             sm_BlockingLoadSet entirely.
//   5. Hash hit + loaderTID != me           → join the observer list.  If
//                                             blocking, wait — workers do
//                                             work-stealing so the pool
//                                             can't deadlock with itself.
bool Broker::requestLoad( const WidgetKey & a_nKey, const ClassKey & a_nType, Request * a_pRequest, bool a_bBlocking )
{
	// Cache hit fast path — no broker lock required, Widget map has its own.
	Widget::Ref pWidget = Widget::findWidgetByType( a_nKey, a_nType );
	if ( pWidget.valid() )
	{
		a_pRequest->onLoaded( pWidget );
		a_pRequest->signal();
		return true;
	}

	// Wall-clock timestamp for the "Load of X blocked for Y ms" diagnostic
	// below.  Was clock() which returns CPU-time-summed-across-all-threads
	// on Linux at microsecond resolution — producing log lines like "blocked
	// for 36625375 ms" on a server that had only been up 79 seconds.  QPC-
	// based Time::ticks() is wall-clock and matches the rest of the engine.
	const qword nStartTicks = Time::ticks();

	AutoLock lock( &sm_Lock );

	// Auto-start the pool on first use.
	if ( !isLoadingThreadActive() )
		if (! startLoadingThread() )
			return false;
	if ( sm_LoadingPool.empty() || ! a_pRequest )
		return false;

	const bool  bWorker = tg_bIsLoadingWorker;
	const dword myTID   = bWorker ? Thread::getCurrentThreadId() : 0;

	bool bSelfClaim = false;

	LoadRequestHash::Iterator iEntry = sm_LoadRequestHash.find( a_nKey );
	if (! iEntry.valid() )
	{
		LoadRequestEntry & entry = sm_LoadRequestHash[ a_nKey ];
		entry.observers.insert( a_pRequest->requestID() );

		if ( bWorker && a_bBlocking )
		{
			// Synchronous nested call from a worker: inline-load.  Skipping
			// the queue avoids per-call lock+signal+wait overhead, which
			// matters because nested-dep trees can be thousands deep on a
			// cold start.  Actual parallelism for this load tree is meant to
			// come from operator>> prefetch (non-blocking, queued path); by
			// the time the consumer reaches its blocking .valid() call, the
			// dep is already in flight on another worker and we hit case 5.
			entry.loaderTID = myTID;
			bSelfClaim = true;
		}
		else
		{
			// Non-blocking from anywhere, or any external thread: queue for
			// the pool to fan out across workers.
			entry.loaderTID = 0;
			sm_LoadList.insert( a_nKey );
		}
	}
	else
	{
		LoadRequestEntry & entry = *iEntry;

		// Cycle detection: a worker that's already loading this key is now
		// being asked to load it again (recursive ResourceLink).  This is
		// what sm_BlockingLoadSet used to detect.
		if ( bWorker && entry.loaderTID == myTID )
		{
			LOG_WARNING( "Broker", "Detected circular load for %llu", a_nKey.m_Id );
			return false;
		}

		if (! entry.observers.find( a_pRequest->requestID() ).valid() )
			entry.observers.insert( a_pRequest->requestID() );
	}

	a_pRequest->attach();

	if ( bSelfClaim )
	{
		// Inline load by current worker.  Mirror the worker loop's lock
		// dance: drop sm_Lock for load(), retake for loadNotify (which
		// removes the entry and signals observers).
		lock.release();

		Widget::Ref w = Widget::findWidget( a_nKey );
		if (! w.valid() )
			w = loadSafely( findBroker( a_nKey ), a_nKey );

		lock.set( &sm_Lock );
		loadNotify( a_nKey, w );
		return true;
	}

	lock.release();

	// Wake a worker (it may already be running; signal is harmless).
	sm_LoadThreadEvent.signal();

	if ( a_bBlocking )
	{
		if ( bWorker )
		{
			// Work-stealing wait.  If we just blocked on the request event,
			// and all other workers are also blocked on nested loads, nobody
			// would drain sm_LoadList — classic pool deadlock.  Instead, do
			// queue work ourselves while we wait for our request to fire.
			//
			// processOneItem returns true when it loaded an item (which may
			// have been ours, signaling a_pRequest; or may have been someone
			// else's, helping the pool make progress).  When the queue is
			// empty we briefly wait on the request event; if our load got
			// completed by another worker mid-wait, wait() returns false and
			// we exit.
			while ( true )
			{
				bool didWork = false;
				while ( processOneItem() )
				{
					didWork = true;
					// Cheap poll — if our request just got signaled by the
					// item we processed (or by another worker concurrently),
					// bail out without sleeping.
					if (! a_pRequest->wait( 0 ) )
						goto worker_done;
				}

				// No work to steal.  Sleep on the request event itself; we'll
				// be released either when our load completes (signal) or
				// after a short timeout (in case new work appears).  Keep the
				// timeout small so we don't sit on stealable work.
				if (! a_pRequest->wait( didWork ? 0 : 5 ) )
					break;	// signaled → done
			}
worker_done: ;
		}
		else
		{
			a_pRequest->wait();
		}
		const qword nElapsedMs = ( Time::ticks() - nStartTicks ) * 1000
		                       / Time::ticksPerSecond();
		LOG_DEBUG_LOW( "Broker", "Load of %s blocked for %llu ms",
			a_nKey.string().cstr(), (unsigned long long)nElapsedMs );
	}

	return true;
}

void Broker::loadNotify( const WidgetKey & a_nKey, Widget * a_pWidget )
{
	AutoLock lock( &sm_Lock );

	if ( a_pWidget != NULL && sm_bEnableWidgetCache )
	{
		bool bBumped = false;

		WidgetCacheList & cache = widgetCache();
		for( WidgetCacheList::iterator iWidget = cache.begin();
			iWidget != cache.end(); ++iWidget )
		{
			if ( *iWidget == a_pWidget )
			{
				cache.erase( iWidget );
				cache.push_front( a_pWidget );
				bBumped = true;
				break;
			}
		}

		if (! bBumped )
			cache.push_front( a_pWidget );

		// clear items from the cache..
		// TODO: We should be using the size of each widget to determine how big our cache
		// should be instead of the number of widgets.
		while( cache.size() > MAX_WIDGET_CACHE )
		{
			cache.pop_back();
		}
	}

	// notify all requests of the loaded widget...
	LoadRequestHash::Iterator iEntry = sm_LoadRequestHash.find( a_nKey );
	if ( iEntry.valid() )
	{
		RequestList & observers = (*iEntry).observers;

		RequestList::Iterator iRequest = observers.head();
		while( iRequest.valid() )
		{
			uintptr_t nRequestID = *iRequest;
			iRequest.next();

			// If the request got deleted, it won't be in our hash..
			RequestHash::Iterator iFind = sm_RequestHash.find( nRequestID );
			if ( iFind.valid() )
			{
				Request * pRequest = *iFind;
				pRequest->onLoaded( a_pWidget );
				pRequest->signal();
				pRequest->detach();		// remove from sm_RequestHash now..
			}
		}

		sm_LoadRequestHash.remove( a_nKey );
	}
}

Broker * Broker::findBroker( const WidgetKey & key )
{
	AutoLock lock( &sm_Lock );
	BrokerHashIt find = sm_BrokerHash.find( key );
	if ( find.valid() )
		return( (*find).broker );

	//TRACE( String("Broker not found for widget (%s)", key.string()) );
	return NULL;
}

//----------------------------------------------------------------------------

void Broker::registerWidget( Broker * pBroker, const WidgetKey & key, dword version, bool autoLoad, bool local )
{
	AutoLock lock( &sm_Lock );
	if ( autoLoad )
		sm_AutoLoadList.push( key );

	BrokerHashIt find = sm_BrokerHash.find( key );
	if ( find.valid() )
	{
		// compare versions, only use the newest version
		if ( version > (*find).version || ( local && version == (*find).version ) )
		{
			if ( sm_AutoLoaded.find( key ).valid() )
			{
				delete sm_AutoLoaded[ key ];
				sm_AutoLoaded.remove( key );
			}

			(*find).broker = pBroker;
			(*find).version = version;
		}
	}
	else
	{
		// add widget to the hash table
		WidgetBroker widget;
		widget.broker = pBroker;
		widget.version = version;

		sm_BrokerHash.insert( key, widget );
	}
}

void Broker::unregisterWidget( Broker * pBroker, const WidgetKey & key, dword version )
{
	AutoLock lock( &sm_Lock );

	BrokerHashIt find = sm_BrokerHash.find( key );
	if ( find.valid() )
	{
		if ( (*find).broker == pBroker &&
			(*find).version == version )
		{
			if ( sm_AutoLoaded.find( key ).valid() )
			{
				delete sm_AutoLoaded[ key ];
				sm_AutoLoaded.remove( key );
			}

			sm_BrokerHash.remove( key );
		}
	}
}

void Broker::autoLoadWidgets()
{
	// Parallel autoload.
	//
	// Every key in sm_AutoLoadList is independent: it points at a distinct
	// .aob on disk, and the list is deduplicated by registerWidget.  load()
	// is also already designed to run without sm_Lock — the worker pool
	// (processOneItem) and requestLoad's self-claim path both drop sm_Lock
	// across pBroker->load(); Factory::createWidget has its own internal
	// synchronization (Factory::GetFactoryLock).
	//
	// We therefore (1) snapshot the work list under sm_Lock, (2) release
	// the lock and fan out N workers each calling pBroker->load() on
	// disjoint keys, then (3) re-acquire sm_Lock to publish results into
	// sm_AutoLoaded.  Any nested loadResource() calls inside widget read()
	// take the queue path through the broker pool, which dedupes shared
	// nested deps via sm_LoadRequestHash so we don't double-load.

	std::vector< std::pair<WidgetKey, Broker *> > work;
	{
		AutoLock lock( &sm_Lock );
		work.reserve( (size_t)sm_AutoLoadList.size() );
		for (int i = 0; i < sm_AutoLoadList.size(); i++)
		{
			WidgetKey key = sm_AutoLoadList[ i ];
			BrokerHashIt find = sm_BrokerHash.find( key );
			Broker * pBroker = find.valid() ? (*find).broker : nullptr;
			ASSERT( pBroker );
			if ( pBroker != nullptr )
				work.emplace_back( key, pBroker );
		}
		sm_AutoLoadList.release();
	}

	if ( work.empty() )
		return;

	// Cap the worker count: tiny autoload lists don't benefit from spinning
	// up 16 threads.  hardware_concurrency() returns 0 on weird platforms,
	// so floor at 2.
	unsigned hw = std::thread::hardware_concurrency();
	if ( hw < 2 ) hw = 2;
	const unsigned threadCount = (unsigned)( work.size() < hw ? work.size() : hw );

	std::vector< Widget * > results( work.size(), nullptr );
	std::atomic<size_t> next( 0 );

	auto worker = [&]()
	{
		for ( size_t i = next.fetch_add(1); i < work.size(); i = next.fetch_add(1) )
		{
			results[i] = loadSafely( work[i].second, work[i].first );
		}
	};

	std::vector<std::thread> pool;
	pool.reserve( threadCount );
	for ( unsigned t = 0; t < threadCount; t++ )
		pool.emplace_back( worker );
	for ( auto & th : pool )
		th.join();

	// Publish results in a single locked pass.  Order doesn't matter — each
	// key is unique — so iterating work[] in input order is fine.
	{
		AutoLock lock( &sm_Lock );
		for ( size_t i = 0; i < work.size(); i++ )
		{
			if ( results[i] != nullptr )
				sm_AutoLoaded[ work[i].first ] = results[i];
		}
	}
}

//---------------------------------------------------------------------------------------------------

// Pop the next unclaimed key from sm_LoadList, claim it on the current thread,
// run pBroker->load() with sm_Lock dropped, and publish the result via
// loadNotify (which wakes observers and removes the entry from
// sm_LoadRequestHash).
//
// Returns true if a key was processed, false if the queue was empty.
//
// Used by both:
//   - the steady-state worker loop (Broker::loadingThread)
//   - the work-stealing wait inside Broker::requestLoad when a worker is
//     blocked on a load owned by another worker
//
// Caller must NOT hold sm_Lock.
bool Broker::processOneItem()
{
	AutoLock lock( &sm_Lock );

	while ( sm_LoadList.head().valid() )
	{
		LoadList::Iterator iLoad = sm_LoadList.head();
		WidgetKey nLoadKey = *iLoad;
		sm_LoadList.remove( iLoad );

		LoadRequestHash::Iterator iEntry = sm_LoadRequestHash.find( nLoadKey );
		if (! iEntry.valid() )
			continue;	// already loaded/cancelled by another path
		LoadRequestEntry & entry = *iEntry;
		if ( entry.loaderTID != 0 )
			continue;	// shouldn't happen — sm_LoadList is for unclaimed keys only — but defensive

		// Claim it.
		entry.loaderTID = Thread::getCurrentThreadId();

		lock.release();

		Widget::Ref pWidget = Widget::findWidget( nLoadKey );
		if (! pWidget.valid() )
			pWidget = loadSafely( findBroker( nLoadKey ), nLoadKey );

		lock.set( &sm_Lock );
		loadNotify( nLoadKey, pWidget );
		return true;
	}

	return false;
}

int Broker::loadingThread()
{
	// Mark this OS thread as a Broker pool worker for the rest of its life.
	// This is what inLoadingThread() returns and what requestLoad uses to
	// take the worker-specific code paths (self-claim, cycle detect via
	// loaderTID, work-stealing wait).
	tg_bIsLoadingWorker = true;

	while( sm_bLoadingThreadActive )
	{
		// Auto-reset event: signaled per requestLoad insertion.  Multiple
		// workers contend on the same event — exactly one wakes per signal.
		// 100ms timeout is the backstop for missed signals during shutdown
		// and to retry if work appeared while another worker was busy.
		sm_LoadThreadEvent.wait( 100 );

		// Drain everything we can see.  Each call takes/releases sm_Lock,
		// so other workers can pop their own items in between.
		while ( processOneItem() ) {}
	}

	return 0;
}

Broker::WidgetCacheList & Broker::widgetCache()
{
	static WidgetCacheList cache;
	return cache;
}


//----------------------------------------------------------------------------
// EOF


