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

// One row of the per-thread profile tree.  Cell data stored separately
// so the renderer can place each column at a fixed pixel X — pixel-perfect
// alignment regardless of font glyph widths.  Was previously a single
// pre-formatted WideString rendered with one Font::push per row (relying
// on a near-monospace font for column alignment); the per-cell variant
// caused streak corruption back when MAX_SRV_DESCRIPTORS was 4096 and the
// 5× SRV allocation rate wrapped the descriptor heap mid-frame.  After the
// bump to 32768 (DisplayDeviceD3D12.h), per-cell rendering is safe again.
// Bar resolution — 12 cells across [###---] makes each cell ~8.3% of
// thread time.  Shared between the row builder and the renderer.
static const int kBarWidth = 12;

struct ProfileLine
{
	WideString	name;		// indented label with tree prefix, pre-padded
	double		ms;
	int			filledBars;	// 0..kBarWidth for the "[####....]" bar
	double		pctThread;
	dword		hits;
	Color		color;
	bool		isOrphan;	// orphan rows render "??" prefix + dashed bar
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

		// kBarWidth = 12 (file-scope).  Each cell = ~8.3% of thread time.
		int filled = (int)( pctThread * kBarWidth / 100.0 + 0.5 );
		if ( filled > kBarWidth ) filled = kBarWidth;
		if ( filled < 0 )         filled = 0;

		// Build the indented name with tree prefix.  No fixed-width padding
		// — the renderer measures actual widths and sizes the name column
		// to fit the longest name in this frame.  Cap at 80 chars to keep
		// pathological names from blowing out the column.
		WideString label;
		label.format( STR("%s%s %S"),
			pPrefix,
			bLast ? STR("`-") : STR("+-"),
			cs.pName );

		const int kNameMax = 80;
		if ( label.length() > kNameMax )
		{
			label.left( kNameMax - 3 );
			label += STR("...");
		}

		// Cell values stored separately; renderer places each column at a
		// fixed pixel X for pixel-perfect alignment with any font.
		ProfileLine & out = ctx.pLines->push();
		out.color      = profileRowColor( pctThread );
		out.name       = label;
		out.ms         = ms;
		out.filledBars = filled;
		out.pctThread  = pctThread;
		out.hits       = cs.nAvHits;
		out.isOrphan   = false;

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
			int sceneIndex = findScene( (dword)msg.wparam );
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
	// Phase D: arm the snapshot-coverage assertion for the duration of scene
	// render.  Internally gated on sm_bPipelinedSimRender — no-op when sim
	// is on the main thread.  Macro is debug-only.
	setAssertSnapshotCoverage( true );
	m_Context.render( m_pRootWindow );
	setAssertSnapshotCoverage( false );
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

		// Filter accumulator — collapsed summary for low-cost threads
		// (typically per-zone NodeZone::simulate workers in a multi-zone
		// focus area).  Rendered as one extra line after the last block.
		int    nFilteredCount = 0;
		double fFilteredMs    = 0.0;
		double fFilteredPct   = 0.0;

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
			const double thread_ms  = (double)nThreadTotal * 1000.0 / (double)nSafeTotal;
			const double thread_pct = 100.0 * (double)nThreadTotal / (double)nSafeTotal;

			// Threshold: skip thread blocks under 1% CPU AND under 1ms/s.
			// These are usually the per-zone NodeZone::simulate workers when
			// the camera focus spans many zones in a constellation — each is
			// idle but eats a full block of screen real estate.  We sum
			// their cost into a summary line at the end of the block list.
			if ( thread_pct < 1.0 && thread_ms < 1.0 )
			{
				nFilteredCount   += 1;
				fFilteredMs      += thread_ms;
				fFilteredPct     += thread_pct;
				continue;
			}

			ProfileBlock & blk = blocks.push();
			blk.threadId   = nThread;
			blk.thread_ms  = thread_ms;
			blk.thread_pct = thread_pct;

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

				// Orphan row — flagged with "?? " prefix, dimmed colour.
				// No padding; renderer auto-sizes name column.
				WideString orphanName;
				orphanName.format( STR("?? %S"), cs.pName );
				ProfileLine & out = blk.lines.push();
				out.color      = GREY;
				out.name       = orphanName;
				out.ms         = ms;
				out.filledBars = 0;
				out.pctThread  = pctThread;
				out.hits       = cs.nAvHits;
				out.isOrphan   = true;
			}
		}

		// Column layout.  Each cell rendered with its own Font::push at a
		// fixed pixel X within the block — pixel-perfect alignment for any
		// font.  Column widths are measured once from worst-case sample
		// strings using the actual font metrics, so M-wide and i-wide glyphs
		// don't drift the columns.  Block width = sum of column widths.
		// Per-row push count = 5 (name, ms, bar, pct, hits); SRV bind cost
		// covered by MAX_SRV_DESCRIPTORS=32768 in DisplayDeviceD3D12.h.
		if ( blocks.size() > 0 )
		{
			// Name column auto-sized to the widest actual label this frame;
			// numerics column-sized by worst-case formatted samples.  Saves
			// horizontal space when names are short, expands when needed.
			int nameColW = 0;
			for ( int b = 0; b < blocks.size(); ++b )
			{
				const ProfileBlock & blk = blocks[b];
				for ( int li = 0; li < blk.lines.size(); ++li )
				{
					const int w = pFont->size(
						(const wchar *)blk.lines[li].name ).width;
					if ( w > nameColW )
						nameColW = w;
				}
			}
			const int msColW   = pFont->size( STR("99999.99 ms") ).width;
			const int barColW  = pFont->size( STR("[############]") ).width;
			const int pctColW  = pFont->size( STR("999.9%") ).width;
			const int hitsColW = pFont->size( STR("999999/s") ).width;

			const int colGap   = pFont->size( STR("  ") ).width;	// 2-space gap between cells
			const int colWidth = nameColW + colGap + msColW + colGap +
								 barColW  + colGap + pctColW + colGap +
								 hitsColW + colGap;
			const int screenW  = pDisplay->clientWindow().width();

			int nColumns = colWidth > 0 ? screenW / colWidth : 1;
			if ( nColumns < 1 )
				nColumns = 1;
			if ( nColumns > blocks.size() )
				nColumns = blocks.size();

			// Per-cell X offsets within a block.  Numeric cells are
			// right-aligned within their column; bar/name are left-aligned.
			const int xName       = 0;
			const int xMsRight    = xName    + nameColW + colGap + msColW;	// right edge for ms
			const int xBar        = xMsRight + colGap;
			const int xPctRight   = xBar     + barColW  + colGap + pctColW;
			const int xHitsRight  = xPctRight + colGap + hitsColW;

			int rowStartY = y;
			int curCol = 0;
			int maxHeightInRow = 0;

			for ( int b = 0; b < blocks.size(); ++b )
			{
				const ProfileBlock & blk = blocks[b];
				const int x = curCol * colWidth;

				// Header — single push, full block width.
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
					const ProfileLine & ln = blk.lines[li];

					// Name (left-aligned, already padded to 44 chars).
					Font::push( pDisplay, pFont, Vector3( x + xName, yy, 0 ),
						ln.name, ln.color );

					// ms — right-aligned within msColW.
					WideString sMs;
					sMs.format( STR("%.2f ms"), ln.ms );
					const int wMs = pFont->size( (const wchar *)sMs ).width;
					Font::push( pDisplay, pFont,
						Vector3( x + xMsRight - wMs, yy, 0 ), sMs, ln.color );

					// Bar (left-aligned).  Orphan rows show "[............]".
					WideString sBar = STR("[");
					if ( ln.isOrphan )
					{
						for ( int bb = 0; bb < kBarWidth; ++bb ) sBar += STR(".");
					}
					else
					{
						for ( int bb = 0; bb < ln.filledBars;            ++bb ) sBar += STR("#");
						for ( int bb = ln.filledBars; bb < kBarWidth;    ++bb ) sBar += STR(".");
					}
					sBar += STR("]");
					Font::push( pDisplay, pFont, Vector3( x + xBar, yy, 0 ),
						sBar, ln.color );

					// pct — right-aligned within pctColW.
					WideString sPct;
					sPct.format( STR("%.1f%%"), ln.pctThread );
					const int wPct = pFont->size( (const wchar *)sPct ).width;
					Font::push( pDisplay, pFont,
						Vector3( x + xPctRight - wPct, yy, 0 ), sPct, ln.color );

					// hits/s — right-aligned within hitsColW.
					WideString sHits;
					sHits.format( STR("%u/s"), ln.hits );
					const int wHits = pFont->size( (const wchar *)sHits ).width;
					Font::push( pDisplay, pFont,
						Vector3( x + xHitsRight - wHits, yy, 0 ), sHits, ln.color );

					yy += lineHeight;
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

			// Summary line for filtered (low-cost) threads.  Renders below
			// the last row of blocks at column 0.
			int summaryY = rowStartY;
			if ( curCol != 0 )
				summaryY += maxHeightInRow + lineHeight;

			if ( nFilteredCount > 0 )
			{
				Vector3 vecPos( 0, summaryY, 0 );
				WideString s;
				s.format(STR("(+%d idle threads omitted: %.2f ms/s total, %.1f%% combined)"),
					nFilteredCount, fFilteredMs, fFilteredPct );
				Font::push( pDisplay, pFont, vecPos, s, LIGHT_GREY );
				summaryY += lineHeight + (lineHeight / 2);
			}

			// Per-frame render-device stats block.  Backends (D3D12, etc.)
			// fill an Array<RenderStat> via getRenderStats() — surfaces SRV
			// bind / CB upload / etc. counters with skip-percentage bars.
			Array<DisplayDevice::RenderStat> deviceStats;
			pDisplay->getRenderStats( deviceStats );
			if ( deviceStats.size() > 0 )
			{
				// Measure column widths for stats: name, value, valueLabel,
				// skipped, bar, pct.
				int statNameW = 0;
				for ( int i = 0; i < deviceStats.size(); ++i )
				{
					WideString sName;
					sName.format( STR("%S"), deviceStats[i].pName );
					const int w = pFont->size( (const wchar *)sName ).width;
					if ( w > statNameW )
						statNameW = w;
				}
				const int statValueW = pFont->size( STR("9999999") ).width;
				const int statLabelW = pFont->size( STR("uploaded") ).width;
				const int statSkipW  = pFont->size( STR("9999999 skip") ).width;
				const int statBarW   = pFont->size( STR("[############]") ).width;
				const int statPctW   = pFont->size( STR("999%") ).width;
				const int gap        = pFont->size( STR("  ") ).width;

				// Column X offsets (right edges for numerics, left for text/bar).
				const int sxName       = 0;
				const int sxValueRight = sxName + statNameW + gap + statValueW;
				const int sxLabel      = sxValueRight + gap;
				const int sxSkipRight  = sxLabel + statLabelW + gap + statSkipW;
				const int sxBar        = sxSkipRight + gap;
				const int sxPctRight   = sxBar + statBarW + gap + statPctW;

				// Header
				{
					Vector3 vp( 0, summaryY, 0 );
					Font::push( pDisplay, pFont, vp, STR("== D3D12 Render Stats =="), GOLD );
					summaryY += lineHeight;
				}

				const int kStatBarWidth = 12;
				for ( int i = 0; i < deviceStats.size(); ++i )
				{
					const DisplayDevice::RenderStat & s = deviceStats[i];

					// Color: red if mostly redundant (we want low skip % for
					// "issued" stats; high skip % is good for cache-hit
					// stats like MatCB — but we colour neutrally here).
					Color color = WHITE;
					if      ( s.fSkipPct >= 75.0f ) color = GREEN;	// strong cache effect
					else if ( s.fSkipPct >= 25.0f ) color = YELLOW;
					else                            color = LIGHT_GREY;

					// Name (left-aligned)
					{
						WideString sName;
						sName.format( STR("%S"), s.pName );
						Font::push( pDisplay, pFont,
							Vector3( sxName, summaryY, 0 ),
							sName, color );
					}
					// Value (right-aligned in its column)
					{
						WideString sVal;
						sVal.format( STR("%u"), s.nValue );
						const int w = pFont->size( (const wchar *)sVal ).width;
						Font::push( pDisplay, pFont,
							Vector3( sxValueRight - w, summaryY, 0 ),
							sVal, color );
					}
					// Value label (left-aligned)
					{
						WideString sLab;
						sLab.format( STR("%S"), s.pValueLabel );
						Font::push( pDisplay, pFont,
							Vector3( sxLabel, summaryY, 0 ),
							sLab, color );
					}
					// Skipped (right-aligned)
					{
						WideString sSkip;
						sSkip.format( STR("%u skip"), s.nSkipped );
						const int w = pFont->size( (const wchar *)sSkip ).width;
						Font::push( pDisplay, pFont,
							Vector3( sxSkipRight - w, summaryY, 0 ),
							sSkip, color );
					}
					// Bar (left-aligned), filled by skip percentage
					{
						int filled = (int)( s.fSkipPct * kStatBarWidth / 100.0f + 0.5f );
						if ( filled > kStatBarWidth ) filled = kStatBarWidth;
						if ( filled < 0 )             filled = 0;
						WideString sBar = STR("[");
						for ( int b = 0; b < filled;          ++b ) sBar += STR("#");
						for ( int b = filled; b < kStatBarWidth; ++b ) sBar += STR(".");
						sBar += STR("]");
						Font::push( pDisplay, pFont,
							Vector3( sxBar, summaryY, 0 ),
							sBar, color );
					}
					// Percent (right-aligned)
					{
						WideString sPct;
						sPct.format( STR("%.0f%%"), s.fSkipPct );
						const int w = pFont->size( (const wchar *)sPct ).width;
						Font::push( pDisplay, pFont,
							Vector3( sxPctRight - w, summaryY, 0 ),
							sPct, color );
					}

					summaryY += lineHeight;
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
