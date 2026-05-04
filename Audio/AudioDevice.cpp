/*
	AudioDevice.cpp
	(c)2005 Palestar, Richard Lyle
*/

#define AUDIO_DLL
#include "Standard/Library.h"
#include "Audio/Sound.h"
#include "Audio/AudioDevice.h"
#include "Factory/FactoryTypes.h"
#include "Debug/Log.h"

#include <string.h>

//----------------------------------------------------------------------------

// Set USE_XAUDIO2 to 1 to prefer the XAudio2 backend over DirectSound. When on,
// LoadAudioLibs also pulls in AudioXA2.dll, and create()/create(name) redirect
// "AudioDeviceDS" lookups to "AudioDeviceXA2" so the toggle takes effect at all
// existing call sites (PlatformWin, MusicPort, ScenePort) without per-site edits.
#define USE_XAUDIO2 1

//----------------------------------------------------------------------------

IMPLEMENT_ABSTRACT_FACTORY( AudioDevice, Widget );
IMPLEMENT_ABSTRACT_FACTORY( AudioBuffer, Widget );

AudioDevice::AudioDevice()
{}

//----------------------------------------------------------------------------

static void LoadAudioLibs()
{
// Load all available audio devices
#ifdef _DEBUG
#if USE_XAUDIO2
	static Library LIB_AUDIOXA2( ("AudioXA2D.dll") );		// XAudio2
#endif
	static Library LIB_AUDIODS( ("AudioDSD.dll") );			// DirectSound
	static Library LIB_AUDIOPSX( ("AudioPSXD.dll") );		// PlayStation 2
	static Library LIB_AUDIOXB( ("AudioXBD.dll") );			// XBOX
#else
#if USE_XAUDIO2
	static Library LIB_AUDIOXA2( ("AudioXA2.dll") );		// XAudio2
#endif
	static Library LIB_AUDIODS( ("AudioDS.dll") );			// DirectSound
	static Library LIB_AUDIOPSX( ("AudioPSX.dll") );			// PlayStation 2
	static Library LIB_AUDIOXB( ("AudioXB.dll") );			// XBOX
#endif
}

//----------------------------------------------------------------------------

AudioDevice * AudioDevice::create()
{
	LoadAudioLibs();
#if USE_XAUDIO2
	if ( AudioDevice * pXA2 = WidgetCast<AudioDevice>(Factory::createNamedWidget( "AudioDeviceXA2" )) )
	{
		LOG_STATUS( "AudioDevice", "create() -> AudioDeviceXA2 (USE_XAUDIO2 on)" );
		return pXA2;
	}
	LOG_WARNING( "AudioDevice", "create(): USE_XAUDIO2 on but AudioDeviceXA2 factory missing — DLL didn't load?" );
#endif
	if ( Factory::typeCount( classKey() ) > 0 )
	{
		LOG_STATUS( "AudioDevice", "create() -> first registered AudioDevice factory" );
		return WidgetCast<AudioDevice>(Factory::createWidget( Factory::type( classKey(), 0 ) ));
	}

	LOG_WARNING( "AudioDevice", "create(): no audio device factory available" );
	return NULL;
}

AudioDevice * AudioDevice::create( const char * pClass )
{
	LoadAudioLibs();
#if USE_XAUDIO2
	// Redirect legacy "AudioDeviceDS" callers to the XA2 backend when the toggle is on.
	if ( pClass != NULL && strcmp( pClass, "AudioDeviceDS" ) == 0 )
	{
		if ( AudioDevice * pXA2 = WidgetCast<AudioDevice>(Factory::createNamedWidget( "AudioDeviceXA2" )) )
		{
			LOG_STATUS( "AudioDevice", "create(\"AudioDeviceDS\") redirected -> AudioDeviceXA2" );
			return pXA2;
		}
		LOG_WARNING( "AudioDevice", "create(\"AudioDeviceDS\"): XA2 toggle on but XA2 factory missing — falling through to DS" );
	}
#endif
	AudioDevice * p = WidgetCast<AudioDevice>(Factory::createNamedWidget( pClass ));
	LOG_STATUS( "AudioDevice", "create(\"%s\") -> %s", pClass ? pClass : "(null)", p ? "ok" : "NULL" );
	return p;
}

//----------------------------------------------------------------------------
// EOF

