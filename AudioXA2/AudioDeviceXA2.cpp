/*
	AudioDeviceXA2.cpp

	XAudio2 backend for medusa AudioDevice.
	(c)2026 Palestar
*/

#include "Debug/Assert.h"
#include "Debug/Trace.h"
#include "Debug/Log.h"
#include "AudioXA2/AudioDeviceXA2.h"
#include "AudioXA2/AudioBufferXA2.h"

//----------------------------------------------------------------------------

#ifdef HYDRA_STATIC
extern "C" int AUDIO_XA2 = 1;
#endif

//----------------------------------------------------------------------------

IMPLEMENT_FACTORY( AudioDeviceXA2, AudioDevice );

AudioDeviceXA2::AudioDeviceXA2()
	: m_pXAudio2( NULL ), m_pMaster( NULL ),
	  m_MasterChannels( 0 ), m_MasterRate( 0 ), m_SpeakerMask( 0 ),
	  m_b3DReady( false ), m_Volume( 1.0f )
{
	memset( &m_MasterFormat, 0, sizeof(m_MasterFormat) );
	memset( &m_X3DInstance, 0, sizeof(m_X3DInstance) );
	memset( &m_Listener, 0, sizeof(m_Listener) );
	// Sane default listener orientation: forward = +Z, up = +Y (matches DarkSpace's
	// Matrix33 convention: k = forward, j = up). Game code overrides via setListener.
	m_Listener.OrientFront.x = 0.0f; m_Listener.OrientFront.y = 0.0f; m_Listener.OrientFront.z = 1.0f;
	m_Listener.OrientTop.x   = 0.0f; m_Listener.OrientTop.y   = 1.0f; m_Listener.OrientTop.z   = 0.0f;
	LOG_STATUS( "AudioDeviceXA2", "constructed" );
}

AudioDeviceXA2::~AudioDeviceXA2()
{
	release();
	TRACE("AudioDeviceXA2 destroyed");
}

//----------------------------------------------------------------------------

float AudioDeviceXA2::volume() const
{
	return m_Volume;
}

bool AudioDeviceXA2::initialize( void * /*hWnd*/,
								 int voices,
								 int primaryRate,
								 int primaryBits,
								 int primaryChannels )
{
	release();
	LOG_STATUS( "AudioDeviceXA2", "initialize: voices=%d rate=%d bits=%d chan=%d", voices, primaryRate, primaryBits, primaryChannels );

	// XAudio2 2.9 (Windows 8+) is apartment-agnostic and does not require
	// CoInitializeEx — MFC has already CoInited the main thread, and the
	// engine itself is free-threaded. Skip COM init deliberately.

	HRESULT hr = XAudio2Create( &m_pXAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR );
	if ( FAILED( hr ) )
	{
		LOG_WARNING( "AudioDeviceXA2", "XAudio2Create failed hr=0x%08X", hr );
		m_pXAudio2 = NULL;
		return false;
	}

	// Stash the requested primary format. We pass DEFAULT_* to CreateMasteringVoice
	// so XAudio2 picks the device's native rate/channel layout (avoids unnecessary
	// resampling), but keep the requested values around for callers that ask.
	m_MasterFormat.wFormatTag      = WAVE_FORMAT_PCM;
	m_MasterFormat.nChannels       = (WORD)primaryChannels;
	m_MasterFormat.nSamplesPerSec  = (DWORD)primaryRate;
	m_MasterFormat.wBitsPerSample  = (WORD)primaryBits;
	m_MasterFormat.nBlockAlign     = (m_MasterFormat.wBitsPerSample >> 3) * m_MasterFormat.nChannels;
	m_MasterFormat.nAvgBytesPerSec = m_MasterFormat.nBlockAlign * m_MasterFormat.nSamplesPerSec;
	m_MasterFormat.cbSize          = 0;

	hr = m_pXAudio2->CreateMasteringVoice( &m_pMaster,
		XAUDIO2_DEFAULT_CHANNELS,
		XAUDIO2_DEFAULT_SAMPLERATE,
		0,			// flags (reserved, must be 0)
		NULL,		// device id (NULL = system default)
		NULL );		// effect chain
	if ( FAILED( hr ) )
	{
		LOG_WARNING( "AudioDeviceXA2", "CreateMasteringVoice failed hr=0x%08X", hr );
		m_pXAudio2->Release();
		m_pXAudio2 = NULL;
		m_pMaster = NULL;
		return false;
	}

	// 'voices' is advisory under XAudio2 — the engine has no fixed-size voice pool
	// the way DirectSound did. AudioBufferXA2 lazy-creates source voices per buffer.
	// Voice-stealing/priority is handled at the AudioBuffer/Sound layer if needed.
	(void)voices;

	// Apply the master volume that may have been set before initialize() ran.
	m_pMaster->SetVolume( m_Volume );

	// Query the master's actual format — XAudio2 picked the device-native rate/channel layout
	// (Windows speaker properties: 24-bit/96kHz/stereo or whatever the user has configured).
	// We use this for surround-aware pan matrix routing in AudioBufferXA2::applyPanLocked.
	XAUDIO2_VOICE_DETAILS details;
	m_pMaster->GetVoiceDetails( &details );
	m_MasterChannels = details.InputChannels;
	m_MasterRate     = details.InputSampleRate;

	// Channel mask drives X3DAudio's surround placement — KSAUDIO_SPEAKER_STEREO,
	// _5POINT1, _7POINT1_SURROUND, etc. Master has it; query and stash for X3DAudioInitialize.
	hr = m_pMaster->GetChannelMask( &m_SpeakerMask );
	if ( FAILED( hr ) || m_SpeakerMask == 0 )
	{
		LOG_WARNING( "AudioDeviceXA2", "GetChannelMask failed hr=0x%08X — falling back to STEREO mask", hr );
		m_SpeakerMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;	// safe default
	}

	// X3DAudioInitialize: opaque handle; speed of sound in m/s (X3DAUDIO_SPEED_OF_SOUND = 343.5).
	// Failures here only kill 3D audio — 2D pan path still works.
	// HRTF / binaural rendering: not implemented in-engine. Users who want HRTF can enable
	// "Windows Sonic for Headphones" or "Dolby Atmos for Headphones" in Windows Sound Properties
	// (Spatial Sound tab). Windows applies HRTF post-mix to ANY app outputting to a surround
	// layout — and we DO route to surround channels via X3DAudio's output matrix, so it works
	// with no code change required from us.
	hr = X3DAudioInitialize( m_SpeakerMask, X3DAUDIO_SPEED_OF_SOUND, m_X3DInstance );
	if ( FAILED( hr ) )
	{
		LOG_WARNING( "AudioDeviceXA2", "X3DAudioInitialize failed hr=0x%08X — 3D positional disabled, 2D pan only", hr );
		m_b3DReady = false;
	}
	else
	{
		m_b3DReady = true;
	}

	LOG_STATUS( "AudioDeviceXA2", "initialize: ok — master @ %u ch, %u Hz, mask=0x%08X, 3D=%s",
		m_MasterChannels, m_MasterRate, m_SpeakerMask, m_b3DReady ? "on" : "off" );
	return true;
}

void AudioDeviceXA2::release()
{
	if ( m_pXAudio2 != NULL || m_pMaster != NULL )
		LOG_STATUS( "AudioDeviceXA2", "release" );

	// Drain pending reaps BEFORE we kill the engine, so each buffer's DestroyVoice runs
	// while the engine is still alive (DestroyVoice on a freed engine is undefined).
	// Per-call cap (4) is for steady-state stutter avoidance; here we want full drain.
	for ( int safety = 0; safety < 1024; ++safety )
	{
		size_t remaining;
		{
			AutoLock lock( &m_ReapLock );
			remaining = m_PendingReap.size();
		}
		if ( remaining == 0 )
			break;
		drainReap();
	}

	if ( m_pMaster != NULL )
	{
		m_pMaster->DestroyVoice();
		m_pMaster = NULL;
	}

	if ( m_pXAudio2 != NULL )
	{
		m_pXAudio2->Release();
		m_pXAudio2 = NULL;
	}

	memset( &m_MasterFormat, 0, sizeof(m_MasterFormat) );
	m_MasterChannels = 0;
	m_MasterRate = 0;
	m_SpeakerMask = 0;
	m_b3DReady = false;
	memset( &m_X3DInstance, 0, sizeof(m_X3DInstance) );
	memset( &m_Listener, 0, sizeof(m_Listener) );
}

//----------------------------------------------------------------------------

void AudioDeviceXA2::setListener( const Vector3 & pos, const Vector3 & velocity,
								  const Vector3 & forward, const Vector3 & up )
{
	// Drain any pending reap entries on every listener update — main-thread context is
	// guaranteed safe for DestroyVoice (XA2 worker isn't blocked on us). RenderContext::setPosition
	// calls setListener every render frame so this fires ~60Hz, plenty for chatter-rate one-shots.
	drainReap();

	if ( !m_b3DReady )
		return;
	AutoLock lock( &m_Lock );
	m_Listener.Position.x      = pos.x;       m_Listener.Position.y      = pos.y;       m_Listener.Position.z      = pos.z;
	m_Listener.OrientFront.x   = forward.x;   m_Listener.OrientFront.y   = forward.y;   m_Listener.OrientFront.z   = forward.z;
	m_Listener.OrientTop.x     = up.x;        m_Listener.OrientTop.y     = up.y;        m_Listener.OrientTop.z     = up.z;
	m_Listener.pCone           = NULL;	// no cone listener for now (omnidirectional pickup)

	// The velocity arg here is camera-position-delta velocity from RenderContext, which is
	// NOT the listener body's physical velocity (zoom/pan/orbit moves the camera without
	// any in-world body actually moving). We ignore it. The listener-body velocity is fed
	// via setListenerVelocity() from game code that knows about the player ship.
	(void)velocity;
}

void AudioDeviceXA2::setListenerVelocity( const Vector3 & /*velocity*/ )
{
	// Doppler is intentionally disabled (see AudioBufferXA2::applyPositionalLocked comment).
	// Listener velocity is therefore ignored — m_Listener.Velocity stays zero. Kept as a
	// virtual no-op so the abstract AudioDevice interface and any callers (ViewTactical's
	// per-frame feed) compile without churn if doppler is later reintroduced.
}

void AudioDeviceXA2::snapshotListener( X3DAUDIO_LISTENER & out )
{
	AutoLock lock( &m_Lock );
	out = m_Listener;
}

void AudioDeviceXA2::scheduleReap( AudioBufferXA2 * pBuffer )
{
	if ( pBuffer == NULL )
		return;
	// Grab a reference so the buffer stays alive until drainReap processes it. If grabReference
	// returns false, the buffer is already being destroyed elsewhere — don't push.
	if ( !pBuffer->grabReference( 0 ) )
		return;
	AutoLock lock( &m_ReapLock );
	m_PendingReap.push_back( pBuffer );
}

void AudioDeviceXA2::drainReap()
{
	// Cap per-frame destruction work. Each release() blocks on DestroyVoice waiting for
	// callbacks to drain (1-2 ms typical). With heavy SFX (combat, rapid UI clicks) the
	// reap queue can spike to 20+ entries; processing them all in one frame stalls the
	// main thread enough that XA2's audio thread misses its deadline → audible stutter.
	// 4 reaps/frame at 60 fps = 240 voices/sec, more than enough for any plausible scene.
	const size_t kMaxReapsPerCall = 4;

	std::vector<AudioBufferXA2 *> toReap;
	{
		AutoLock lock( &m_ReapLock );
		const size_t take = ( m_PendingReap.size() < kMaxReapsPerCall ) ? m_PendingReap.size() : kMaxReapsPerCall;
		if ( take > 0 )
		{
			toReap.reserve( take );
			for ( size_t i = 0; i < take; ++i )
				toReap.push_back( m_PendingReap[ i ] );
			m_PendingReap.erase( m_PendingReap.begin(), m_PendingReap.begin() + take );
		}
	}
	for ( size_t i = 0; i < toReap.size(); ++i )
	{
		AudioBufferXA2 * pBuf = toReap[ i ];
		// release() destroys the voice (DestroyVoice is safe here — we're on main thread,
		// not inside an XA2 callback) and clears the buffer's self-pin. Then we drop the
		// reap-list grab. If that was the last Ref, the buffer is deleted now.
		pBuf->release();
		pBuf->releaseReference( 0 );
	}
}

AudioBuffer * AudioDeviceXA2::createBuffer()
{
	try {
		return new AudioBufferXA2( this );
	}
	catch ( DeviceFailure ) {}
	return NULL;
}

AudioBuffer * AudioDeviceXA2::duplicateBuffer( AudioBuffer * copy )
{
	try {
		return new AudioBufferXA2( WidgetCast<AudioBufferXA2>( copy ) );
	}
	catch ( DeviceFailure ) {}
	return NULL;
}

void AudioDeviceXA2::setVolume( float volume )
{
	ASSERT( volume >= 0 && volume <= 1 );
	m_Volume = volume;
	if ( m_pMaster != NULL )
		m_pMaster->SetVolume( m_Volume );		// linear amplitude — no millibel conversion
}

bool AudioDeviceXA2::beginRecord( AudioListener * /*pListener*/ )
{
	// Capture is OUT OF SCOPE for the XAudio2 migration.
	return false;
}

void AudioDeviceXA2::stopRecord()
{
	// Capture is OUT OF SCOPE.
}

//----------------------------------------------------------------------------
// EOF
