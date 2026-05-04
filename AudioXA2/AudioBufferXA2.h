/*
	AudioBufferXA2.h

	XAudio2 source-voice-backed AudioBuffer.
	(c)2026 Palestar
*/

#ifndef AUDIO_BUFFER_XA2_H
#define AUDIO_BUFFER_XA2_H

#include "Standard/CriticalSection.h"
#include "Standard/AutoLock.h"
#include "Audio/AudioBuffer.h"
#include "AudioXA2/AudioDeviceXA2.h"

#include <vector>

//----------------------------------------------------------------------------

class AudioBufferXA2 : public AudioBuffer, public IXAudio2VoiceCallback
{
public:
	DECLARE_WIDGET_CLASS();

	// Types
	typedef Reference< AudioBufferXA2 >	Ref;

	// Construction
	AudioBufferXA2();
	AudioBufferXA2( AudioDeviceXA2 * pDevice );
	AudioBufferXA2( const AudioBufferXA2 * pDuplicate );
	virtual			~AudioBufferXA2();

	// AudioBuffer accessors
	dword			crc() const;
	int				size() const;
	int				channels() const;
	int				bits() const;
	int				rate() const;
	float			volume() const;
	float			pan() const;

	bool			playing() const;
	int				position() const;
	bool			looping() const;

	// AudioBuffer mutators
	bool			initializeBuffer( int size, int channels, int bits, int rate );
	bool			release();

	bool			setRate( int rate );
	bool			setVolume( float volume );
	bool			setPan( float pan );

	bool			play( bool loop = false );
	bool			play( Stream * pStream );
	bool			stop();

	void *			lockBuffer();
	void			unlockBuffer();

	// IXAudio2VoiceCallback — runs on XAudio2's worker thread; refills stream slots and
	// marks one-shots as completed. All other callbacks are no-ops.
	STDMETHOD_( void, OnVoiceProcessingPassStart )( UINT32 /*BytesRequired*/ ) override {}
	STDMETHOD_( void, OnVoiceProcessingPassEnd )() override {}
	STDMETHOD_( void, OnStreamEnd )() override {}
	STDMETHOD_( void, OnBufferStart )( void * /*pCtx*/ ) override {}
	STDMETHOD_( void, OnBufferEnd )( void * pCtx ) override;
	STDMETHOD_( void, OnLoopEnd )( void * /*pCtx*/ ) override {}
	STDMETHOD_( void, OnVoiceError )( void * /*pCtx*/, HRESULT /*Error*/ ) override {}

private:
	// pContext sentinel passed to XA2 buffers — distinguishes one-shot/looping main buffer
	// (NULL) from streaming ring slots (encoded as (slot+1) cast to void*).
	static void *	encodeStreamSlot( int slot )		{ return (void *)(uintptr_t)( slot + 1 ); }
	static int		decodeStreamSlot( void * pCtx )		{ return (int)(uintptr_t)pCtx - 1; }

	bool			ensureSourceVoice();
	void			submitStreamSlot( int slot, bool primingFromMainThread );
	void			applyPanLocked();

	// Data
	AudioDeviceXA2 *	m_pDevice;					// our device (raw — device outlives us)
	IXAudio2SourceVoice *
						m_pVoice;					// source voice (lazy-created on first play)
	WAVEFORMATEX		m_Format;					// PCM format

	std::vector<BYTE>	m_Data;						// owned PCM bytes for one-shot/looping playback
	std::vector<BYTE>	m_StreamChunks[ 3 ];		// ring of refillable buffers for play(Stream*)
	int					m_StreamChunkSize;			// bytes per stream chunk

	dword			m_CRC;
	int				m_Size;
	int				m_Bits;
	int				m_Channels;
	int				m_Rate;

	float			m_Volume;
	float			m_Pan;

	volatile bool	m_Playing;
	bool			m_Looping;
	volatile bool	m_StreamEnded;					// set when pStream->request returned 0

	bool			m_Locked;
	bool			m_bSelfPinned;					// holds an extra refcount on `this` while a voice is active —
													// mirrors AudioDS's device-side voice→buffer pin so a buffer
													// returned from Sound::play outlives the transient Reference<>
													// during the caller's assignment.

	CriticalSection	m_Lock;							// guards m_pVoice / m_Playing / m_pStream against OnBufferEnd
};

//----------------------------------------------------------------------------

#endif

//----------------------------------------------------------------------------
// EOF
