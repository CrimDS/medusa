/*
	ThreadPool.h

	Minimal fixed-size worker-thread pool with a parallelFor helper.  Built on
	the existing Thread/Event/CriticalSection/Atomic primitives — no new deps,
	no new standards.

	Usage:
		static void myJob( int i, void * ud ) { ... };
		ThreadPool pool;                    // auto-sizes to hw-1 workers
		pool.parallelFor( 0, N, myJob, ud );  // blocks until all N jobs done

	(c)2025 Palestar
*/

#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include "Standard/Types.h"
#include "Standard/Thread.h"
#include "Standard/Event.h"
#include "Standard/CriticalSection.h"
#include "Standard/Array.h"
#include "MedusaDll.h"

//---------------------------------------------------------------------------------------------------

class ThreadPoolWorker;		// fwd

class DLL ThreadPool
{
public:
	// Signature for jobs dispatched by parallelFor.  Each call receives a
	// single range index `i` in [begin, end) along with the user data pointer
	// passed to parallelFor.  The callback runs on a worker thread and must
	// be thread-safe with respect to any shared state it touches.
	typedef void (*JobFunc)( int i, void * userData );

	// nWorkers < 0 picks (hardware threads - 1), clamped to [1, 16].
	// A thread-pool typically wants one fewer worker than cores so the main
	// thread (which dispatches jobs and does other per-frame work) isn't
	// starved by contention.
	explicit ThreadPool( int nWorkers = -1 );
	~ThreadPool();

	// Run fn(i, userData) for every i in [begin, end).  Blocks the caller
	// until all indices have been processed.  When end-begin <= 0 this is a
	// no-op.  When end-begin == 1 we just run the job inline on the calling
	// thread to avoid the dispatch/wake overhead.
	void			parallelFor( int begin, int end, JobFunc fn, void * userData );

	int				workerCount() const { return m_nWorkers; }

	// Returns the current thread's worker index [0..nWorkers-1] if this
	// thread is a ThreadPool worker, or -1 if it's the main thread (or any
	// thread not belonging to a pool).  Used by render code to route shared-
	// state mutations into per-worker scratch slots during parallelFor.
	static int		currentWorkerIndex();

	// Expose the process-wide default pool.  Constructed on first use.
	static ThreadPool & shared();

private:
	friend class ThreadPoolWorker;

	// Per-dispatch state.  Set by parallelFor(), read by workers.  Stable
	// across a dispatch — no locking needed on these once workers are woken
	// because parallelFor is blocking (no overlapping dispatches).
	JobFunc			m_JobFn;
	void *			m_JobUserData;
	int				m_nJobEnd;
	volatile int	m_nJobNext;			// atomically fetch-incremented by workers
	volatile int	m_nPendingJobs;		// decremented when a job completes; last one signals m_AllDone

	Event			m_JobAvailable;		// auto-reset; signaled once per worker we want to wake
	Event			m_AllDone;			// auto-reset; signaled when m_nPendingJobs hits 0

	volatile int	m_bShutdown;		// set by destructor; workers exit when they observe it
	int				m_nWorkers;
	Array<ThreadPoolWorker *> m_Workers;

	// Called by each worker after it finishes a single job.  If it was the
	// last pending job of the current dispatch, wakes the main thread.
	void			onJobComplete();

	// Called by each worker inside its run-loop to take the next job from
	// the dispatch, or return -1 if this dispatch is drained.
	int				grabNextJob();
};

//---------------------------------------------------------------------------------------------------

//! Internal worker thread — runs a loop that waits for jobs and executes them
//! until the pool is destroyed.  Declared here so ThreadPool can own an array
//! of them; not intended for use outside ThreadPool.
class DLL ThreadPoolWorker : public Thread
{
public:
	ThreadPoolWorker( ThreadPool * pool, int nWorkerIndex );
	virtual int		run();

private:
	ThreadPool *	m_pPool;
	int				m_nWorkerIndex;
};

//---------------------------------------------------------------------------------------------------

#endif

//---------------------------------------------------------------------------------------------------
// EOF
