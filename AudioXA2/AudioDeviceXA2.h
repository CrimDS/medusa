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
#include <x3daudio.h>

#include <vector>

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

	void				setListener( const Vector3 & pos, const Vector3 & velocity,
									 const Vector3 & forward, const Vector3 & up );
	void				setListenerVelocity( const Vector3 & velocity );

	// Internal — used by AudioBufferXA2
	IXAudio2 *			xa2() const					{ return m_pXAudio2; }
	const WAVEFORMATEX &masterFormat() const		{ return m_MasterFormat; }
	UINT32				masterChannels() const		{ return m_MasterChannels; }
	UINT32				masterRate() const			{ return m_MasterRate; }
	bool				is3DReady() const			{ return m_b3DReady; }
	DWORD				speakerMask() const			{ return m_SpeakerMask; }
	const X3DAUDIO_HANDLE & x3dHandle() const		{ return m_X3DInstance; }
	// Snapshot the listener under m_Lock so a buffer's X3DAudioCalculate sees a
	// consistent state even if setListener() races with a positional update.
	void				snapshotListener( X3DAUDIO_LISTENER & out );

	// Deferred reaper for ended one-shot buffers. OnBufferEnd cannot DestroyVoice
	// directly (XA2's DestroyVoice blocks until callbacks drain — calling it from
	// inside a callback self-deadlocks and leaves a dangling callback ptr in the
	// audio graph, causing 00000000-vtable crashes on the next process pass).
	// Buffers schedule themselves for reap; AudioDeviceXA2::drainReap (called from
	// main thread at the top of setListener) does the real DestroyVoice + final
	// Reference release on a thread that XA2 isn't blocked on.
	void				scheduleReap( AudioBufferXA2 * pBuffer );
	void				drainReap();

private:
	// Data
	IXAudio2 *			m_pXAudio2;					// XAudio2 engine
	IXAudio2MasteringVoice *
						m_pMaster;					// mastering voice
	WAVEFORMATEX		m_MasterFormat;				// primary buffer format requested at init
	UINT32				m_MasterChannels;			// actual master channel count (queried after CreateMasteringVoice)
	UINT32				m_MasterRate;				// actual master sample rate (device-native — Windows speaker setting)
	DWORD				m_SpeakerMask;				// KSAUDIO_SPEAKER_* bitmask from master, used by X3DAudio
	bool				m_b3DReady;					// X3DAudioInitialize succeeded
	X3DAUDIO_HANDLE		m_X3DInstance;				// X3DAudio context (opaque 20-byte handle)
	X3DAUDIO_LISTENER	m_Listener;					// current listener state (position + orientation; velocity always zero)
	float				m_Volume;					// current master volume, 0..1
	CriticalSection		m_Lock;						// device lock; also guards m_Listener snapshots

	// Reap list — buffers awaiting deferred DestroyVoice from main thread.
	// Raw pointers + manual grabReference/releaseReference so we don't pull AudioBufferXA2
	// into this header (it includes us back). scheduleReap grabs +1; drainReap releases -1.
	std::vector<AudioBufferXA2 *>	m_PendingReap;
	CriticalSection					m_ReapLock;

	friend class AudioBufferXA2;
};

//----------------------------------------------------------------------------

#endif

//----------------------------------------------------------------------------
// EOF
