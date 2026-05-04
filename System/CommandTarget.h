/*
	CommandTarget.h

	This pure base class is used to define a class as a receipient of the message heirarchy,
	which includes messages received from windows and internal messages passed by game objects

	(c)2005 PaleStar Development, Richard Lyle
*/


#ifndef COMMANDTARGET_H
#define COMMANDTARGET_H

#include "Standard/Array.h"
#include "Standard/Types.h"
#include "Standard/Point.h"
#include "System/Messages.h"
#include "MedusaDll.h"

#include <cstdint>

//----------------------------------------------------------------------------

#pragma warning( disable: 4251 )

class DLL CommandTarget
{
public:
	// Types
	struct Message
	{
		// Data
		dword		message;
		// wparam/lparam are pointer-width: HM_MOUSEMOVE etc. stash a
		// PointInt* in here, and 32-bit dword truncated x64 pointers
		// → wild deref in NodeWindow::cursorMove.  Small-integer
		// payloads (key codes, hashes) implicit-promote.
		uintptr_t	wparam;
		uintptr_t	lparam;
		dword		origin;
	};

	// Construction
							CommandTarget();
	virtual					~CommandTarget();

	virtual bool			onMessage( const Message & msg ) = 0;

	// Static
	static int				targetCount();
	static CommandTarget *	target( int n );

	static bool				postWindowMessage( void * hWnd, dword message,
								uintptr_t wparam, uintptr_t lparam );
	static bool				postMessage( const Message & msg );

private:
	// Static Data
	static Array< CommandTarget * >
							s_Targets;			// array of all existing command targets
};

//----------------------------------------------------------------------------



#endif

//----------------------------------------------------------------------------
// EOF
