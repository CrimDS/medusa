/*
	SafeThread.cpp
	(c)2005 Palestar Inc, Richard Lyle
*/

#define DEBUG_DLL
#include "Debug/SafeThread.h"
#include "Debug/Profile.h"

#if defined(_WIN32) || defined(_XBOX)
#include <Excpt.h>
#endif



//----------------------------------------------------------------------------

SafeThread::SafeThread( Priority p /*= STANDARD*/, int stack /*= DEFAULT_STACK_SIZE*/ ) : Thread( p, stack ) 
{}

//----------------------------------------------------------------------------

int SafeThread::runProxy()
{
#if defined(_WIN32) || defined(_XBOX)
#if !defined(_DEBUG) && !defined(DISABLE_SEH_HANDLER)
	__try {
#endif
		// run the thread
		int n = this->run();
		// remove any profiling information for this thread
		Profiler::stop();

		return n;

#if !defined(_DEBUG) && !defined(DISABLE_SEH_HANDLER)
	}
	__except( ProcessException( GetExceptionInformation() ) )
	{}
	// terminate the process
	exit( -1 );
	//return -666;
#endif
#else

	// run this thread
	int n = this->run();
	// remove any profiling information for this thread
	Profiler::stop();

	return n;
#endif

}

//----------------------------------------------------------------------------
//EOF
