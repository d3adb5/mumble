// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_AUDIOOUTPUTSPEECH_H_
#define MUMBLE_MUMBLE_AUDIOOUTPUTSPEECH_H_

#include <speex/speex_jitter.h>
#include <speex/speex_resampler.h>

#include <QtCore/QMutex>

#include "AudioOutputBuffer.h"
#include "AudioOutputCache.h"
#include "AudioPreprocessor.h"
#include "AudioReceiveProcessing.h"
#include "MumbleProtocol.h"
#include "Settings.h"

#include <memory>
#include <mutex>
#include <vector>

class ClientUser;
struct OpusDecoder;
struct DenoiseState;
class WebRTCAudioProcessor;
class DeepFilterNetProcessor;

class AudioOutputSpeech : public AudioOutputBuffer {
private:
	Q_OBJECT
	Q_DISABLE_COPY(AudioOutputSpeech)
protected:
	static std::mutex s_audioCachesMutex;
	static std::vector< AudioOutputCache > s_audioCaches;

	static void invalidateAudioOutputCache(void *maskedIndex);
	static std::size_t storeAudioOutputCache(const Mumble::Protocol::AudioData &audioData);

	unsigned int iAudioBufferSize;
	unsigned int iBufferOffset;
	unsigned int iBufferFilled;
	unsigned int iOutputSize;
	unsigned int iLastConsume;
	unsigned int iFrameSize;
	unsigned int iFrameSizePerChannel;
	unsigned int iSampleRate;
	unsigned int iMixerFreq;
	bool bLastAlive;
	bool bHasTerminator;

	float *fFadeIn;
	float *fFadeOut;
	float *fResamplerBuffer;

	SpeexResamplerState *srs;

	QMutex qmJitter;
	JitterBuffer *jbJitter;
	int iMissCount;
	/// Span (in jitter buffer timestamp units) of the most recent packet inserted
	/// into jbJitter. Used to estimate how much audio the buffered packets amount
	/// to. Protected by qmJitter.
	spx_uint32_t m_lastPacketSpan;

	/// Drops packets from the front of jbJitter while the audio queued up in it
	/// exceeds the delay limit configured in Settings::iMaxIncomingAudioDelayMs.
	/// Does nothing if Settings::bLimitIncomingAudioDelay is disabled.
	/// The caller must hold qmJitter.
	void enforceIncomingDelayLimit();

	OpusDecoder *opusState;

	QList< QByteArray > qlFrames;

	/// Local, receive-side processing of this stream, configured per user
	/// through the owning ClientUser. All state below is only ever touched by
	/// the audio output thread (from prepareSampleBuffer()).
	///
	/// The suppression method the allocated processors currently match;
	/// NoiseCancelOff while none are allocated.
	Settings::NoiseCancel m_localSuppressActive = Settings::NoiseCancelOff;
	/// Speex denoiser per channel (the preprocessor is mono only).
	AudioPreprocessor m_localPreprocessor[2];
	/// Suppression strength currently applied to the Speex denoisers.
	int m_localSpeexStrengthApplied = 0;
#ifdef USE_RNNOISE
	/// RNNoise denoiser state per channel.
	DenoiseState *m_localDenoiser[2] = { nullptr, nullptr };
#endif
#ifdef USE_WEBRTC_AUDIO_PROCESSING
	/// WebRTC noise suppressor, one instance handling all channels interleaved.
	std::unique_ptr< WebRTCAudioProcessor > m_localWebrtc;
#endif
#ifdef USE_DEEPFILTERNET
	/// DeepFilterNet3 suppressor per channel (the model is mono only).
	std::unique_ptr< DeepFilterNetProcessor > m_localDeepFilter[2];
#endif
	/// Noise floor / speech envelope estimate of the decoded stream.
	Mumble::ReceiveProcessing::SnrTracker m_snrTracker;
	/// Smoothed SNR-gated amplification currently applied, in dB.
	float m_localGainDb = 0.0f;

	/// (Re)allocate or release the suppression processors so they match the
	/// requested method. \p mode must already be resolved against the backends
	/// this build provides.
	void setupLocalSuppression(Settings::NoiseCancel mode);
	/// Run the per-user local processing (noise suppression and SNR-gated
	/// amplification) over \p sampleCount interleaved decoded samples,
	/// in 10 ms frames. Also maintains the live SNR measurement.
	void applyLocalProcessing(float *samples, unsigned int sampleCount);

public:
	Mumble::Protocol::audio_context_t m_audioContext;
	Mumble::Protocol::AudioCodec m_codec;
	int iMissedFrames;
	ClientUser *p;

	/// Fetch and decode frames from the jitter buffer. Called in mix().
	///
	/// @param frameCount Number of frames to decode. frame means a bundle of one sample from each channel.
	virtual bool prepareSampleBuffer(unsigned int frameCount) Q_DECL_OVERRIDE;

	void addFrameToBuffer(const Mumble::Protocol::AudioData &audioData);

	/// @param systemMaxBufferSize maximum number of samples the system audio play back may request each time
	AudioOutputSpeech(ClientUser *, unsigned int freq, Mumble::Protocol::AudioCodec codec,
					  unsigned int systemMaxBufferSize);
	~AudioOutputSpeech() Q_DECL_OVERRIDE;
};

#endif // AUDIOOUTPUTSPEECH_H_
