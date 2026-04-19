/*
	InterfaceContext.cpp

	The context under which the entire game interface is ran
	(c)2005 Palestar, Richard Lyle
*/

// uncomment to lose profiling information
//#define PROFILE_OFF

#pragma warning( disable : 4146 ) // c:\Projects\Medusa\Standard\String.h(892) : warning C4146: unary minus operator applied to unsigned type, result still unsigned

#define GUI3D_DLL
#include "Debug/Assert.h"
#include "Debug/Profile.h"
#include "File/FileDisk.h"
#include "Draw/Draw.h"
#include "Standard/StringHash.h"
#include "System/Messages.h"
#include "System/Platform.h"
#include "Gui3d/NodeInterfaceClient.h"
#include "Gui3d/NodeWindow.h"
#include "Gui3d/InterfaceContext.h"
#include "World/RenderSnapshot.h"

#if defined(_WIN32)
#include <windows.h>
#endif

//---------------------------------------------------------------------------------------------------

static float GetPercent( qword nCPU, qword nTotalCPU )
{
	float fPercent = 0;
	if ( nTotalCPU > 0 )
		fPercent = ((float)nCPU * 100.0f) / ((float)nTotalCPU);

	return fPercent;
}

//----------------------------------------------------------------------------
//
// ALT+P profile-tree renderer.
//
// Renders the per-thread call tree using Profiler::callSite() (the per-
// (parent, name) accumulator).  Same function called from multiple parents
// shows up under EACH parent with separate stats — see Profile.h.
//
// Layout per row (fixed columns, monospace font assumed):
//   <prefix><tree-connector><name padded to 44>  <ms 7> ms  [<bar 12>] <pct 5>%  <hits 5>/s
//
// Prefix uses ASCII '|', '+', '`', '-' so it works on any font without
// requiring Unicode box-drawing glyphs.  Color codes per-row by % of the
// owning thread: red >50%, orange >25%, yellow >10%, dim grey <1%.
//
//----------------------------------------------------------------------------

#ifndef PROFILE_OFF

// One formatted row of the per-thread profile tree.  Accumulated in a per-
// thread buffer first, then placed by the column layout below — this lets
// us lay several thread blocks side by side instead of running off the
// bottom of the screen.
struct ProfileLine
{
	WideString	text;
	Color		color;
};

struct ProfileRenderCtx
{
	dword			nThread;
	int				nSites;
	qword			nThreadTotal;	// sum of root-CallSite nAvCPU on this thread
	qword			nTotalCPU;		// global Profiler::totalCPU()
	bool *			pVisited;		// per-CallSite visit flag, length nSites
	Array<ProfileLine> * pLines;	// row accumulator (output)
};

static Color profileRowColor( double pctOfThread )
{
	if ( pctOfThread > 50.0 ) return RED;
	if ( pctOfThread > 25.0 ) return ORANGE;
	if ( pctOfThread > 10.0 ) return YELLOW;
	if ( pctOfThread <  1.0 ) return LIGHT_GREY;
	return WHITE;
}

static void renderProfileChildren( ProfileRenderCtx & ctx,
								   const char * pParentName,
								   const wchar * pPrefix )
{
	// Gather CallSites whose parent (interned pointer) matches pParentName.
	// Skip self-loops — proxy WorldContext::update calling itself produces a
	// CallSite where parent == self; following it would recurse forever.
	// Mark them visited anyway so the orphan pass doesn't surface them as
	// "??" rows — the recursive CPU is already included in the parent's
	// inclusive time via rdtsc.
	// Skip rows with zero hits AND zero CPU — stale entries from earlier
	// ticks that didn't fire this second.
	int kids[ 256 ];
	int nKids = 0;
	for ( int j = 0; j < ctx.nSites && nKids < 256; ++j )
	{
		if ( ctx.pVisited[ j ] )
			continue;
		const Profiler::CallSite & cs = Profiler::callSite( ctx.nThread, j );
		if ( cs.pParentName != pParentName )
			continue;
		if ( pParentName != NULL && cs.pName == pParentName )
		{
			ctx.pVisited[ j ] = true;	// self-loop: counted, suppressed
			continue;
		}
		if ( cs.nAvHits == 0 && cs.nAvCPU == 0 )
			continue;
		kids[ nKids++ ] = j;
	}

	// Sort by inclusive CPU desc — hottest path floats to the top so the eye
	// lands on the expensive subtree first.  Insertion sort, fine for ~10s.
	for ( int a = 1; a < nKids; ++a )
	{
		int v = kids[ a ];
		qword vCPU = Profiler::callSite( ctx.nThread, v ).nAvCPU;
		int b = a;
		while ( b > 0 && Profiler::callSite( ctx.nThread, kids[ b - 1 ] ).nAvCPU < vCPU )
		{
			kids[ b ] = kids[ b - 1 ];
			--b;
		}
		kids[ b ] = v;
	}

	const qword nSafeTotal  = ctx.nTotalCPU    > 0 ? ctx.nTotalCPU    : 1;
	const qword nSafeThread = ctx.nThreadTotal > 0 ? ctx.nThreadTotal : 1;

	for ( int i = 0; i < nKids; ++i )
	{
		const int idx = kids[ i ];
		const bool bLast = ( i == nKids - 1 );
		ctx.pVisited[ idx ] = true;

		const Profiler::CallSite & cs = Profiler::callSite( ctx.nThread, idx );

		// ms used per wallclock second by this call site.  nAvCPU is in
		// CPU cycles per second; nTotalCPU is the same units summed across
		// all activity, so the ratio × 1000 gives ms-per-second.
		const double ms        = (double)cs.nAvCPU * 1000.0 / (double)nSafeTotal;
		const double pctThread = 100.0 * (double)cs.nAvCPU / (double)nSafeThread;

		// Build the indented name: prefix + connector + function name.
		// Then truncate or pad to a fixed 44-char column so everything that
		// follows lines up regardless of name length or tree depth.
		WideString label;
		label.format( STR("%s%s %S"),
			pPrefix,
			bLast ? STR("`-") : STR("+-"),
			cs.pName );

		const int kNameWidth = 44;
		if ( label.length() > kNameWidth )
		{
			label.left( kNameWidth - 3 );
			label += STR("...");
		}
		while ( label.length() < kNameWidth )
			label += STR(" ");

		// 12-char bar.  Each cell = ~8.3% of thread time.
		const int kBarWidth = 12;
		int filled = (int)( pctThread * kBarWidth / 100.0 + 0.5 );
		if ( filled > kBarWidth ) filled = kBarWidth;
		if ( filled < 0 )         filled = 0;

		WideString bar;
		for ( int b = 0; b < filled;     ++b ) bar += STR("#");
		for ( int b = filled; b < kBarWidth; ++b ) bar += STR(".");

		ProfileLine & out = ctx.pLines->push();
		out.color = profileRowColor( pctThread );
		out.text.format( STR("%s %7.2f ms  [%s] %5.1f%%  %5u/s"),
			(const wchar *)label, ms,
			(const wchar *)bar, pctThread, cs.nAvHits );

		// Recurse: children get an extended prefix that draws either a
		// vertical pipe (more siblings to come at this level) or spaces
		// (this row was the last sibling so no pipe needed).
		WideString childPrefix;
		childPrefix.format( STR("%s%s"),
			pPrefix,
			bLast ? STR("    ") : STR("|   ") );

		renderProfileChildren( ctx, cs.pName, (const wchar *)childPrefix );
	}
}

#endif

//----------------------------------------------------------------------------

InterfaceContext * InterfaceContext::s_InterfaceContext = NULL;		// current interface context

//-------------------------------------------------------------------------------

IMPLEMENT_RESOURCE_FACTORY( InterfaceContext, Resource );

BEGIN_PROPERTY_LIST( InterfaceContext, Resource );
	ADD_PROPERTY( m_DocumentClass );
	ADD_PROPERTY( m_ActiveScene );
	ADD_PROPERTY( m_Scenes );
	ADD_PROPERTY( m_SceneHash );
	ADD_PROPERTY( m_WindowStyle );
	ADD_PROPERTY( m_Cursor );
END_PROPERTY_LIST();

InterfaceContext::InterfaceContext()
{
	m_DocumentClass = CLASS_KEY( Document );

	m_ActiveScene = -1;
	m_Time = 0.0f;
	m_Bits = 0;
	m_nFPSLimit = 60;
	m_Limitor.setLocked( false );
	m_Limitor.setDuration( 1000 / m_nFPSLimit );

	m_pDocument = NULL;
	
	m_CursorState = POINTER;
	m_CursorPosition = PointInt( 0, 0 );
	m_pHelpWindow = NULL;

	m_MovieCapture = false;
	m_MovieFrame = 0;
	m_DisplayProfile = false;
	m_DisplayWireframe = false;

	// create the root window
	m_pRootWindow = new NodeWindow;
	m_pRootWindow->setContext( this );
	m_MessageQueueInvalid = true;

	// create the document
	createDocument();
	// activate the document
	m_pDocument->onActivate();
}

InterfaceContext::~InterfaceContext()
{
	if ( s_InterfaceContext == this )
		s_InterfaceContext = NULL;

	// release the message queue
	m_MessageQueue.release();

	// release the root window
	m_pRootWindow = NULL;

	// release the document
	deleteDocument();
}

//-------------------------------------------------------------------------------

const int VERSION_051299 = 51299;
const int VERSION_20040204 = 20040204;

bool InterfaceContext::read( const InStream & input )
{
	if (! Resource::read( input ) )
		return false;

	// create the document
	createDocument();
	// activate the document
	m_pDocument->onActivate();
	// set the default cursor state
	setCursorState( POINTER );

	if ( m_Scenes.isValid( m_ActiveScene ) )
	{
		// get the new scene
		Scene * pScene = m_Scenes[ m_ActiveScene ];

		// set the render context frame and position in world space
		m_Context.setPosition( pScene->defaultCameraPosition() );
		m_Context.setFrame( pScene->defaultCameraFrame() );

		// make sure the root of our scene node is detached
		pScene->node()->detachSelf();
		// attach new scene to root window
		m_pRootWindow->detachAllNodes();
		m_pRootWindow->attachNode( pScene->node() );

		// call onActivate for the new windows
		onActivate( m_pRootWindow );
		// build the message queue
		updateMessageQueue();
	}
	return true;
}

//-------------------------------------------------------------------------------

const StringHash	EXIT("EXIT");

// handle incoming messages from the CommandTarget interface, see sceneMessage() for messages from the GUI
bool InterfaceContext::onMessage( const Message & msg )
{
	if ( m_pDocument == NULL || m_pRootWindow == NULL )
		return false;

	// if we have a focus window, it gets first crack at all incoming message..
	if ( m_pFocusWindow.valid() && m_pFocusWindow->onMessage( msg ) )
		return true;
	// send all messages to the document
	if ( m_pDocument->onMessage( msg ) )
		return true;

	switch( msg.message )
	{
	case HM_SYSKEYDOWN:
		switch( msg.wparam )
		{
		case HK_F4:		// standard quit program
#if defined(_WIN32)
			::PostMessage( NULL, WM_QUIT, 0, 0 );
#endif
			return true;
//		case 'M':		// movie capture
//			m_MovieCapture = !m_MovieCapture;
//			return true;
		case 'T':		// screen capture
			capture();
			return true;
#ifdef _DEBUG
		case 'W':		// display wireframe
			m_DisplayWireframe = !m_DisplayWireframe;
			return true;
#endif
		case 'P':		// display profiling information
			m_DisplayProfile = !m_DisplayProfile;
			return true;
		}
		break;
	case IC_EXIT:
#if defined(_WIN32)
		::PostMessage( NULL, WM_QUIT, 0, 0 );
#endif
		return true;
	case IC_SET_SCENE:
		{
			int sceneIndex = findScene( msg.wparam );
			if ( sceneIndex >= 0 )
			{
				setActiveScene( sceneIndex );
				return true;
			}

			TRACE( "InterfaceContext::onMessage, SetScene failed to find scene!" );
		}
		return true;
	}

	// send message to all members of the message queue in reverse order, the last
	// is the top most NodeIntefaceClient
	for(int i=m_MessageQueue.size() - 1;i>=0;i--)
		if ( m_MessageQueue[ i ]->onMessage( msg ) )
			return true;
	
	return false;
}

//----------------------------------------------------------------------------

Platform * InterfaceContext::platform() const
{
	// return back the singleton platform object
	return Platform::sm_pPlatform;
}

const char * InterfaceContext::help() const
{
	if ( m_pHelpWindow != NULL )
		return m_pHelpWindow->help();

	return "";
}

//-------------------------------------------------------------------------------

void InterfaceContext::setDocumentClass( const ClassKey & key )
{
	// set the key
	m_DocumentClass = key;
	// recreate the document
	createDocument();
	// activate the document
	m_pDocument->onActivate();
}

bool InterfaceContext::addScene( const char * pName, Scene::Link pScene, bool activate /*= true*/  )
{
	if (! pScene.valid() )
		return false;

	dword command = StringHash( pName );

	int sceneIndex = m_Scenes.size();
	m_Scenes.push( pScene );
	m_SceneHash[ command ] = sceneIndex;

	// set the context
	setContext( pScene->node(), this );
	if ( activate )
		setActiveScene( sceneIndex );

	return true;
}

void InterfaceContext::setCursor( Material::Link pMaterial )
{
	m_Cursor = pMaterial;
	// reset the state so the hardware cursor will get updated..
	setCursorState( m_CursorState );
}

void InterfaceContext::setCursorState( int state )
{
	// store the cursor state
	m_CursorState = state;

	// disable / enable the cursor motion
	Platform * pPlatform = platform();
	if ( pPlatform != NULL )
	{
		// enable or disable cursor movement based on the state
		pPlatform->enableCursor( m_CursorState != HIDDEN );

		// update the hardware cursor if needed..
		if (! pPlatform->config().bSoftwareCursor )
		{
			if ( m_CursorState != HIDDEN && m_Cursor.valid() )
			{
				int nDiffuse = m_Cursor->findTexture( PrimitiveSurface::DIFFUSE, 0 );
				if ( nDiffuse >= 0 )
					pPlatform->setHardwareCursor( m_Cursor->texture( nDiffuse ).m_pImage, m_CursorState - 1 );

				pPlatform->showHardwareCursor( true );
			}
			else
			{
				pPlatform->showHardwareCursor( false );
			}
		}
	}
}

void InterfaceContext::setActiveScene( int scene )
{
	ASSERT( m_WindowStyle.valid() );

	if ( m_ActiveScene != scene )
	{
		// deactivate the old scene
		onDeactivate(  m_pRootWindow );

		// change the active scene
		m_ActiveScene = scene;
		// get the new scene
		Scene * pScene = m_Scenes[ m_ActiveScene ];

		// set the render context frame and position in world space
		m_Context.setPosition( pScene->defaultCameraPosition() );
		m_Context.setFrame( pScene->defaultCameraFrame() );

		// make sure our node is detached from any parent node
		pScene->node()->detachSelf();
		// attach new scene to root window
		m_pRootWindow->detachAllNodes();
		m_pRootWindow->attachNode( pScene->node() );

		// make sure the context is set
		setContext( m_pRootWindow, this );
		// call onActivate for the new windows
		onActivate( m_pRootWindow );
		// build the message queue
		updateMessageQueue();
	}
}

void InterfaceContext::setTime( float time )
{
	m_Time = time;
}

void InterfaceContext::setBits( dword bits )
{
	m_Bits = bits;
}

void InterfaceContext::setFPSLimit( int a_nFPS )
{
	m_nFPSLimit = a_nFPS;
	if ( m_nFPSLimit > 0 )
		m_Limitor.setDuration( 1000 / m_nFPSLimit );
	else
		m_Limitor.setDuration( 0 );
}

bool InterfaceContext::render()
{
	Platform * pPlatform = platform();
	if ( pPlatform == NULL )
		return false;			// no platform object

	// frame rate may be limited... so wait for timer to expire if needed...
	if ( m_nFPSLimit > 0 )
		m_Limitor.wait();

	// RAII so the timer pops on every exit path — including the early
	// return below when pPlatform->update() signals quit.  The previous
	// PROFILE_START/_END pair leaked the timer on that path.
	PROFILE_FUNCTION();

	// set the current interface context
	s_InterfaceContext = this;

	DisplayDevice * pDisplay = pPlatform->display();
	ASSERT( pDisplay );

	// set the display and audio devices
	m_Context.setDisplay( pDisplay );
	m_Context.setAudio( pPlatform->audio() );

	// lock the document, since changes from another thread during the render and update can cause problems
	m_pDocument->onLock();

	// process messages firstly
	if (! pPlatform->update() )
	{
		// quit program
		m_pDocument->onUnlock();
		return false;
	}

	// pre-update the document
	m_pDocument->onPreUpdate();

	// update the current time
	float deltaTime = 1.0f / 30.0f;
	if ( m_Context.fps() > 0 )
		deltaTime = 1.0f / m_Context.fps();
	m_Time += deltaTime;

	// update the document
	PROFILE_START( "InterfaceContext::document->onUpdate" );
	m_pDocument->onUpdate( deltaTime );
	PROFILE_END();
	// update the interface
	PROFILE_START( "InterfaceContext::onUpdate(root)" );
	onUpdate( m_pRootWindow, deltaTime );
	PROFILE_END();

	// detach any nodes now
	if ( m_DetachQueue.valid() )
	{
		m_MessageQueueInvalid = true;
		while( m_DetachQueue.valid() )
		{
			BaseNode * pNode = *m_DetachQueue;

			// if pParent is null, then node has already been detached
			BaseNode * pParent = pNode->parent();
			if ( pParent != NULL )
				pParent->detachNode( pNode );

			m_DetachQueue.pop();
		}
	}

	// update the message queue if invalid
	if ( m_MessageQueueInvalid )
		updateMessageQueue();

	// lock the display
	pDisplay->lock();
	// calculate the window size
	RectInt window( pDisplay->clientWindow() );
	window -= window.upperLeft();

	// set the root window size and position
	m_pRootWindow->setWindow( window );
	m_pRootWindow->setWindowStyle( m_WindowStyle );

	// set the fill mode
	pDisplay->setFillMode( m_DisplayWireframe ? DisplayDevice::FILL_WIREFRAME : DisplayDevice::FILL_SOLID );

	// setup the context
	m_Context.setTime( m_Time );
	m_Context.setBits( m_Bits );
	m_Context.setProjection( window, PI / 4, 1.0f, 32768.0f );
	
	// render the scene
	m_Context.beginRender( Color(0,0,0), Color(0,0,0) );
	PROFILE_START( "InterfaceContext::scene_render" );
	// Pin the latest published RenderSnapshot for the duration of scene
	// rendering, and announce we're rendering-from-snapshot so that
	// Noun::calculateWorld() and friends consult the snapshot instead of
	// walking the live parent chain (which would race with sim thread
	// mutations on ancestors).  No-op when sim is not pipelined.
	RenderSnapshotRing::instance().pinForFrame();
	setRenderingFromSnapshot( true );
	m_Context.render( m_pRootWindow );
	setRenderingFromSnapshot( false );
	RenderSnapshotRing::instance().releaseFrame();
	PROFILE_END();

	// render the cursor
	PROFILE_START( "InterfaceContext::cursor+tooltip" );
	m_Context.beginScene();
	if ( platform() != NULL && m_CursorState > HIDDEN )
	{
		m_CursorPosition = platform()->cursorPosition();

		if ( platform()->config().bSoftwareCursor )
		{
			// software cursor
			const RectFloat	cursorUV( 0, 0, 1, 1 );
			const PointInt	cursorOffset( CURSOR_SIZE2, CURSOR_SIZE2 );
			const SizeInt	cursorSize( CURSOR_SIZE, CURSOR_SIZE );

			// save then set the time so the correct frame will be rendered
			float fContextTime = m_Context.time();
			m_Context.setTime( m_CursorState - 1 );
			// set the cursor material, then push a window primitive to render it
			Material::push( m_Context, m_Cursor );
			PrimitiveWindow::push( pDisplay, RectInt( m_CursorPosition - cursorOffset, cursorSize ), cursorUV, WHITE );
			// restore the context time
			m_Context.setTime( fContextTime );
		}

		// determine which top most window the cursor is current inside and has help information
		m_pHelpWindow = NULL;
		for(int i=m_MessageQueue.size() - 1;i>=0;i--)
		{
			NodeWindow * pWindow = WidgetCast<NodeWindow>( m_MessageQueue[ i ] );
			if ( pWindow != NULL && pWindow->visible() && strlen( pWindow->help() ) > 0 )
			{
				PointInt localPosition( pWindow->screenToWindow( m_CursorPosition ) );
				RectInt localWindow( pWindow->localWindow() );

				if ( localWindow.inRect( localPosition ) )
				{
					m_pHelpWindow = pWindow;
					break;
				}
			}
		}

		// render the tool tip
		if ( m_sCursorTip.length() > 0 )
		{
			Font * pFont = m_WindowStyle->font();
			ASSERT( pFont );

			SizeInt		tipSize( pFont->size( m_sCursorTip ) );
			PointInt	tipPosition( m_CursorPosition );

			if ( tipPosition.x > ((window.left + window.right) / 2) )
				tipPosition.x -= (tipSize.width + CURSOR_SIZE2);	// left justify
			else
				tipPosition.x += CURSOR_SIZE2;						// right justify
			if ( tipPosition.y > ((window.top + window.bottom) / 2) )
				tipPosition.y -= (tipSize.height + CURSOR_SIZE2);	// top justify
			else
				tipPosition.y += CURSOR_SIZE2;						// bottom justify

			Font::push( pDisplay, pFont, tipPosition, m_sCursorTip, WHITE );
		}
	}
	PROFILE_END();	// close "InterfaceContext::cursor+tooltip"

	// "InterfaceContext::render()" closes via PROFILE_FUNCTION RAII at scope exit.

	// display the frames per second
	PROFILE_LMESSAGE( 0, CharString().format("FPS: %.2f", m_Context.fps()) );

#ifndef PROFILE_OFF
	// display profiling information — see renderProfileChildren above for the
	// per-row layout and the tree-building rationale.  Per-thread blocks are
	// laid out side-by-side in columns so a busy frame doesn't run off the
	// bottom of the screen.
	if ( m_DisplayProfile )
	{
		Font * pFont = m_WindowStyle->font();
		ASSERT( pFont );

		int y = 0;
		const int lineHeight = pFont->size().height;

		// lock the profiler while building a report
		Profiler::lock( true );

		// output user-supplied messages first (FPS, latency, etc.) full-width.
		for(int i=0;i<Profiler::messageCount();i++)
		{
			Vector3 vecPos( 0, y, 0 );
			y += lineHeight;

			WideString sMessage = Profiler::message( i );
			Font::push( pDisplay, pFont, vecPos, sMessage, WHITE );
		}

		const qword nTotalCPU = Profiler::totalCPU();

		// Global summary line, full-width.
		{
			Vector3 vecPos( 0, y, 0 );
			y += lineHeight;

			WideString sSummary;
			sSummary.format(STR("Total CPU: %s cyc/s   load %.1f%%"),
				(const wchar *)FormatNumber<wchar,qword>( nTotalCPU ),
				100.0f * Profiler::CPUused() );
			Font::push( pDisplay, pFont, vecPos, sSummary, WHITE );
		}

		// Build a block of formatted lines per active thread.  Each block is
		// rendered later into a column slot; we collect the strings first so
		// we know each block's height before deciding the layout.
		struct ProfileBlock
		{
			Array<ProfileLine>	lines;
			dword				threadId;
			double				thread_ms;
			double				thread_pct;
		};
		Array<ProfileBlock> blocks;

		for(int k=0;k<Profiler::threadCount();k++)
		{
			const dword nThread = Profiler::thread( k );
			const int   nSites  = Profiler::callSiteCount( nThread );
			if ( nSites == 0 )
				continue;

			// Thread total = sum of root CallSite CPU.  Excludes self-loops
			// since those aren't roots (their parent is themselves).
			qword nThreadTotal = 0;
			for ( int j = 0; j < nSites; ++j )
			{
				const Profiler::CallSite & cs = Profiler::callSite( nThread, j );
				if ( cs.pParentName == NULL )
					nThreadTotal += cs.nAvCPU;
			}
			if ( nThreadTotal == 0 )
				continue;	// idle thread

			const qword nSafeTotal = nTotalCPU > 0 ? nTotalCPU : 1;

			ProfileBlock & blk = blocks.push();
			blk.threadId   = nThread;
			blk.thread_ms  = (double)nThreadTotal * 1000.0 / (double)nSafeTotal;
			blk.thread_pct = 100.0 * (double)nThreadTotal / (double)nSafeTotal;

			// Per-CallSite visit flags so the recursive walker doesn't
			// double-render anything (it's already a DAG by construction
			// since each site is keyed by (parent,name), but we still want
			// belt+braces against any future cycles).
			const int kMaxSites = 256;
			bool visited[ kMaxSites ];
			const int nWalkSites = ( nSites < kMaxSites ) ? nSites : kMaxSites;
			for ( int j = 0; j < nWalkSites; ++j )
				visited[j] = false;

			ProfileRenderCtx ctx;
			ctx.nThread      = nThread;
			ctx.nSites       = nWalkSites;
			ctx.nThreadTotal = nThreadTotal;
			ctx.nTotalCPU    = nTotalCPU;
			ctx.pVisited     = visited;
			ctx.pLines       = &blk.lines;

			renderProfileChildren( ctx, NULL, STR("") );

			// Any CallSites we didn't visit (orphans whose parent isn't on
			// this thread, e.g. cross-DLL data still in flight) get a final
			// pass at depth 0 so nothing silently disappears.
			for ( int j = 0; j < nWalkSites; ++j )
			{
				if ( visited[ j ] )
					continue;
				const Profiler::CallSite & cs = Profiler::callSite( nThread, j );
				if ( cs.nAvHits == 0 && cs.nAvCPU == 0 )
					continue;

				const double ms        = (double)cs.nAvCPU * 1000.0 / (double)nSafeTotal;
				const double pctThread = 100.0 * (double)cs.nAvCPU /
										 (double)( nThreadTotal > 0 ? nThreadTotal : 1 );

				ProfileLine & out = blk.lines.push();
				out.color = GREY;
				out.text.format( STR("?? %-41.41S %7.2f ms  [............] %5.1f%%  %5u/s"),
					cs.pName, ms, pctThread, cs.nAvHits );
			}
		}

		// Column layout.  Measure a worst-case row to size each column;
		// then fit as many columns side-by-side as the screen allows.  The
		// header row spans one line above the column's tree.  When more
		// blocks exist than columns, wrap onto a new "row of columns" below
		// the tallest block in the current row.
		if ( blocks.size() > 0 )
		{
			WideString sample;
			for ( int i = 0; i < 90; ++i )
				sample += STR("M");
			const int colWidth = pFont->size( (const wchar *)sample ).width + 16;
			const int screenW  = pDisplay->clientWindow().width();

			int nColumns = colWidth > 0 ? screenW / colWidth : 1;
			if ( nColumns < 1 )
				nColumns = 1;
			if ( nColumns > blocks.size() )
				nColumns = blocks.size();

			int rowStartY = y;
			int curCol = 0;
			int maxHeightInRow = 0;

			for ( int b = 0; b < blocks.size(); ++b )
			{
				const ProfileBlock & blk = blocks[b];
				const int x = curCol * colWidth;

				// Header
				{
					Vector3 vecPos( x, rowStartY, 0 );
					WideString s;
					s.format(STR("== Thread %u   %.2f ms/s   %.1f%% =="),
						blk.threadId, blk.thread_ms, blk.thread_pct );
					Font::push( pDisplay, pFont, vecPos, s, GOLD );
				}

				int yy = rowStartY + lineHeight;
				for ( int li = 0; li < blk.lines.size(); ++li )
				{
					Vector3 vp( x, yy, 0 );
					yy += lineHeight;
					Font::push( pDisplay, pFont, vp, blk.lines[li].text, blk.lines[li].color );
				}

				const int blockHeight = ( blk.lines.size() + 1 ) * lineHeight;
				if ( blockHeight > maxHeightInRow )
					maxHeightInRow = blockHeight;

				++curCol;
				if ( curCol >= nColumns )
				{
					curCol = 0;
					rowStartY += maxHeightInRow + lineHeight;	// gap between row-bands
					maxHeightInRow = 0;
				}
			}
		}

		// unlock the profiler
		Profiler::lock( false );
	}
#endif
	m_Context.endScene();

	// render all primitives on the stack
	m_Context.endRender();
	// capture movie frames
	if ( m_MovieCapture )
		capture();
	// unlock the display device..
	pDisplay->unlock();

	// "InterfaceContext::render()" Timer pops here via PROFILE_FUNCTION RAII
	// when this scope exits.  Previously a stray PROFILE_END() lived here from
	// the pre-RAII version — it popped the Timer prematurely so every
	// subsequent PROFILE_START (endScene:exec_*, present) was recorded at the
	// render-thread root instead of nested under InterfaceContext::render.

	// unlock the document, we are done with render and update
	m_pDocument->onUnlock();

	return true;
}

void InterfaceContext::capture()
{
	// generate the filename
	while( FileDisk::fileExists( CharString().format(".\\%8.8u.jpg", m_MovieFrame) ) )
		m_MovieFrame++;
	
	DisplayDevice * pDisplay = m_Context.display();
	pDisplay->lock();

	// create the targa
	pDisplay->capture( CharString().format(".\\%8.8u.jpg", m_MovieFrame) );

	pDisplay->unlock();
}

void InterfaceContext::setFocus( BaseNode * pFocus )
{
	if ( pFocus != NULL )
	{
		// recurse down the heirarchy, making sure this node and it's parents are the last children
		BaseNode * pParent = pFocus->parent();
		while( pParent != NULL )
		{
			if ( pParent->child( pParent->childCount() - 1 ) != pFocus )
			{
				// make pFocus the last child of pParent
				if ( pFocus->grabReference() )
				{
					pParent->detachNode( pFocus );
					pParent->attachNode( pFocus );

					pFocus->releaseReference();
				}
			}

			pFocus = pParent;
			pParent = pFocus->parent();
		}
	}

	// rebuild message queue
	setMessageQueueInvalid();
}

bool InterfaceContext::lockFocus( NodeWindow * pFocus )
{
	if ( m_pFocusWindow.valid() && pFocus != m_pFocusWindow )
		return false;
	m_pFocusWindow = pFocus;
	return true;
}

void InterfaceContext::unlockFocus( NodeWindow * pFocus )
{
	if ( pFocus == m_pFocusWindow )
		m_pFocusWindow = NULL;
}

void InterfaceContext::detachNode( BaseNode * pNode )
{
	m_DetachQueue.push( pNode );
}

//----------------------------------------------------------------------------

InterfaceContext * InterfaceContext::interfaceContext()
{
	return s_InterfaceContext;
}

BaseNode * InterfaceContext::findNode( const char * pName )
{
	if ( s_InterfaceContext != NULL )
		if ( s_InterfaceContext->scene() != NULL )
			if ( s_InterfaceContext->scene()->node() != NULL )
				return s_InterfaceContext->scene()->node()->findNode( pName );

	return NULL;
}

//----------------------------------------------------------------------------

void InterfaceContext::updateMessageQueue()
{
	// release the previous message queue
	m_MessageQueue.release();

	// build a list of NodeInterfaceClient objects in the current scene
	// incoming messages are sent to the clients in reverse order
	buildMessageQueue( m_pRootWindow );
	// mark the message queue as valid
	m_MessageQueueInvalid = false;
}

void InterfaceContext::buildMessageQueue( BaseNode * pNode )
{
	if ( WidgetCast<NodeInterfaceClient>( pNode ) )
	{
		NodeInterfaceClient * pInterfaceNode = (NodeInterfaceClient *)pNode;
		if (! pInterfaceNode->enabled() )
			return;		// node not enabled, don't recurse any further

		// add to the queue
		m_MessageQueue.push( pInterfaceNode );
	}

	// recurse into the children
	for(int i=0;i<pNode->childCount();i++)
		buildMessageQueue( pNode->child(i) );
}

void InterfaceContext::createDocument()
{
	deleteDocument();

	// create the document using a factory
	m_pDocument = WidgetCast<Document>( Factory::createWidget( m_DocumentClass ) );
	ASSERT( m_pDocument );

	// assign the document to all the NodeInterfaceClient objects
	for(int i=0;i<m_Scenes.size();i++)
		if ( m_Scenes[i].valid() )
			setContext( m_Scenes[i]->node(), this );
}

void InterfaceContext::deleteDocument()
{
	if ( m_pDocument )
	{
		delete m_pDocument;
		m_pDocument = NULL;
	}
}

//----------------------------------------------------------------------------

void InterfaceContext::onActivate( BaseNode * pNode )
{
	ASSERT( pNode != NULL );

	if ( WidgetCast< NodeInterfaceClient > ( pNode ) )
		((NodeInterfaceClient *)pNode)->onActivate();

	for(int i=0;i<pNode->childCount();i++)
		onActivate( pNode->child(i) );
}

void InterfaceContext::onDeactivate( BaseNode * pNode )
{
	ASSERT( pNode != NULL );

	if ( WidgetCast< NodeInterfaceClient > ( pNode ) )
		((NodeInterfaceClient *)pNode)->onDeactivate();

	for(int i=0;i<pNode->childCount();i++)
		onDeactivate( pNode->child(i) );
}

void InterfaceContext::onUpdate( BaseNode * pNode, float t )
{
	ASSERT( pNode != NULL );

	if ( WidgetCast< NodeInterfaceClient > ( pNode ) )
	{
		if (! ((NodeInterfaceClient *)pNode)->enabled() )
			return;		// node not enabled, don't recurse any further
	
		((NodeInterfaceClient *)pNode)->onUpdate( t );
	}

	for(int i=0;i<pNode->childCount();i++)
		onUpdate( pNode->child(i), t );
}

bool InterfaceContext::onMessage( BaseNode * pNode, const Message & msg )
{
	ASSERT( pNode != NULL );

	if ( WidgetCast< NodeInterfaceClient > ( pNode ) )
		if ( ((NodeInterfaceClient *)pNode)->onMessage( msg ) )
			return true;

	// begin at last child, this is the opposite of the render order because the 
	// last children rendered on the ones on the top
	for(int i=pNode->childCount() - 1;i>=0;i--)
		if ( onMessage( pNode->child(i), msg ) )
			return true;

	return false;
}

void InterfaceContext::setContext( BaseNode * pNode, InterfaceContext * pContext )
{
	ASSERT( pNode != NULL );

	if ( WidgetCast< NodeInterfaceClient > ( pNode ) )
		((NodeInterfaceClient *)pNode)->setContext( pContext );

	for(int i=0;i<pNode->childCount();i++)
		setContext( pNode->child(i), pContext );
}

//----------------------------------------------------------------------------
// EOF
