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
	: m_pXAudio2( NULL ), m_pMaster( NULL ), m_MasterChannels( 0 ), m_MasterRate( 0 ), m_Volume( 1.0f )
{
	memset( &m_MasterFormat, 0, sizeof(m_MasterFormat) );
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

	LOG_STATUS( "AudioDeviceXA2", "initialize: ok — master @ %u ch, %u Hz (Windows device-native; sources auto-resampled)",
		m_MasterChannels, m_MasterRate );
	return true;
}

void AudioDeviceXA2::release()
{
	if ( m_pXAudio2 != NULL || m_pMaster != NULL )
		LOG_STATUS( "AudioDeviceXA2", "release" );

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
