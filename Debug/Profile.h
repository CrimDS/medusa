/*
	Profile.h

	Code profiling system.

	Use the PROFILE_START / PROFILE_END / PROFILE_FUNCTION macros so profiling
	can be removed via PROFILE_OFF.  PROFILE_FUNCTION is RAII (preferred) —
	PROFILE_START/_END is the manual form and must be balanced on every code
	path including early returns and exceptions.

	The string passed to start() must outlive the profiler.  String literals
	always do.  Don't pass dynamic strings (CharString, std::string).

	Names are interned by content (not address): identical literals from
	different DLLs collapse to a single canonical pointer so that pointer
	comparisons (used by the tree builder) work across the World/Render/
	Game DLL boundaries.

	Two parallel data models are maintained:

	1. Profile (legacy, flat) — one entry per (thread, name).  Used by the
	   ProfilerServer wire format and the unit tests.

	2. CallSite (new, per call site) — one entry per (thread, parent_name,
	   name).  Same function called from two parents shows as TWO CallSites,
	   each with its own CPU/hits.  This is what the ALT+P renderer uses
	   so the displayed tree matches the actual call graph.

	(c)2005 Palestar, Richard Lyle
*/

#ifndef DEBUG_PROFILE_H
#define DEBUG_PROFILE_H

#include "Standard/CharString.h"
#include "Standard/HashArray.h"
#include "Standard/Types.h"
#include "Standard/CriticalSection.h"
#include "MedusaDll.h"

//-------------------------------------------------------------------------------

#ifndef PROFILE_OFF
	#define PROFILE_FUNCTION()					ProfileFunction pf( __FUNCTION__ );
	#define PROFILE_START( name )				Profiler::start( name );
	#define PROFILE_END()						Profiler::end();
	#define PROFILE_MESSAGE( string )			Profiler::message( 0, string );
	#define PROFILE_LMESSAGE( line, string )	Profiler::message( line, string );
	#define PROFILE_STOP()						Profiler::stop();
#else
	#define PROFILE_FUNCTION()
	#define PROFILE_START( string )
	#define PROFILE_END()
	#define PROFILE_MESSAGE( string )
	#define PROFILE_LMESSAGE( line, string )
#endif

//-------------------------------------------------------------------------------

#pragma warning( disable: 4251 ) // needs to have dll-interface to be used by clients of class 'Profile'

class DLL Profiler
{
public:
	// Legacy flat profile entry — one per (thread, name).  Aggregates across
	// all parents.  Kept for ProfilerServer wire format and TestProfiler.
	// pParentName is set on first appearance and never updated; for accurate
	// per-call-site parent tracking use CallSite below.
	struct Profile
	{
		Profile();

		const char *		pName;			// interned canonical pointer
		const char *		pParentName;	// first-seen parent (legacy hint)
		qword				nCPU;
		dword				nHits;
		qword				nAvCPU;			// CPU time used per second
		dword				nAvHits;		// number of hits per second
		int					nBytes;			// memory allocated (unused)
	};

	// Per-call-site profile entry — one per (thread, parent_name, name).
	// Same function called from multiple parents → multiple CallSites, each
	// accumulating only the CPU/hits charged to that specific edge.  Used by
	// the ALT+P renderer to build a faithful call tree.
	struct CallSite
	{
		CallSite();

		const char *		pName;			// interned, this site's function name
		const char *		pParentName;	// interned, NULL = root of thread's tree
		qword				nCPU;
		dword				nHits;
		qword				nAvCPU;
		dword				nAvHits;
	};

	// begin profiling — name is interned (canonicalised) so cross-DLL
	// duplicate literals collapse to one entry.
	static void				start( const char * pName );
	// end profiling the section started by the matching start()
	static void				end();
	// clears all sections for the calling thread, should be called before a thread exits
	static void				stop();
	// add/update user message
	static void				message( dword nLine, const char * pMessage );
	// remove message line
	static void				removeMessage( dword nLine );
	// remove all messages
	static void				clearMessages();

	// Intern a name string by content.  Two literals with the same text from
	// different DLLs return the same canonical pointer.  Public so callers
	// (like the renderer) can intern user-supplied labels for comparison.
	static const char *		intern( const char * pName );

	// lock the profiler before calling any of the below functions
	static void				lock( bool bLock );

	// none of the below functions are thread-safe, use the lock() function before calling these functions
	static int				threadCount();		// number of threads being profiles
	static dword			thread( int n );	// get thread_id n

	// Legacy flat profile API.
	static int				profileCount( dword nThread );
	static Profile &		profile( dword nThread, int n );

	// New per-call-site API.  Returns one entry per (parent, name) tuple
	// recorded on this thread.
	static int				callSiteCount( dword nThread );
	static CallSite &		callSite( dword nThread, int n );

	static int				messageCount();
	static const char *		message( int n );
	static qword			totalCPU();			// total number of CPU cycles per second
	static qword			peakCPU();
	static float			CPUused();			// returns the percent of cpu load (0.0f - 1.0f)

private:
	// Types
	struct Timer
	{
		const char *		pName;			// interned
		qword				nTime;
		int					nBytes;
	};

	typedef HashArray< dword, Profile >				ProfileHash;
	typedef HashArray< dword, ProfileHash >			ThreadHash;
	typedef HashArray< qword, CallSite >			CallSiteHash;	// keyed by (parent_ptr<<32 | name_ptr_low32)
	typedef HashArray< dword, CallSiteHash >		ThreadCallSiteHash;
	typedef HashArray< dword, CharString >			MessageHash;
	typedef Hash< dword, Array< Timer > >			StackHash;

	static CriticalSection		sm_Lock;
	static ThreadHash			sm_ThreadHash;
	static ThreadCallSiteHash	sm_ThreadCallSites;
	static MessageHash			sm_MessageHash;
	static StackHash			sm_StackHash;			// timing stack for each thread
	static dword				sm_nLastUpdate;
	static qword				sm_nLastCPU;
	static qword				sm_nTotalCPU;
	static qword				sm_nPeakCPU;
	static float				sm_fLoad;

	static void					updateProfiles();
	static const char *			internLocked( const char * pName );	// caller holds sm_Lock
};

//----------------------------------------------------------------------------

inline Profiler::Profile::Profile() : pName( NULL ), pParentName( NULL ),
	nCPU( 0 ), nHits( 0 ), nAvCPU( 0 ), nAvHits( 0 ), nBytes( 0 )
{}

inline Profiler::CallSite::CallSite() : pName( NULL ), pParentName( NULL ),
	nCPU( 0 ), nHits( 0 ), nAvCPU( 0 ), nAvHits( 0 )
{}

//----------------------------------------------------------------------------

//! This object is used by the PROFILE_FUNCTION macro to profile a function, it constructs an object on the stack which ends
//! the profiling when the object is destroyed.
class DLL ProfileFunction
{
public:
	ProfileFunction( const char * a_pFunctionName )
	{
		Profiler::start( a_pFunctionName );
	}
	~ProfileFunction()
	{
		Profiler::end();
	}
};

//---------------------------------------------------------------------------------------------------


#endif

//----------------------------------------------------------------------------
// EOF
