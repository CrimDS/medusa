/*
	AudioBufferXA2.cpp

	XAudio2 source-voice-backed AudioBuffer.
	(c)2026 Palestar
*/

#include "Debug/Assert.h"
#include "Debug/Trace.h"
#include "Debug/Log.h"
#include "Standard/Time.h"
#include "AudioXA2/AudioBufferXA2.h"

//----------------------------------------------------------------------------

IMPLEMENT_FACTORY( AudioBufferXA2, AudioBuffer );

//----------------------------------------------------------------------------

AudioBufferXA2::AudioBufferXA2()
	: m_pDevice( NULL ), m_pVoice( NULL ),
	  m_StreamChunkSize( 0 ),
	  m_CRC( 0 ), m_Size( 0 ), m_Bits( 0 ), m_Channels( 0 ), m_Rate( 0 ),
	  m_Volume( 0.5f ), m_Pan( 0.0f ),
	  m_Playing( false ), m_Looping( false ), m_StreamEnded( false ),
	  m_Locked( false ), m_bSelfPinned( false ),
	  m_bPositional( false ), m_LastPositionalApplyTicks( 0 ), m_FalloffReach( 50.0f )
{
	memset( &m_Format, 0, sizeof(m_Format) );
}

AudioBufferXA2::AudioBufferXA2( AudioDeviceXA2 * pDevice )
	: m_pDevice( pDevice ), m_pVoice( NULL ),
	  m_StreamChunkSize( 0 ),
	  m_CRC( 0 ), m_Size( 0 ), m_Bits( 0 ), m_Channels( 0 ), m_Rate( 0 ),
	  m_Volume( 0.5f ), m_Pan( 0.0f ),
	  m_Playing( false ), m_Looping( false ), m_StreamEnded( false ),
	  m_Locked( false ), m_bSelfPinned( false ),
	  m_bPositional( false ), m_LastPositionalApplyTicks( 0 ), m_FalloffReach( 50.0f )
{
	memset( &m_Format, 0, sizeof(m_Format) );
}

AudioBufferXA2::AudioBufferXA2( const AudioBufferXA2 * pDuplicate )
	: m_pDevice( pDuplicate->m_pDevice ), m_pVoice( NULL ),
	  m_Format( pDuplicate->m_Format ),
	  m_Data( pDuplicate->m_Data ),
	  m_StreamChunkSize( 0 ),
	  m_CRC( pDuplicate->m_CRC ),
	  m_Size( pDuplicate->m_Size ),
	  m_Bits( pDuplicate->m_Bits ),
	  m_Channels( pDuplicate->m_Channels ),
	  m_Rate( pDuplicate->m_Rate ),
	  m_Volume( pDuplicate->m_Volume ),
	  m_Pan( pDuplicate->m_Pan ),
	  m_Playing( false ), m_Looping( false ), m_StreamEnded( false ),
	  m_Locked( false ), m_bSelfPinned( false ),
	  m_bPositional( false ), m_LastPositionalApplyTicks( 0 ), m_FalloffReach( 50.0f )
{}

AudioBufferXA2::~AudioBufferXA2()
{
	release();
}

//----------------------------------------------------------------------------

dword AudioBufferXA2::crc() const		{ return m_CRC; }
int   AudioBufferXA2::size() const		{ return m_Size; }
int   AudioBufferXA2::channels() const	{ return m_Channels; }
int   AudioBufferXA2::bits() const		{ return m_Bits; }
int   AudioBufferXA2::rate() const		{ return m_Rate; }
float AudioBufferXA2::volume() const	{ return m_Volume; }
float AudioBufferXA2::pan() const		{ return m_Pan; }
bool  AudioBufferXA2::playing() const	{ return m_Playing; }
bool  AudioBufferXA2::looping() const	{ return m_Looping; }

int AudioBufferXA2::position() const
{
	if ( m_pVoice == NULL || m_Format.nBlockAlign == 0 )
		return 0;

	XAUDIO2_VOICE_STATE state;
	m_pVoice->GetState( &state, 0 );
	// Match the DS contract: byte offset within the loop-buffer. SamplesPlayed is cumulative,
	// so wrap modulo the buffer length for one-shot/looping playback.
	const int totalSamples = m_Size / m_Format.nBlockAlign;
	const int offsetSamples = totalSamples > 0 ? (int)( state.SamplesPlayed % (UINT64)totalSamples ) : 0;
	return offsetSamples * m_Format.nBlockAlign;
}

//----------------------------------------------------------------------------

bool AudioBufferXA2::initializeBuffer( int size, int channels, int bits, int rate )
{
	m_CRC = 0;
	m_Size = size;
	m_Channels = channels;
	m_Bits = bits;
	m_Rate = rate;

	m_Format.wFormatTag      = WAVE_FORMAT_PCM;
	m_Format.nChannels       = (WORD)channels;
	m_Format.nSamplesPerSec  = (DWORD)rate;
	m_Format.wBitsPerSample  = (WORD)bits;
	m_Format.nBlockAlign     = (m_Format.wBitsPerSample >> 3) * m_Format.nChannels;
	m_Format.nAvgBytesPerSec = m_Format.nBlockAlign * m_Format.nSamplesPerSec;
	m_Format.cbSize          = 0;

	// Allocate the one-shot buffer — Sound::initializeAudioBuffer fills this via lockBuffer.
	// Streaming ring chunks are allocated lazily in play(Stream*) so one-shot Sounds
	// (the common case via Sound::play → duplicateBuffer) don't pay for buffers they never use.
	m_Data.assign( (size_t)size, 0 );
	m_StreamChunkSize = 0;

	return true;
}

bool AudioBufferXA2::release()
{
	stop();

	// Critical: do NOT hold m_Lock while calling DestroyVoice. DestroyVoice blocks
	// until all in-flight callbacks (OnBufferEnd) drain. OnBufferEnd takes m_Lock
	// on entry, so holding m_Lock here would deadlock against the XA2 worker.
	// Capture the voice + clear our pointer under the lock; once m_pVoice is NULL
	// any pending OnBufferEnd will see it and bail (early return at top of callback).
	IXAudio2SourceVoice * pVoiceToDestroy = NULL;
	bool bReleasePin = false;
	{
		AutoLock lock( &m_Lock );
		pVoiceToDestroy = m_pVoice;
		m_pVoice = NULL;
		m_pStream = NULL;
		if ( m_bSelfPinned )
		{
			m_bSelfPinned = false;
			bReleasePin = true;
		}
		m_Data.clear();
		m_Data.shrink_to_fit();
		for ( int i = 0; i < 3; ++i )
		{
			m_StreamChunks[ i ].clear();
			m_StreamChunks[ i ].shrink_to_fit();
		}
		m_StreamChunkSize = 0;
	}

	if ( pVoiceToDestroy != NULL )
		pVoiceToDestroy->DestroyVoice();

	// Release the self-pin LAST — may delete `this` if no other Refs remain.
	// No member access after this line.
	if ( bReleasePin )
		releaseReference( 0 );
	return true;
}

//----------------------------------------------------------------------------

bool AudioBufferXA2::ensureSourceVoice()
{
	if ( m_pVoice != NULL )
		return true;
	if ( m_pDevice == NULL || m_pDevice->xa2() == NULL )
		return false;
	if ( m_Format.nBlockAlign == 0 )
		return false;

	// Allow up to 2x pitch (XAudio2's default MaxFrequencyRatio). DarkSpace's setRate()
	// callers don't pitch faster than that, but bump if the audit ever proves wrong.
	HRESULT hr = m_pDevice->xa2()->CreateSourceVoice(
		&m_pVoice, &m_Format, 0, XAUDIO2_DEFAULT_FREQ_RATIO, this, NULL, NULL );
	if ( FAILED( hr ) )
	{
		LOG_WARNING( "AudioBufferXA2", "CreateSourceVoice failed hr=0x%08X (rate=%d bits=%d ch=%d)",
			hr, m_Rate, m_Bits, m_Channels );
		m_pVoice = NULL;
		return false;
	}

	// Apply any deferred state. Positional spatialization (X3DAudio matrix + doppler)
	// supersedes 2D pan when both are set; only one is meaningful per buffer.
	m_pVoice->SetVolume( m_Volume );
	if ( m_bPositional )
		applyPositionalLocked();
	else if ( m_Pan != 0.0f )
		applyPanLocked();
	return true;
}

bool AudioBufferXA2::play( bool loop /*= false*/ )
{
	AutoLock lock( &m_Lock );

	if ( m_Data.empty() )
		return false;
	if ( !ensureSourceVoice() )
		return false;

	// If a previous play left the voice running or queued, wipe it out so SubmitSourceBuffer
	// starts clean. Stop+Flush is XAudio2's idiomatic "reset the queue" sequence.
	m_pVoice->Stop( 0 );
	m_pVoice->FlushSourceBuffers();

	XAUDIO2_BUFFER buf = {};
	buf.AudioBytes = (UINT32)m_Data.size();
	buf.pAudioData = m_Data.data();
	buf.LoopCount  = loop ? XAUDIO2_LOOP_INFINITE : 0;
	buf.Flags      = XAUDIO2_END_OF_STREAM;
	buf.pContext   = NULL;		// NULL ctx = main one-shot/looping buffer (vs streaming slots)

	HRESULT hr = m_pVoice->SubmitSourceBuffer( &buf );
	if ( FAILED( hr ) )
	{
		LOG_WARNING( "AudioBufferXA2", "play: SubmitSourceBuffer failed hr=0x%08X", hr );
		return false;
	}

	hr = m_pVoice->Start( 0 );
	if ( FAILED( hr ) )
	{
		LOG_WARNING( "AudioBufferXA2", "play: Start failed hr=0x%08X", hr );
		return false;
	}

	m_Looping = loop;
	m_Playing = true;
	m_StreamEnded = false;
	m_pStream = NULL;

	// Self-pin: keep `this` alive across the Sound::play return → caller assignment gap.
	// Without this, dupBuffer's Reference<> dtor inside Sound::play decrements to 0 and
	// triggers ~AudioBufferXA2 *before* the caller's m_pBuffer Ref<> can grab it; voice
	// is destroyed before the audio thread picks up the queued buffer. Mirrors how AudioDS's
	// device-side m_AllocatedVoices Ref<> kept the buffer alive during voice playback.
	if ( !m_bSelfPinned )
	{
		if ( grabReference( 0 ) )
			m_bSelfPinned = true;
	}
	return true;
}

void AudioBufferXA2::submitStreamSlot( int slot, bool primingFromMainThread )
{
	// Caller holds m_Lock when primingFromMainThread; OnBufferEnd holds it too.
	std::vector<BYTE> & chunk = m_StreamChunks[ slot ];
	const int chunkSize = m_StreamChunkSize;

	BYTE * dst = chunk.data();
	int    received = 0;

	if ( m_pStream != NULL && !m_StreamEnded )
	{
		while ( received < chunkSize )
		{
			int bytes = m_pStream->request( dst + received, chunkSize - received );
			if ( bytes <= 0 )
			{
				// Stream exhausted — pad the rest with silence so we don't get a click,
				// then mark ended. Subsequent OnBufferEnd calls will see m_StreamEnded
				// and stop refilling.
				memset( dst + received, 0, (size_t)( chunkSize - received ) );
				m_StreamEnded = true;
				break;
			}
			received += bytes;
		}
	}
	else
	{
		// No stream / already ended — emit silence so the voice can drain cleanly.
		memset( dst, 0, (size_t)chunkSize );
	}

	XAUDIO2_BUFFER buf = {};
	buf.AudioBytes = (UINT32)chunkSize;
	buf.pAudioData = dst;
	buf.LoopCount  = 0;
	buf.Flags      = 0;
	buf.pContext   = encodeStreamSlot( slot );

	if ( m_pVoice != NULL )
		m_pVoice->SubmitSourceBuffer( &buf );

	(void)primingFromMainThread;
}

bool AudioBufferXA2::play( Stream * pStream )
{
	if ( pStream == NULL )
		return false;

	AutoLock lock( &m_Lock );

	if ( m_Size == 0 || m_Format.nBlockAlign == 0 )
		return false;
	if ( !ensureSourceVoice() )
		return false;

	// Lazy-allocate the streaming ring on first play(Stream*). Half-buffer chunking
	// matches what existing Stream subclasses (Music) were designed around.
	if ( m_StreamChunkSize == 0 )
	{
		m_StreamChunkSize = (m_Size / 2) & ~(int)(m_Format.nBlockAlign - 1);	// align down to sample boundary
		if ( m_StreamChunkSize <= 0 )
			m_StreamChunkSize = m_Format.nBlockAlign;
		for ( int i = 0; i < 3; ++i )
			m_StreamChunks[ i ].assign( (size_t)m_StreamChunkSize, 0 );
	}

	m_pVoice->Stop( 0 );
	m_pVoice->FlushSourceBuffers();

	m_pStream = pStream;
	m_pStream->m_pAttached = this;
	m_StreamEnded = false;
	m_Looping = true;	// streaming is conceptually a continuous loop until exhausted

	// Prime all 3 ring slots up front, then start the voice. If the stream is short and
	// returns 0 during priming, m_StreamEnded gets set and the remaining slots fill with silence
	// so the voice still drains cleanly and OnBufferEnd transitions m_Playing → false.
	for ( int i = 0; i < 3; ++i )
		submitStreamSlot( i, /*primingFromMainThread*/ true );

	HRESULT hr = m_pVoice->Start( 0 );
	if ( FAILED( hr ) )
	{
		TRACE( CharString().format("AudioBufferXA2::play(Stream), Start failed hr=0x%08X", hr) );
		m_pStream->m_pAttached = NULL;
		m_pStream = NULL;
		return false;
	}

	m_Playing = true;

	// Self-pin (see one-shot play() comment for rationale).
	if ( !m_bSelfPinned )
	{
		if ( grabReference( 0 ) )
			m_bSelfPinned = true;
	}
	return true;
}

bool AudioBufferXA2::stop()
{
	// Same lock-discipline as release(): capture the voice ptr + clear m_Playing
	// under the lock, then call into XA2 outside the lock. Stop()/FlushSourceBuffers
	// are non-blocking but XA2 may briefly contend with its own internal lock that
	// the worker also holds while invoking our OnBufferEnd, so don't hold m_Lock here.
	IXAudio2SourceVoice * pVoice = NULL;
	bool bReleasePin = false;
	{
		AutoLock lock( &m_Lock );
		pVoice = m_pVoice;
		m_Playing = false;
		if ( m_bSelfPinned )
		{
			m_bSelfPinned = false;
			bReleasePin = true;
		}
		// Don't clear m_pStream here — match AudioBufferDS semantics. ~Stream auto-clears
		// the reverse pointer if the Stream is destroyed first.
	}

	if ( pVoice != NULL )
	{
		pVoice->Stop( 0 );
		pVoice->FlushSourceBuffers();
	}

	// Release the self-pin LAST — it may delete `this` if no other Refs remain.
	// No member access after this line.
	if ( bReleasePin )
		releaseReference( 0 );
	return true;
}

//----------------------------------------------------------------------------

bool AudioBufferXA2::setRate( int rate )
{
	AutoLock lock( &m_Lock );
	m_Rate = rate;
	if ( m_pVoice == NULL || m_Format.nSamplesPerSec == 0 )
		return true;

	const float ratio = (float)rate / (float)m_Format.nSamplesPerSec;
	HRESULT hr = m_pVoice->SetFrequencyRatio( ratio );
	return SUCCEEDED( hr );
}

bool AudioBufferXA2::setVolume( float volume )
{
	ASSERT( volume >= 0 && volume <= 1 );
	AutoLock lock( &m_Lock );
	m_Volume = volume;
	if ( m_pVoice == NULL )
		return true;
	HRESULT hr = m_pVoice->SetVolume( volume );		// linear amplitude — no millibel conversion
	return SUCCEEDED( hr );
}

void AudioBufferXA2::applyPanLocked()
{
	// Caller holds m_Lock. Build a source-channel × dest-channel gain matrix for SetOutputMatrix.
	// XA2 layout: pLevelMatrix[DestinationChannels * SourceChannel + DestinationChannel].
	// We route mono / stereo sources onto FL+FR of whatever speaker layout Windows has chosen
	// (5.1 / 7.1 layouts have FL=ch0, FR=ch1 by convention), zeroing center / LFE / surrounds.
	if ( m_pVoice == NULL || m_pDevice == NULL )
		return;

	const UINT32 srcChans  = m_Format.nChannels;
	const UINT32 destChans = m_pDevice->masterChannels();
	if ( srcChans == 0 || destChans == 0 )
		return;

	// Cap to a sane stack-allocated matrix size (covers 7.1 source × 7.1 dest = 64 floats).
	if ( srcChans > 8 || destChans > 8 )
		return;	// fall through to XA2's default identity routing for unusual layouts

	// Linear pan: p in [-1,+1]. p<0 attenuates right, p>0 attenuates left.
	const float p = m_Pan;
	const float lGain = ( p <= 0.0f ) ? 1.0f : ( 1.0f - p );
	const float rGain = ( p >= 0.0f ) ? 1.0f : ( 1.0f + p );

	float matrix[ 64 ] = { 0 };

	if ( srcChans == 1 )
	{
		// mono → FL/FR with pan blend, all other dest channels silent
		matrix[ 0 ] = lGain;								// mono → FL
		if ( destChans >= 2 )
			matrix[ 1 ] = rGain;							// mono → FR
	}
	else if ( srcChans == 2 )
	{
		// stereo → dest: srcL → FL (with pan-left attenuation), srcR → FR (with pan-right attenuation)
		matrix[ 0 ] = lGain;								// L → FL  (idx = destChans*0 + 0)
		if ( destChans >= 2 )
			matrix[ destChans + 1 ] = rGain;				// R → FR  (idx = destChans*1 + 1)
	}
	else
	{
		// Multi-channel source — out of scope. Leave default identity routing.
		return;
	}

	m_pVoice->SetOutputMatrix( NULL, srcChans, destChans, matrix );
}

bool AudioBufferXA2::setPan( float pan )
{
	ASSERT( pan >= -1 && pan <= 1 );
	AutoLock lock( &m_Lock );
	m_Pan = pan;
	// For positional buffers, the X3DAudio output matrix supersedes 2D pan.
	if ( !m_bPositional )
		applyPanLocked();
	return true;
}

bool AudioBufferXA2::setFalloff( float reachDistance )
{
	AutoLock lock( &m_Lock );
	m_FalloffReach = ( reachDistance > 0.001f ) ? reachDistance : 0.001f;
	if ( m_bPositional && m_pVoice != NULL )
		applyPositionalLocked();
	return true;
}

bool AudioBufferXA2::setPosition( const Vector3 & pos )
{
	AutoLock lock( &m_Lock );
	m_EmitterPos = pos;
	m_bPositional = true;
	if ( m_pVoice != NULL )
		applyPositionalLocked();
	return true;
}

void AudioBufferXA2::applyPositionalLocked()
{
	// Caller holds m_Lock. Runs X3DAudio's MATRIX calculation only — doppler/pitch shift was
	// removed because it never produced perceptually-useful results in DarkSpace's coordinate
	// scale and exposed too many edge cases (camera-zoom shift, chase-cam co-motion, smoothing
	// vs responsiveness trade-offs). Distance attenuation, surround placement and 3D pan all
	// still come from this matrix, just no frequency-ratio modulation.
	if ( !m_bPositional || m_pVoice == NULL || m_pDevice == NULL || !m_pDevice->is3DReady() )
		return;

	// Throttle to ~60 Hz per buffer. NodeSound::render calls setPosition at full frame rate
	// (often 100-200 Hz), but Microsoft's docs explicitly recommend SetOutputMatrix be called
	// "less than 60 Hz" — over-frequent updates outpace XA2's internal matrix-ramp smoothing
	// and can starve the audio worker, producing audible stutter. ~16 ms is plenty since
	// the audio thread quantum is ~21 ms anyway.
	const qword now = Time::ticks();
	const qword tickRate = Time::ticksPerSecond();
	if ( m_LastPositionalApplyTicks > 0 && tickRate > 0 )
	{
		const qword elapsed = now - m_LastPositionalApplyTicks;
		const qword sixteenMs = tickRate / 60;
		if ( elapsed < sixteenMs )
			return;
	}
	m_LastPositionalApplyTicks = now;

	const UINT32 srcChans  = m_Format.nChannels;
	const UINT32 destChans = m_pDevice->masterChannels();
	if ( srcChans == 0 || destChans == 0 )
		return;
	if ( srcChans > 2 || destChans > 8 )
		return;	// fall back to default routing for unusual layouts

	// Linear distance falloff from 1.0 at distance 0 to 0.0 at CurveDistanceScaler.
	// Matches DarkSpace's pre-existing model `volume = clamp(1 - dist*m_Falloff)` when
	// CurveDistanceScaler is set to 1/m_Falloff (== reach distance).
	static const X3DAUDIO_DISTANCE_CURVE_POINT s_LinearPoints[ 2 ] = {
		{ 0.0f, 1.0f },
		{ 1.0f, 0.0f },
	};
	static const X3DAUDIO_DISTANCE_CURVE s_LinearCurve = {
		const_cast< X3DAUDIO_DISTANCE_CURVE_POINT * >( s_LinearPoints ), 2
	};
	// Stereo channel azimuths: L = 270° (3π/2), R = 90° (π/2). 0 = front, clockwise positive.
	static const float kPi = 3.14159265358979323846f;
	static const float s_StereoAzimuths[ 2 ] = { 3.0f * kPi / 2.0f, kPi / 2.0f };

	X3DAUDIO_EMITTER emitter;
	memset( &emitter, 0, sizeof(emitter) );
	emitter.OrientFront.x = 0.0f; emitter.OrientFront.y = 0.0f; emitter.OrientFront.z = 1.0f;
	emitter.OrientTop.x   = 0.0f; emitter.OrientTop.y   = 1.0f; emitter.OrientTop.z   = 0.0f;
	emitter.Position.x    = m_EmitterPos.x;       emitter.Position.y    = m_EmitterPos.y;       emitter.Position.z    = m_EmitterPos.z;
	// Velocity left zero — doppler is intentionally disabled (see applyPositionalLocked comment).
	emitter.ChannelCount  = srcChans;
	emitter.ChannelRadius = 1.0f;
	emitter.pChannelAzimuths    = ( srcChans == 2 ) ? const_cast< float * >( s_StereoAzimuths ) : NULL;
	emitter.pVolumeCurve        = const_cast< X3DAUDIO_DISTANCE_CURVE * >( &s_LinearCurve );
	emitter.CurveDistanceScaler = m_FalloffReach;
	// InnerRadius — within this radius the source is treated as omnidirectional (full volume,
	// no spatial separation). Without it, the player's own ship engine sound (where camera ≈
	// emitter position) gets unstable matrix coefficients as the camera barely rotates, audible
	// as crackling and channel flutter. 15% of reach is a reasonable transition zone.
	emitter.InnerRadius         = m_FalloffReach * 0.15f;
	emitter.InnerRadiusAngle    = X3DAUDIO_PI / 4.0f;	// 45° transition cone
	// DopplerScaler not set (zero default) — feature disabled at the API level too.

	X3DAUDIO_LISTENER listener;
	m_pDevice->snapshotListener( listener );

	float matrix[ 16 ] = { 0 };	// max srcChans (2) * destChans (8) = 16
	X3DAUDIO_DSP_SETTINGS dsp;
	memset( &dsp, 0, sizeof(dsp) );
	dsp.SrcChannelCount     = srcChans;
	dsp.DstChannelCount     = destChans;
	dsp.pMatrixCoefficients = matrix;

	X3DAudioCalculate( m_pDevice->x3dHandle(), &listener, &emitter,
		X3DAUDIO_CALCULATE_MATRIX,
		&dsp );

	m_pVoice->SetOutputMatrix( NULL, srcChans, destChans, matrix );
	// Frequency ratio is owned by setRate() now (no doppler component to stack).
}

//----------------------------------------------------------------------------

void * AudioBufferXA2::lockBuffer()
{
	ASSERT( !m_Locked );
	if ( m_Data.empty() )
		return NULL;
	m_Locked = true;
	return m_Data.data();
}

void AudioBufferXA2::unlockBuffer()
{
	ASSERT( m_Locked );

	// Match AudioBufferDS — produce a CRC over the freshly written waveform.
	m_CRC = 0;
	const dword * pWords = reinterpret_cast<const dword *>( m_Data.data() );
	const int    nWords  = m_Size / 4;
	for ( int i = 0; i < nWords; ++i )
		m_CRC += pWords[ i ];

	m_Locked = false;
}

//----------------------------------------------------------------------------

void STDMETHODCALLTYPE AudioBufferXA2::OnBufferEnd( void * pCtx )
{
	// CRITICAL: do NOT call DestroyVoice or releaseReference (which can trigger ~AudioBufferXA2
	// → release → DestroyVoice) from inside this callback. DestroyVoice waits synchronously for
	// callbacks to drain — calling it from a callback self-deadlocks and leaves the source voice
	// in a half-destroyed state with a dangling callback ptr in the audio graph. Symptom: next
	// process pass crashes at OnVoiceProcessingPassEnd with vtable=0x00000000.
	// Instead we just mark idle and hand off to AudioDeviceXA2::scheduleReap; the device's main-
	// thread drainReap (called from setListener) does DestroyVoice + final ref release safely.
	bool bScheduleReap = false;
	{
		AutoLock lock( &m_Lock );

		if ( !m_Playing || m_pVoice == NULL )
			return;

		if ( pCtx == NULL )
		{
			// One-shot or looping main buffer finished.
			m_Playing = false;
			bScheduleReap = m_bSelfPinned;
		}
		else
		{
			const int slot = decodeStreamSlot( pCtx );
			if ( slot < 0 || slot >= 3 )
				return;

			if ( m_StreamEnded )
			{
				// All slots are draining silence after stream exhaustion. Once XAudio2 reports
				// an empty queue, mark idle. GetState is safe to call from the worker thread.
				XAUDIO2_VOICE_STATE state;
				m_pVoice->GetState( &state, 0 );
				if ( state.BuffersQueued == 0 )
				{
					m_Playing = false;
					bScheduleReap = m_bSelfPinned;
				}
			}
			else
			{
				// Refill this slot from the stream and resubmit.
				submitStreamSlot( slot, /*primingFromMainThread*/ false );
			}
		}
	}

	if ( bScheduleReap && m_pDevice != NULL )
		m_pDevice->scheduleReap( this );
}

//----------------------------------------------------------------------------
// EOF
