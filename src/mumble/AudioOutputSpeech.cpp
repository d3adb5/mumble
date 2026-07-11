// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "AudioOutputSpeech.h"

#include "Audio.h"
#include "AudioInputAmplification.h"
#include "ClientUser.h"
#include "PacketDataStream.h"
#include "Utils.h"
#include "Global.h"

#include <opus.h>

#ifdef USE_RNNOISE
extern "C" {
#	include "rnnoise.h"
}
#endif

#ifdef USE_WEBRTC_AUDIO_PROCESSING
#	include "WebRTCAudioProcessing.h"
#endif

#ifdef USE_DEEPFILTERNET
#	include "DeepFilterNet.h"
#	include "DeepFilterNetProcessing.h"
#endif

#include <algorithm>
#include <cassert>
#include <cmath>

// The Qt-free receive-processing helper mirrors Settings::NoiseCancel value for
// value; make sure the two never drift apart.
using LocalSuppression = Mumble::ReceiveProcessing::Suppression;
static_assert(static_cast< int >(LocalSuppression::Off) == Settings::NoiseCancelOff);
static_assert(static_cast< int >(LocalSuppression::Speex) == Settings::NoiseCancelSpeex);
static_assert(static_cast< int >(LocalSuppression::RNN) == Settings::NoiseCancelRNN);
static_assert(static_cast< int >(LocalSuppression::Both) == Settings::NoiseCancelBoth);
static_assert(static_cast< int >(LocalSuppression::WebRTC) == Settings::NoiseCancelWebRTC);
static_assert(static_cast< int >(LocalSuppression::DeepFilter) == Settings::NoiseCancelDeepFilter);

/// The suppression backends this build provides for the receive side.
static constexpr Mumble::ReceiveProcessing::SuppressionSupport localSuppressionSupport() {
	Mumble::ReceiveProcessing::SuppressionSupport support;
#ifdef USE_RNNOISE
	support.rnnoise = true;
#endif
#ifdef USE_WEBRTC_AUDIO_PROCESSING
	support.webrtc = true;
#endif
#ifdef USE_DEEPFILTERNET
	support.deepfilter = true;
#endif
	return support;
}

/// Scale of the float samples the suppressors' 16 bit PCM view uses.
static constexpr float LOCAL_PCM_SCALE = 32768.0f;

static short clampToShort(float v) {
	return static_cast< short >(std::clamp(v, -32768.0f, 32767.0f));
}

std::mutex AudioOutputSpeech::s_audioCachesMutex;
std::vector< AudioOutputCache > AudioOutputSpeech::s_audioCaches(100);

void AudioOutputSpeech::invalidateAudioOutputCache(void *maskedIndex) {
	// The given "pointer" actually is to be understood as an index
	const std::size_t index = reinterpret_cast< std::size_t >(maskedIndex) - 1;

	std::lock_guard< std::mutex > lock(s_audioCachesMutex);

	if (index < s_audioCaches.size()) {
		s_audioCaches[index].clear();
	}
}

std::size_t AudioOutputSpeech::storeAudioOutputCache(const Mumble::Protocol::AudioData &audioData) {
	std::lock_guard< std::mutex > lock(s_audioCachesMutex);

	// Find free spot in s_audioCaches
	auto it = std::find_if(s_audioCaches.begin(), s_audioCaches.end(),
						   [](const AudioOutputCache &chunk) { return !chunk.isValid(); });

	if (it != s_audioCaches.end()) {
		// Write audio data to that free (currently unused) chunk
		it->loadFrom(audioData);

		return static_cast< std::size_t >(std::distance(s_audioCaches.begin(), it));
	} else {
		// The list of audio chunks is full -> extend it
		AudioOutputCache chunk;
		chunk.loadFrom(audioData);

		s_audioCaches.push_back(std::move(chunk));

		return s_audioCaches.size() - 1;
	}
}


AudioOutputSpeech::AudioOutputSpeech(ClientUser *user, unsigned int freq, Mumble::Protocol::AudioCodec codec,
									 unsigned int systemMaxBufferSize)
	: iMixerFreq(freq), m_codec(codec), p(user) {
	int err;

	opusState = nullptr;

	bHasTerminator = false;
	bStereo        = false;

	iSampleRate = SAMPLE_RATE;

	// opus's "frame" means different from normal audio term "frame"
	// normally, a frame means a bundle of only one sample from each channel,
	// e.g. for a stereo stream, ...[LR]LRLRLR.... where the bracket indicates a frame
	// in opus term, a frame means samples that span a period of time, which can be either stereo or mono
	// e.g. ...[LRLR....LRLR].... or ...[MMMM....MMMM].... for mono stream
	// opus supports frames with: 2.5, 5, 10, 20, 40 or 60 ms of audio data.
	// sample rate / 100 means 10ms (0.01s) mono audio data points (samples) per frame.
	iFrameSizePerChannel = iFrameSize = iSampleRate / 100; // for mono stream

	assert(m_codec == Mumble::Protocol::AudioCodec::Opus);

	// Always pretend Stereo mode is true by default. since opus will convert mono stream to stereo stream.
	// https://tools.ietf.org/html/rfc6716#section-2.1.2
	bStereo   = true;
	opusState = opus_decoder_create(static_cast< int >(iSampleRate), bStereo ? 2 : 1, nullptr);
	opus_decoder_ctl(opusState,
					 OPUS_SET_PHASE_INVERSION_DISABLED(1)); // Disable phase inversion for better mono downmix.

	// iAudioBufferSize: size (in unit of float) of the buffer used to store decoded pcm data.
	// For opus, the maximum frame size of a packet is 120ms (the maximum duration for a single frame
	// is 60ms but multiple frames may be bundled into a single packet of a duration up to 120ms).
	iAudioBufferSize = iSampleRate * 120 / 1000; // = SampleRate * 120ms = 48000Hz * 0.12s = 5760, ~23KB

	// iBufferSize: size of the buffer to store the resampled audio data.
	// Note that the number of samples in each opus packet can be different from the number of samples the system
	// requests from us each time (this is known as the system's audio buffer size).
	// For example, the maximum size of an opus packet is 120ms, but the system's audio buffer size is typically
	// ~5ms on my laptop.
	// Whenever the system's audio callback is called, we have two choice:
	//  1. Decode a new opus packet. Then we need a buffer to store unused samples (which don't fit in the system's
	//  buffer),
	//  2. Use unused samples from the buffer (remaining from the last decoded frame).
	// How large should this buffer be? Consider the case in which remaining samples in the buffer can not fill
	// the system's audio buffer. In that case, we need to decode a new opus packet. In the worst case, the buffer size
	// needed is
	//    120ms of new decoded audio data + system's buffer size - 1.
	iOutputSize = static_cast< unsigned int >(
		ceilf(static_cast< float >(iAudioBufferSize * iMixerFreq) / static_cast< float >(iSampleRate)));
	iBufferSize = iOutputSize + systemMaxBufferSize; // -1 has been rounded up

	if (bStereo) {
		iAudioBufferSize *= 2;
		iOutputSize *= 2;
		iBufferSize *= 2;
		iFrameSize *= 2;
	}

	pfBuffer = new float[iBufferSize];

	srs              = nullptr;
	fResamplerBuffer = nullptr;
	if (iMixerFreq != iSampleRate) {
		srs              = speex_resampler_init(bStereo ? 2 : 1, iSampleRate, iMixerFreq, 3, &err);
		fResamplerBuffer = new float[iAudioBufferSize];
	}

	iBufferOffset = iBufferFilled = iLastConsume = 0;
	bLastAlive                                   = true;

	iMissCount    = 0;
	iMissedFrames = 0;

	m_lastPacketSpan = 0;

	m_audioContext = Mumble::Protocol::AudioContext::INVALID;

	jbJitter   = jitter_buffer_init(static_cast< int >(iFrameSize));
	int margin = Global::get().s.iJitterBufferSize * static_cast< int >(iFrameSize);
	jitter_buffer_ctl(jbJitter, JITTER_BUFFER_SET_MARGIN, &margin);

	// We are configuring our Jitter buffer to use a custom deleter function. This prevents the buffer from
	// copying the stored data into the buffer itself and also from releasing the memory of it. Instead it
	// will now call this "deleter" function instead.
	// This allows us to manage our own (global) storage for our audio data. With that, we can reuse the same
	// memory regions in order to avoid frequent memory allocations and deallocations.
	// Also this is the basis for using our trick of actually only storing indices instead of proper data
	// pointers in the buffer.
	jitter_buffer_ctl(jbJitter, JITTER_BUFFER_SET_DESTROY_CALLBACK,
					  reinterpret_cast< void * >(&AudioOutputSpeech::invalidateAudioOutputCache));

	fFadeIn  = new float[iFrameSizePerChannel];
	fFadeOut = new float[iFrameSizePerChannel];

	float mul = static_cast< float >(M_PI / (2.0 * static_cast< double >(iFrameSizePerChannel)));
	for (unsigned int i = 0; i < iFrameSizePerChannel; ++i)
		fFadeIn[i] = fFadeOut[iFrameSizePerChannel - i - 1] = sinf(static_cast< float >(i) * mul);
}

AudioOutputSpeech::~AudioOutputSpeech() {
	if (opusState) {
		opus_decoder_destroy(opusState);
	}

	setupLocalSuppression(Settings::NoiseCancelOff);

	if (srs)
		speex_resampler_destroy(srs);

	jitter_buffer_destroy(jbJitter);

	if (p) {
		p->setTalking(Settings::Passive);
	}

	delete[] fFadeIn;
	delete[] fFadeOut;
	delete[] fResamplerBuffer;
}

void AudioOutputSpeech::addFrameToBuffer(const Mumble::Protocol::AudioData &audioData) {
	QMutexLocker lock(&qmJitter);

	if (audioData.payload.empty()) {
		return;
	}

	int samples = 0;

	assert(m_codec == Mumble::Protocol::AudioCodec::Opus);
	assert(audioData.usedCodec == m_codec);

	samples = opus_decoder_get_nb_samples(
		opusState, audioData.payload.data(),
		static_cast< int >(audioData.payload.size())); // this function return samples per channel
	samples *= 2;                                      // since we assume all input stream is stereo.

	// We can't handle frames which are not a multiple of our configured framesize.
	if (static_cast< unsigned int >(samples) % iFrameSize != 0) {
		qWarning("AudioOutputSpeech: Dropping Opus audio packet, because its sample count (%d) is not a "
				 "multiple of our frame size (%d)",
				 samples, iFrameSize);
		return;
	}

	// Copy the audio data to an AudioOutputCache instance and store that in our global chunk list
	std::size_t storageIndex = storeAudioOutputCache(audioData);

	// We cheat a bit and instead of storing the actual audio data in the jitter buffer, we store the index to
	// the created audio chunk in the buffer. Passing a length of 0 should ensure that this "pointer" will never
	// be dereferenced.

	// A call to jitter_buffer_put stores the packet in an internal array used for book-keeping.
	// The library uses jbp.data == NULL to differentiate between unused and reserved elements
	// of the book-keeping array.
	// Since a storageIndex of zero will look the same as a null pointer, we always add one to
	// ensure the library never erroneously confuses this entry with a free slot.
	JitterBufferPacket jbp;
	jbp.data      = reinterpret_cast< char * >(storageIndex) + 1;
	jbp.len       = 0;
	jbp.span      = static_cast< unsigned int >(samples);
	jbp.timestamp = static_cast< unsigned int >(iFrameSize * audioData.frameNumber);

	m_lastPacketSpan = static_cast< spx_uint32_t >(jbp.span);

	jitter_buffer_put(jbJitter, &jbp);
}

void AudioOutputSpeech::enforceIncomingDelayLimit() {
	const Settings &settings = Global::get().s;
	if (!settings.bLimitIncomingAudioDelay || m_lastPacketSpan == 0) {
		return;
	}

	// Convert the limit into jitter buffer timestamp units: iFrameSize units make up
	// 10ms of audio. Enforce at least one frame so that a misconfigured value can
	// never cause every single packet to be dropped.
	const spx_int32_t maxBacklog =
		static_cast< spx_int32_t >(iFrameSize) * std::max(settings.iMaxIncomingAudioDelayMs / 10, 1);

	// Estimate the queued audio from the number of packets the buffer holds ahead of
	// its playback pointer. This deliberately avoids absolute timestamps: the sender
	// resets its frame counter after long silence and the buffer re-synchronizes
	// itself after losses, both of which make absolute bookkeeping go stale, whereas
	// the available count is always relative to the buffer's current pointer.
	auto backlog = [this]() {
		spx_int32_t avail = 0;
		jitter_buffer_ctl(jbJitter, JITTER_BUFFER_GET_AVAILABLE_COUNT, &avail);
		return static_cast< spx_int32_t >(static_cast< spx_uint32_t >(avail) * m_lastPacketSpan);
	};

	spx_int32_t currentBacklog = backlog();
	if (currentBacklog <= maxBacklog) {
		return;
	}

	unsigned int droppedPackets = 0;
	spx_uint32_t droppedAudio   = 0;
	// When crossing a gap in the stream the pointer only advances by one frame per
	// iteration, so bound the time spent here; catch-up continues next cycle.
	for (int i = 0; i < 128 && currentBacklog > maxBacklog; ++i) {
		JitterBufferPacket jbp;
		spx_int32_t startofs = 0;

		const int prevPointer = jitter_buffer_get_pointer_timestamp(jbJitter);
		if (jitter_buffer_get(jbJitter, &jbp, static_cast< int >(iFrameSize), &startofs) == JITTER_BUFFER_OK) {
			// We own the returned packet (see the destroy callback comment in
			// prepareSampleBuffer()); release its audio cache slot without decoding it.
			invalidateAudioOutputCache(jbp.data);
			++droppedPackets;
			droppedAudio += static_cast< spx_uint32_t >(jbp.span);
		} else if (jitter_buffer_get_pointer_timestamp(jbJitter) == prevPointer) {
			// The buffer refuses to advance its playback pointer (it wants to insert
			// silence instead, or is resyncing); leave the catch-up to a later cycle.
			break;
		}

		currentBacklog = backlog();
	}

	if (droppedPackets > 0) {
		qWarning("AudioOutputSpeech: Dropped %u packet(s) (%u ms of audio) to limit the incoming audio delay to %d ms",
				 droppedPackets, droppedAudio * 10 / iFrameSize, settings.iMaxIncomingAudioDelayMs);
	}
}

void AudioOutputSpeech::setupLocalSuppression(Settings::NoiseCancel mode) {
	if (mode == m_localSuppressActive) {
		return;
	}

	const unsigned int channels = bStereo ? 2 : 1;

	// Release everything first; the processors carry stream state that would be
	// stale after a method change anyway.
	for (auto &preprocessor : m_localPreprocessor) {
		preprocessor.deinit();
	}
#ifdef USE_RNNOISE
	for (auto &denoiser : m_localDenoiser) {
		if (denoiser) {
			rnnoise_destroy(denoiser);
			denoiser = nullptr;
		}
	}
#endif
#ifdef USE_WEBRTC_AUDIO_PROCESSING
	m_localWebrtc.reset();
#endif
#ifdef USE_DEEPFILTERNET
	for (auto &deepfilter : m_localDeepFilter) {
		deepfilter.reset();
	}
#endif

	if (mode == Settings::NoiseCancelSpeex || mode == Settings::NoiseCancelBoth) {
		for (unsigned int channel = 0; channel < channels; ++channel) {
			m_localPreprocessor[channel].init(iSampleRate, iFrameSizePerChannel);
			m_localPreprocessor[channel].setVAD(false);
			m_localPreprocessor[channel].setAGC(false);
			m_localPreprocessor[channel].setDereverb(false);
			m_localPreprocessor[channel].setDenoise(true);
		}
		// Force the strength to be (re)applied on the next processed frame.
		m_localSpeexStrengthApplied = 0;
	}

#ifdef USE_RNNOISE
	if (mode == Settings::NoiseCancelRNN || mode == Settings::NoiseCancelBoth) {
		for (unsigned int channel = 0; channel < channels; ++channel) {
			m_localDenoiser[channel] = rnnoise_create(nullptr);
		}
	}
#endif

#ifdef USE_WEBRTC_AUDIO_PROCESSING
	if (mode == Settings::NoiseCancelWebRTC) {
		m_localWebrtc = std::make_unique< WebRTCAudioProcessor >(
			static_cast< int >(iSampleRate), static_cast< int >(channels), false, true,
			WebRTCAudioProcessor::NoiseLevel::High, false);
		if (!m_localWebrtc->isValid()) {
			// Remember the mode anyway so this is not retried every frame; the
			// processing loop skips the missing processor.
			qWarning("AudioOutputSpeech: Failed to initialize the local WebRTC noise suppressor");
			m_localWebrtc.reset();
		}
	}
#endif

#ifdef USE_DEEPFILTERNET
	if (mode == Settings::NoiseCancelDeepFilter) {
		const float attenLimit = Mumble::DeepFilter::clampAttenLimitDb(Global::get().s.iDeepFilterAttenLimit);
		const float postFilter = Mumble::DeepFilter::postFilterBeta(Global::get().s.iDeepFilterPostFilter);

		bool valid = true;
		for (unsigned int channel = 0; channel < channels; ++channel) {
			m_localDeepFilter[channel] = std::make_unique< DeepFilterNetProcessor >(attenLimit, postFilter);
			valid                      = valid && m_localDeepFilter[channel]->isValid();
		}
		if (!valid) {
			// Remember the mode anyway so this is not retried every frame; the
			// processing loop skips the missing processors.
			qWarning("AudioOutputSpeech: Failed to initialize the local DeepFilterNet suppressor");
			for (auto &deepfilter : m_localDeepFilter) {
				deepfilter.reset();
			}
		}
	}
#endif

	m_localSuppressActive = mode;
}

void AudioOutputSpeech::applyLocalProcessing(float *samples, unsigned int sampleCount) {
	if (!p) {
		return;
	}

	const unsigned int channels = bStereo ? 2 : 1;

	// Read the per-user configuration once per call; the UI may change it at
	// any time.
	Settings::NoiseCancel suppressMode = Settings::NoiseCancelOff;
	if (p->m_localSuppressEnabled.load()) {
		suppressMode = static_cast< Settings::NoiseCancel >(Mumble::ReceiveProcessing::resolveSuppression(
			static_cast< LocalSuppression >(p->m_localSuppressMode.load()), localSuppressionSupport()));
	}
	setupLocalSuppression(suppressMode);

	// The amplification levels mirror the input's: a base floor that is always
	// applied, an adaptive ceiling for noise and the maximum ceiling for
	// speech, each on the same loudness-knob scale.
	const bool ampEnabled  = p->m_localSnrAmpEnabled.load();
	const float maxDb      = Mumble::Amplification::gainDbForLoudnessF(p->m_localAmpMaxLoudness.load());
	const float adaptiveDb = Mumble::Amplification::gainDbForLoudnessF(p->m_localAmpAdaptiveLoudness.load());
	const float baseDb     = Mumble::Amplification::gainDbForLoudnessF(p->m_localAmpBaseLoudness.load());
	const float silenceDb  = static_cast< float >(p->m_localSnrSilenceDb10.load()) / 10.0f;
	const float speechDb   = static_cast< float >(p->m_localSnrSpeechDb10.load()) / 10.0f;

	if (m_localSuppressActive == Settings::NoiseCancelSpeex || m_localSuppressActive == Settings::NoiseCancelBoth) {
		const int strength = p->m_localSpeexSuppressStrength.load();
		if (strength != m_localSpeexStrengthApplied) {
			for (unsigned int channel = 0; channel < channels; ++channel) {
				m_localPreprocessor[channel].setNoiseSuppress(strength);
			}
			m_localSpeexStrengthApplied = strength;
		}
	}

	// Work through the decoded audio one 10 ms frame at a time - the unit all
	// suppressors operate on. Decoded packets are always whole frames; anything
	// less is left untouched.
	for (unsigned int offset = 0; offset + iFrameSize <= sampleCount; offset += iFrameSize) {
		float *frame = samples + offset;

		if (m_localSuppressActive != Settings::NoiseCancelOff) {
			// The suppressors operate on (mono) 16 bit PCM, so run them on a
			// deinterleaved short view of the frame and convert the result back.
			short deinterleaved[2][480];
			assert(iFrameSizePerChannel <= 480);
			for (unsigned int i = 0; i < iFrameSizePerChannel; ++i) {
				for (unsigned int channel = 0; channel < channels; ++channel) {
					deinterleaved[channel][i] = clampToShort(frame[i * channels + channel] * LOCAL_PCM_SCALE);
				}
			}

#ifdef USE_RNNOISE
			// RNNoise first, Speex second - the order the input pipeline uses
			// for the "Both" mode. RNNoise works on floats in 16 bit range.
			if (m_localSuppressActive == Settings::NoiseCancelRNN
				|| m_localSuppressActive == Settings::NoiseCancelBoth) {
				float denoiseFrame[480];
				for (unsigned int channel = 0; channel < channels; ++channel) {
					if (!m_localDenoiser[channel]) {
						continue;
					}
					for (unsigned int i = 0; i < iFrameSizePerChannel; ++i) {
						denoiseFrame[i] = deinterleaved[channel][i];
					}
					rnnoise_process_frame(m_localDenoiser[channel], denoiseFrame, denoiseFrame);
					for (unsigned int i = 0; i < iFrameSizePerChannel; ++i) {
						deinterleaved[channel][i] = clampToShort(denoiseFrame[i]);
					}
				}
			}
#endif

			if (m_localSuppressActive == Settings::NoiseCancelSpeex
				|| m_localSuppressActive == Settings::NoiseCancelBoth) {
				for (unsigned int channel = 0; channel < channels; ++channel) {
					if (m_localPreprocessor[channel]) {
						m_localPreprocessor[channel].run(*deinterleaved[channel]);
					}
				}
			}

#ifdef USE_WEBRTC_AUDIO_PROCESSING
			if (m_localSuppressActive == Settings::NoiseCancelWebRTC && m_localWebrtc) {
				// The WebRTC processor handles the interleaved frame in one go.
				short interleaved[2 * 480];
				for (unsigned int i = 0; i < iFrameSizePerChannel * channels; ++i) {
					interleaved[i] = deinterleaved[i % channels][i / channels];
				}
				m_localWebrtc->processCapture(interleaved, 0);
				for (unsigned int i = 0; i < iFrameSizePerChannel * channels; ++i) {
					deinterleaved[i % channels][i / channels] = interleaved[i];
				}
			}
#endif

#ifdef USE_DEEPFILTERNET
			if (m_localSuppressActive == Settings::NoiseCancelDeepFilter) {
				for (unsigned int channel = 0; channel < channels; ++channel) {
					if (m_localDeepFilter[channel]) {
						m_localDeepFilter[channel]->processFrame(deinterleaved[channel]);
					}
				}
			}
#endif

			for (unsigned int i = 0; i < iFrameSizePerChannel; ++i) {
				for (unsigned int channel = 0; channel < channels; ++channel) {
					frame[i * channels + channel] =
						static_cast< float >(deinterleaved[channel][i]) / LOCAL_PCM_SCALE;
				}
			}
		}

		// Track the stream's noise floor and speech envelope on the signal that
		// is actually played (and possibly amplified), i.e. after suppression.
		// The measurement runs even while both features are off so that the
		// user information dialog can always show a live SNR.
		const float rms = Mumble::ReceiveProcessing::frameRms(frame, iFrameSize);
		m_snrTracker.update(rms);

		// Classify the frame as speech or noise with hysteresis on the
		// configured SNR thresholds (like the input's voice activity gate),
		// then ease the smoothed speech state towards the result: it selects
		// how far the amplification ceiling sits between the adaptive (noise)
		// and maximum (speech) levels.
		const float frameSnrDb = m_snrTracker.frameSnrDb(rms);
		m_gateSpeech = Mumble::Amplification::classifySpeech(frameSnrDb, silenceDb, speechDb, m_gateSpeech);
		const float gateStep = m_gateSpeech ? Mumble::ReceiveProcessing::SPEECHINESS_RISE_PER_FRAME
											: Mumble::ReceiveProcessing::SPEECHINESS_FALL_PER_FRAME;
		m_gateSpeechiness =
			Mumble::Amplification::approach(m_gateSpeechiness, m_gateSpeech ? 1.0f : 0.0f, gateStep);

		float gainTargetDb = 0.0f;
		if (ampEnabled) {
			// Lift the speech envelope towards the target level, raised to at
			// least the base floor and capped at this frame's ceiling.
			const float ceilingDb = Mumble::Amplification::gainCeilingDb(adaptiveDb, maxDb, m_gateSpeechiness);
			const float liftDb    = Mumble::ReceiveProcessing::desiredGainDb(m_snrTracker.signalLevel, maxDb);
			gainTargetDb          = Mumble::Amplification::effectiveGainDb(liftDb, baseDb, ceilingDb);
		}
		m_localGainDb = Mumble::Amplification::approach(m_localGainDb, gainTargetDb,
														Mumble::ReceiveProcessing::GAIN_STEP_DB_PER_FRAME);
		const float gainFactor = Mumble::Amplification::dbToLinear(m_localGainDb);
		Mumble::ReceiveProcessing::applyGainClamped(frame, iFrameSize, gainFactor);

		p->m_localSnrDb.store(m_snrTracker.snrDb());
		p->m_localFrameSnrDb.store(frameSnrDb);
		p->m_localAmpSpeech.store(m_gateSpeech);
		p->m_localAmpFactor.store(gainFactor);
	}
}

bool AudioOutputSpeech::prepareSampleBuffer(unsigned int frameCount) {
	unsigned int channels = bStereo ? 2 : 1;
	// Note: all stereo supports are crafted for opus, since other codecs are deprecated and will soon be removed.

	unsigned int sampleCount = frameCount * channels;

	// we can not control exactly how many frames decoder returns
	// so we need a buffer to keep unused frames
	// shift the buffer, remove decoded and played frames
	for (unsigned int i = iLastConsume; i < iBufferFilled; ++i)
		pfBuffer[i - iLastConsume] = pfBuffer[i];

	iBufferFilled -= iLastConsume;

	iLastConsume = sampleCount;

	// Maximum interaural delay is accounted for to prevent audio glitches
	if (iBufferFilled >= sampleCount + INTERAURAL_DELAY)
		return bLastAlive;

	float *pOut;
	bool nextalive = bLastAlive;

	while (iBufferFilled < sampleCount + INTERAURAL_DELAY) {
		int decodedSamples = static_cast< int >(iFrameSize);
		resizeBuffer(iBufferFilled + iOutputSize + INTERAURAL_DELAY);
		// TODO: allocating memory in the audio callback will crash mumble in some cases.
		//       we need to initialize the buffer with an appropriate size when initializing
		//       this class. See #4250.

		pOut = (srs) ? fResamplerBuffer : (pfBuffer + iBufferFilled);

		if (!bLastAlive) {
			memset(pOut, 0, iFrameSize * sizeof(float));
		} else {
			if (p == &LoopUser::lpLoopy) {
				LoopUser::lpLoopy.fetchFrames();
			}

			int avail = 0;
			int ts    = jitter_buffer_get_pointer_timestamp(jbJitter);
			jitter_buffer_ctl(jbJitter, JITTER_BUFFER_GET_AVAILABLE_COUNT, &avail);

			if (p && (ts == 0)) {
				int want = static_cast< int >(p->fAverageAvailable);
				if (avail < want) {
					++iMissCount;
					if (iMissCount < 20) {
						memset(pOut, 0, iFrameSize * sizeof(float));
						goto nextframe;
					}
				}
			}

			if (qlFrames.isEmpty()) {
				QMutexLocker lock(&qmJitter);

				// If late packets have made the buffered audio exceed the configured
				// delay limit, skip ahead to catch up with the live stream (#6354).
				enforceIncomingDelayLimit();

				JitterBufferPacket jbp;

				spx_int32_t startofs = 0;
				if (jitter_buffer_get(jbJitter, &jbp, static_cast< int >(iFrameSize), &startofs) == JITTER_BUFFER_OK) {
					std::lock_guard< std::mutex > audioChunkLock(s_audioCachesMutex);

					iMissCount = 0;

					// The "data pointer" that is stored in the buffer is actually just an index to s_audioCaches
					const std::size_t index = reinterpret_cast< std::size_t >(jbp.data) - 1;
					assert(jbp.len == 0);
					assert(index < s_audioCaches.size());

					AudioOutputCache &cache = s_audioCaches[index];
					assert(cache.isValid());

					bHasTerminator = cache.isLastFrame();

					assert(m_codec == Mumble::Protocol::AudioCodec::Opus);

					// Copy audio data into qlFrames
					qlFrames << QByteArray(reinterpret_cast< const char * >(cache.getAudioData().data()),
										   static_cast< int >(cache.getAudioData().size()));

					if (cache.containsPositionalInformation()) {
						assert(cache.getPositionalInformation().size() == 3);
						assert(fPos.size() == 3);

						for (unsigned int i = 0; i < 3; ++i) {
							fPos[i] = cache.getPositionalInformation()[i];
						}
					} else {
						fPos[0] = fPos[1] = fPos[2] = 0.0f;
					}

					m_suggestedVolumeAdjustment = cache.getVolumeAdjustment();
					m_audioContext              = cache.getContext();

					if (p) {
						float a = static_cast< float >(avail);
						if (static_cast< float >(avail) >= p->fAverageAvailable)
							p->fAverageAvailable = a;
						else
							p->fAverageAvailable *= 0.99f;
					}

					// If a destroy callback has been registered, jitter_buffer_get expects the caller to
					// invoke the destroy callback on the returned packet.
					// We registered a destroy callback in our constructor, so we clean up the packet here.
					cache.clear();
				} else {
					// Let the jitter buffer know it's the right time to adjust the buffering delay to the network
					// conditions.
					jitter_buffer_update_delay(jbJitter, &jbp, nullptr);

					iMissCount++;
					if (iMissCount > 10)
						nextalive = false;
				}
			}

			if (!qlFrames.isEmpty()) {
				QByteArray qba = qlFrames.takeFirst();

				assert(m_codec == Mumble::Protocol::AudioCodec::Opus);

				if (qba.isEmpty() || !(p && p->bLocalMute)) {
					// If qba is empty, we have to let Opus know about the packet loss
					// Otherwise if the associated user is not locally muted, we want to decode the audio
					// packet normally in order to be able to play it.
					decodedSamples = opus_decode_float(
						opusState, qba.isEmpty() ? nullptr : reinterpret_cast< const unsigned char * >(qba.constData()),
						static_cast< opus_int32 >(qba.size()), pOut, static_cast< int >(iAudioBufferSize / channels),
						0);
				} else {
					// If the packet is non-empty, but the associated user is locally muted,
					// we don't have to decode the packet. Instead it is enough to know how many
					// samples it contained so that we can then mute the appropriate output length
					decodedSamples = opus_packet_get_samples_per_frame(
						reinterpret_cast< const unsigned char * >(qba.constData()), SAMPLE_RATE);
				}

				// The returned sample count we get from the Opus functions refer to samples per channel.
				// Thus in order to get the total amount, we have to multiply by the channel count.
				decodedSamples *= static_cast< int >(channels);

				if (decodedSamples < 0) {
					decodedSamples = static_cast< int >(iFrameSize);
					memset(pOut, 0, iFrameSize * sizeof(float));
				}

				bool update = true;
				if (p) {
					float &fPowerMax = p->fPowerMax;
					float &fPowerMin = p->fPowerMin;

					float pow = 0.0f;
					for (int i = 0; i < decodedSamples; ++i) {
						pow += pOut[i] * pOut[i];
					}
					pow = sqrtf(pow / static_cast< float >(decodedSamples)); // Average over both L and R channel.

					if (!p->bLocalMute) {
						// For locally muted users no audio is decoded, so the measured
						// power would be garbage
						p->registerAudioPower(pow);
					}

					if (pow >= fPowerMax) {
						fPowerMax = pow;
					} else {
						if (pow <= fPowerMin) {
							fPowerMin = pow;
						} else {
							fPowerMax = 0.99f * fPowerMax;
							fPowerMin += 0.0001f * pow;
						}
					}

					update = (pow < (fPowerMin + 0.01f * (fPowerMax - fPowerMin))); // Update jitter buffer when quiet.
				}

				if (qlFrames.isEmpty() && update) {
					jitter_buffer_update_delay(jbJitter, nullptr, nullptr);
				}

				if (qlFrames.isEmpty() && bHasTerminator) {
					nextalive = false;
				}
			} else {
				assert(m_codec == Mumble::Protocol::AudioCodec::Opus);
				decodedSamples =
					opus_decode_float(opusState, nullptr, 0, pOut, static_cast< int >(iFrameSizePerChannel), 0);
				decodedSamples *= static_cast< int >(channels);

				if (decodedSamples < 0) {
					decodedSamples = static_cast< int >(iFrameSize);
					memset(pOut, 0, iFrameSize * sizeof(float));
				}
			}

			// Local, receive-side processing configured for this user: noise
			// suppression and SNR-gated amplification of their decoded stream.
			// This only ever rewrites the audio played back for this user; the
			// transmit path is not involved. For locally muted users nothing was
			// decoded, so there is nothing to process either.
			if (p && !p->bLocalMute) {
				applyLocalProcessing(pOut, static_cast< unsigned int >(decodedSamples));
			}

			if (!nextalive) {
				for (unsigned int i = 0; i < static_cast< unsigned int >(iFrameSizePerChannel); ++i) {
					for (unsigned int s = 0; s < channels; ++s)
						pOut[i * channels + s] *= fFadeOut[i];
				}
			} else if (ts == 0) {
				for (unsigned int i = 0; i < static_cast< unsigned int >(iFrameSizePerChannel); ++i) {
					for (unsigned int s = 0; s < channels; ++s)
						pOut[i * channels + s] *= fFadeIn[i];
				}
			}

			for (unsigned int i = static_cast< unsigned int >(decodedSamples) / iFrameSize; i > 0; --i) {
				jitter_buffer_tick(jbJitter);
			}
		}
	nextframe:
		if (p && p->bLocalMute) {
			// Overwrite the output with zeros as this user is muted
			// NOTE: If Opus is used, then in this case no samples have actually been decoded and thus
			// we don't discard previously done work (in form of decoding the audio stream) by overwriting
			// it with zeros.
			memset(pOut, 0, static_cast< unsigned int >(decodedSamples) * sizeof(float));
		}

		spx_uint32_t inlen  = static_cast< unsigned int >(decodedSamples) / channels; // per channel
		spx_uint32_t outlen = static_cast< unsigned int >(
			ceilf(static_cast< float >(static_cast< unsigned int >(decodedSamples) / channels * iMixerFreq)
				  / static_cast< float >(iSampleRate)));
		if (srs && bLastAlive) {
			if (channels == 1) {
				speex_resampler_process_float(srs, 0, fResamplerBuffer, &inlen, pfBuffer + iBufferFilled, &outlen);
			} else if (channels == 2) {
				speex_resampler_process_interleaved_float(srs, fResamplerBuffer, &inlen, pfBuffer + iBufferFilled,
														  &outlen);
			}
		}
		iBufferFilled += outlen * channels;
	}

	if (p) {
		Settings::TalkState ts;
		if (!nextalive) {
			m_audioContext = Mumble::Protocol::AudioContext::INVALID;
		}

		switch (m_audioContext) {
			case Mumble::Protocol::AudioContext::LISTEN:
				// Fallthrough
			case Mumble::Protocol::AudioContext::NORMAL:
				ts = Settings::Talking;
				break;
			case Mumble::Protocol::AudioContext::SHOUT:
				ts = Settings::Shouting;
				break;
			case Mumble::Protocol::AudioContext::INVALID:
				ts = Settings::Passive;
				break;
			case Mumble::Protocol::AudioContext::WHISPER:
				ts = Settings::Whispering;
				break;
			default:
				// Default to normal talking, if we don't know the used context
				ts = Settings::Talking;
				break;
		}

		if (ts != Settings::Passive && (p->bLocalMute || p->volumeMute)) {
			ts = Settings::MutedTalking;
		}

		p->setTalking(ts);
	}

	bool tmp   = bLastAlive;
	bLastAlive = nextalive;
	return tmp;
}
