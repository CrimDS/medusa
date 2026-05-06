/*
	Jukebox.cpp
	(c)2005 Palestar Inc, Richard Lyle
*/

#define AUDIO_DLL
#include "Audio/Jukebox.h"
#include "Standard/Library.h"

//----------------------------------------------------------------------------

IMPLEMENT_ABSTRACT_FACTORY( Jukebox, Widget );

//----------------------------------------------------------------------------

// Mirror of AudioDevice.cpp's LoadAudioLibs.  AudioDS.dll holds the only Jukebox
// implementation (JukeboxDS, which uses DirectShow rather than DirectSound for
// playback — independent of the AudioDevice backend).  AudioXA2.dll is loaded
// alongside so a future XA2-side Jukebox can register its factory without an
// edit here; today it has no Jukebox class and is harmless to load.
static void LoadAudioLibs()
{
#ifdef _DEBUG
	static Library LIB_AUDIOXA2( ("AudioXA2D.dll") );		// XAudio2
	static Library LIB_AUDIODS( ("AudioDSD.dll") );			// DirectSound + DirectShow Jukebox
	static Library LIB_AUDIOPSX( ("AudioPSXD.dll") );		// PlayStation 2
	static Library LIB_AUDIOXB( ("AudioXBD.dll") );			// XBOX
#else
	static Library LIB_AUDIOXA2( ("AudioXA2.dll") );		// XAudio2
	static Library LIB_AUDIODS( ("AudioDS.dll") );			// DirectSound + DirectShow Jukebox
	static Library LIB_AUDIOPSX( ("AudioPSX.dll") );		// PlayStation 2
	static Library LIB_AUDIOXB( ("AudioXB.dll") );			// XBOX
#endif
}

Jukebox * Jukebox::create()
{
	LoadAudioLibs();
	return WidgetCast<Jukebox>( Factory::createNamedWidget( "Jukebox" ) );
}

//----------------------------------------------------------------------------
//EOF
