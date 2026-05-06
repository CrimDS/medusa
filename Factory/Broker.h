/*
	Broker.h

	This object is used to store / load widgets
	(c)2005 Palestar, Richard Lyle
*/

#ifndef BROKER_H
#define BROKER_H

#include "Standard/Hash.h"
#include "Standard/CriticalSection.h"
#include "Standard/Event.h"
#include "Debug/SafeThread.h"
#include "WidgetKey.h"
#include "ClassKey.h"

#include <cstdint>
#include <list>
#include <vector>

#include "MedusaDll.h"

//----------------------------------------------------------------------------

class Widget;

class DLL Broker
{
public:
	// Types
	class DLL Request
	{
	public:
		// Construction
		Request();
		virtual ~Request();
		// Interface
		virtual bool	onLoaded( Widget * a_pWidget ) { return false; };
		// Accessors
		bool			isAttached() const;						// is this request already attached
		uintptr_t		requestID() const;
		// Mutators
		void			attach();								// register this request with the hash
		void			detach();								// detach this request from the hash
		void			signal();								// signal's the load event
		bool			wait( dword nTimeout = 0xffffffff );	// blocks on the load event until signaled..  returns true on timeout
	private:
		// Data
		bool			m_bAttached;
		Event			m_LoadEvent;
	};
	friend class Request;

	// Data
	static bool			sm_bEnableWidgetCache;

	// When true, ResourceLink::operator>> fires a non-blocking requestLoad on
	// every link it deserializes.  This is what produces actual parallelism
	// in the loader pool — child dep loads fan out across idle workers as a
	// Widget is being read, so by the time the read body's blocking .valid()
	// calls arrive, the deps are already in flight.  Default ON; flip OFF
	// (e.g. for savegame-only contexts) if eager loads of unused links bite.
	static bool			sm_bEagerPrefetch;

	// Mutators
	virtual dword		version( const WidgetKey & key ) = 0;
	virtual dword		size( const WidgetKey & key ) = 0;
	virtual Widget *	load( const WidgetKey & key ) = 0;
	virtual bool		store( Widget * pWidget, dword version, bool autoLoad ) = 0;

	// Static
	static bool			isLoadingThreadActive();
	static bool			startLoadingThread();
	static bool			stopLoadingThread();
	static void			flushCache();

	// Live in-flight count: queued items (sm_LoadList) plus currently active
	// loads (inside pBroker->load).  Used by gating UI like ViewConnectServer
	// to keep the splash visible until the prefetched load tree fully drains
	// before transitioning to gameplay.  Cheap snapshot — locked just long
	// enough to read sm_LoadList.size() and combine with the atomic active
	// counter; safe to call every frame from the render thread.
	static int			pendingLoadCount();

	// Returns true when the calling thread IS one of the broker loader pool
	// workers — i.e. we are inside a Broker pool thread executing
	// pBroker->load (or its recursion).  Used by deserializers (Material::read)
	// to avoid eagerly touching device-side state (D3D primitive factory pool,
	// etc) from the loader thread, which can deadlock against the main thread
	// holding the same pool's lock during render.  Lazy creation on first
	// render is safe.
	static bool			inLoadingThread();

	//! This starts an asynchronous load of the widget in the background, it will invoked the onLoaded() virtual
	//! function in the LoadRequest object once the widget has been loaded...
	static bool			requestLoad( const WidgetKey & a_nKey, const ClassKey & a_nType, Request * a_pRequest, bool a_bBlocking );
	//! This is invoked to notify all waiting requests that the given load has either completed or failed.
	static void			loadNotify( const WidgetKey & a_nKey, Widget * a_pWidget );
	//! This function will return the broker object that has the given widget
	static Broker *		findBroker( const WidgetKey & key );

protected:
	// Static
	static void			registerWidget( Broker * pBroker,
							const WidgetKey & key,
							dword version, bool autoLoad, bool local );
	static void			unregisterWidget( Broker * pBroker,
							const WidgetKey & key,
							dword version );

	static void			autoLoadWidgets();

private:
	// Types
	struct WidgetBroker
	{
		Broker *	broker;
		dword		version;
	};
	class LoadingThread : public SafeThread
	{
	public:
		// Construction
		LoadingThread();
		// Thread interface
		int run()
		{
			return loadingThread();
		}
	};
	friend class LoadingThread;

	typedef Hash< WidgetKey, WidgetBroker >		BrokerHash;
	typedef BrokerHash::Iterator				BrokerHashIt;
	typedef Hash< uintptr_t, Request * >		RequestHash;
	typedef List< uintptr_t >					RequestList;

	// Per-key in-flight gate.  Mediates dedup AND cycle detection:
	//   - observers : list of Request IDs waiting for this load.
	//   - loaderTID : 0 if unclaimed (still in sm_LoadList awaiting a worker),
	//                 otherwise the OS thread id of the worker actively
	//                 running pBroker->load() for this key.  A second
	//                 requestLoad on the same key that arrives on this same
	//                 thread is a cycle.  An arrival from a different thread
	//                 just registers as another observer and waits.
	struct LoadRequestEntry
	{
		LoadRequestEntry() : loaderTID( 0 ) {}
		RequestList		observers;
		dword			loaderTID;
	};
	typedef Hash< WidgetKey, LoadRequestEntry >	LoadRequestHash;
	typedef List< WidgetKey >					LoadList;
	typedef std::list< Reference< Widget > >	WidgetCacheList;
	typedef std::vector< LoadingThread * >		ThreadPool;

	static CriticalSection
						sm_Lock;				// lock for broker static data
	static BrokerHash	sm_BrokerHash;

	static Array< WidgetKey >
						sm_AutoLoadList;			// list of widgets to autoload
	static Hash< WidgetKey, Widget * >
						sm_AutoLoaded;			// hash of loaded widgets

	// Background Loading...
	static bool			sm_bLoadingThreadActive;
	static Event		sm_LoadThreadEvent;		// auto-reset event signaled when new load requests are queued
	static LoadList		sm_LoadList;			// keys queued for the worker pool (loaderTID == 0 in the entry)
	static LoadRequestHash
						sm_LoadRequestHash;		// in-flight gate: observers + loaderTID per key
	static RequestHash	sm_RequestHash;			// hash of all requests by their ID

	static ThreadPool	sm_LoadingPool;			// worker threads (1..N); stable between start/stop

	// Static
	static int			loadingThread();
	static bool			processOneItem();		// pop+claim+load+notify exactly one queued key on the calling worker
	static WidgetCacheList &
						widgetCache();
};

//---------------------------------------------------------------------------------------------------

// (Was: #pragma warning(disable:4311) — needed when requestID returned dword.
// Now uintptr_t-wide, no truncation, warning no longer applicable.)

inline bool Broker::Request::isAttached() const
{
	return m_bAttached;
}

inline uintptr_t Broker::Request::requestID() const
{
	return reinterpret_cast<uintptr_t>( this );
}

inline void Broker::Request::signal()
{
	m_LoadEvent.signal();
}

inline bool Broker::Request::wait( dword nTimeout /*= 0xffffffff*/ )
{
	return m_LoadEvent.wait( nTimeout );
}

//---------------------------------------------------------------------------------------------------


#endif

//----------------------------------------------------------------------------
// EOF


