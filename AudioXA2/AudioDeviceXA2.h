/*
	AudioDeviceXA2.h

	XAudio2 backend for medusa AudioDevice.
	Scaffold only — virtuals stubbed to return false / no-op until Step 4.
	(c)2026 Palestar
*/

#ifndef AUDIO_DEVICE_XA2_H
#define AUDIO_DEVICE_XA2_H

//----------------------------------------------------------------------------

#include "Standard/CriticalSection.h"
#include "Audio/AudioDevice.h"

#include <xaudio2.h>

//----------------------------------------------------------------------------

class AudioBufferXA2;	// forward declare

class AudioDeviceXA2 : public AudioDevice
{
public:
	DECLARE_WIDGET_CLASS();

	// Types
	typedef Reference< AudioDeviceXA2 >		Ref;

	// Construction
						AudioDeviceXA2();
	virtual				~AudioDeviceXA2();

	// AudioDevice interface
	float				volume() const;

	bool				initialize( void * hWnd,
							int voices = 32,
							int primaryRate = 44100,
							int primaryBits = 16,
							int primaryChannels = 2);
	void				release();

	AudioBuffer *		createBuffer();
	AudioBuffer *		duplicateBuffer( AudioBuffer * copy );

	void				setVolume( float volume );

	bool				beginRecord( AudioListener * pListener );
	void				stopRecord();

	// Internal — used by AudioBufferXA2
	IXAudio2 *			xa2() const					{ return m_pXAudio2; }
	const WAVEFORMATEX &masterFormat() const		{ return m_MasterFormat; }
	UINT32				masterChannels() const		{ return m_MasterChannels; }
	UINT32				masterRate() const			{ return m_MasterRate; }

private:
	// Data
	IXAudio2 *			m_pXAudio2;					// XAudio2 engine
	IXAudio2MasteringVoice *
						m_pMaster;					// mastering voice
	WAVEFORMATEX		m_MasterFormat;				// primary buffer format requested at init
	UINT32				m_MasterChannels;			// actual master channel count (queried after CreateMasteringVoice)
	UINT32				m_MasterRate;				// actual master sample rate (device-native — Windows speaker setting)
	float				m_Volume;					// current master volume, 0..1
	CriticalSection		m_Lock;						// device lock

	friend class AudioBufferXA2;
};

//----------------------------------------------------------------------------

#endif

//----------------------------------------------------------------------------
// EOF
