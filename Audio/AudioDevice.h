/*
	AudioDevice.h

	Abtract Interface to a audio device
	(c)2005 Palestar, Richard Lyle
*/

#ifndef AUDIO_DEVICE_H
#define AUDIO_DEVICE_H

//----------------------------------------------------------------------------

#include "Standard/Reference.h"
#include "Factory/FactoryTypes.h"
#include "Audio/AudioBuffer.h"
#include "Math/Vector3.h"
#include "MedusaDll.h"



//----------------------------------------------------------------------------

const ClassKey	CLASS_KEY_AUDIO_DEVICE( LL(3964232654930652497) );
const ClassKey	CLASS_KEY_AUDIO_DEVICE_DS( LL(3964232566486428845) );

//----------------------------------------------------------------------------

class DLL AudioDevice : public Widget
{
public:
	DECLARE_WIDGET_CLASS();

	// Exceptions
	class DeviceFailure {};

	// Types
	typedef Reference< AudioDevice >		Ref;

	class AudioListener
	{
	public:
		virtual void onBeginCapture( int rate, int bits, int channels ) = 0;
		virtual void onCapture( void * pWave, int waveBytes ) = 0; 
		virtual void onEndCapture() = 0;
	};

	// Construction
	AudioDevice();

	// Accessors
	virtual float			volume() const = 0;					// master volume level, 0 (silence) - 1 (full)

	// Mutators
	virtual bool			initialize( void * hWnd, 
								int voices = 32,
								int primaryRate = 44100, 
								int primaryBits = 16, 
								int primaryChannels = 2) = 0;	// initialize this device
	virtual void			release() = 0;						// release this device

	virtual AudioBuffer *	createBuffer() = 0;					// create a new audio buffer
	virtual AudioBuffer *	duplicateBuffer(	
								AudioBuffer * copy ) = 0;		// create a audio buffer copy

	virtual void			setVolume( float volume ) = 0;		// set the master volume level

	virtual bool			beginRecord(
								AudioListener * pListener ) = 0;// begin capturing audio, false if failed
	virtual void			stopRecord() = 0;					// stop recording audio

	// 3D listener state. Camera setup code calls this each render frame so the audio
	// backend can run X3DAudio for any positional buffers. Default no-op for DS — only
	// XAudio2 implements 3D. forward = look-direction, up = head-up vector.
	// NOTE: the velocity arg here is the camera-position-delta velocity which is NOT
	// what you want for Doppler — when the user zooms, the camera moves but no body
	// is actually traveling. Use setListenerVelocity below to feed the listener-body's
	// physical velocity (typically the player ship's worldVelocity).
	virtual void			setListener( const Vector3 & /*pos*/,
										 const Vector3 & /*velocity*/,
										 const Vector3 & /*forward*/,
										 const Vector3 & /*up*/ )			{}

	// Listener body's physical velocity, separate from camera motion. Drives Doppler.
	// Game code calls this each frame with the player ship's worldVelocity (or any
	// world body the listener is conceptually attached to). Default no-op for DS.
	virtual void			setListenerVelocity( const Vector3 & /*velocity*/ )	{}

	// Static
	static AudioDevice *	create();
	static AudioDevice *	create( const char * pClass );
};

//----------------------------------------------------------------------------



#endif

//----------------------------------------------------------------------------
// EOF
