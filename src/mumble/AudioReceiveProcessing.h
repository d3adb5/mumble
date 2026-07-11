// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_AUDIORECEIVEPROCESSING_H_
#define MUMBLE_MUMBLE_AUDIORECEIVEPROCESSING_H_

#include "AudioInputAmplification.h"

#include <algorithm>
#include <cmath>

// Pure, dependency-free helpers backing the local (receive-side) processing of
// other users' audio streams: the signal-to-noise estimate of a decoded stream
// and the SNR-gated amplification derived from it. Everything here operates on
// normalized float PCM (full scale = 1.0) as produced by the Opus decoder and
// is kept free of Qt and Speex so it can be unit tested on its own.
namespace Mumble {
namespace ReceiveProcessing {

	/// Loudness the amplification drives the speech envelope towards, as a
	/// fraction of full scale. The same 30000-of-32768 level the input AGC
	/// targets for the transmitted signal.
	constexpr float TARGET_LEVEL = 30000.0f / 32768.0f;

	/// Floor for level and noise estimates (~ -80 dBFS). Keeps the SNR math
	/// away from log(0) and stands in for digital silence.
	constexpr float LEVEL_FLOOR = 1e-4f;

	/// Upper bound of the reported SNR in dB; everything above is "clean".
	constexpr float SNR_MAX_DB = 60.0f;

	/// Upward adaptation of the noise floor per 10 ms frame (~3.5 dB/s). Slow
	/// enough not to climb into ongoing speech before a pause re-anchors it.
	constexpr float NOISE_RISE_PER_FRAME = 1.004f;

	/// Weight of the downward noise-floor adaptation: how far the estimate
	/// moves towards a quieter frame at once. Fast, so pauses take effect
	/// almost immediately.
	constexpr float NOISE_FALL_WEIGHT = 0.3f;

	/// Decay of the speech envelope per 10 ms frame (~4.3 dB/s), bridging the
	/// gaps between words without letting a stale peak linger forever.
	constexpr float SIGNAL_DECAY_PER_FRAME = 0.995f;

	/// Frame SNR (dB) below which a frame counts as pure noise and receives no
	/// amplification at all.
	constexpr float GATE_OPEN_DB = 3.0f;

	/// Frame SNR (dB) above which a frame counts as pure signal and receives
	/// the full amplification. In between the gain fades in linearly.
	constexpr float GATE_FULL_DB = 12.0f;

	/// Maximum change of the applied gain per 10 ms frame (50 dB/s): fast
	/// enough to follow speech onsets, slow enough not to click.
	constexpr float GAIN_STEP_DB_PER_FRAME = 0.5f;

	/// RMS of \p count interleaved samples, normalized to [0, 1]. Averaging
	/// across channels keeps stereo and mono streams comparable.
	inline float frameRms(const float *samples, unsigned int count) {
		if (count == 0) {
			return 0.0f;
		}
		float sum = 0.0f;
		for (unsigned int i = 0; i < count; ++i) {
			sum += samples[i] * samples[i];
		}
		return std::sqrt(sum / static_cast< float >(count));
	}

	/// Running estimate of a stream's noise floor and speech envelope, fed one
	/// 10 ms frame RMS at a time. The noise floor follows quiet frames quickly
	/// and rises only slowly, so it settles on the background hiss between the
	/// words; the envelope rides the loud frames and decays across pauses.
	struct SnrTracker {
		float noiseLevel  = LEVEL_FLOOR;
		float signalLevel = LEVEL_FLOOR;
		bool primed       = false;

		void reset() {
			noiseLevel  = LEVEL_FLOOR;
			signalLevel = LEVEL_FLOOR;
			primed      = false;
		}

		void update(float rms) {
			rms = std::clamp(rms, LEVEL_FLOOR, 1.0f);
			if (!primed) {
				noiseLevel  = rms;
				signalLevel = rms;
				primed      = true;
				return;
			}

			if (rms < noiseLevel) {
				noiseLevel += (rms - noiseLevel) * NOISE_FALL_WEIGHT;
			} else {
				noiseLevel = std::min(noiseLevel * NOISE_RISE_PER_FRAME, rms);
			}
			noiseLevel = std::max(noiseLevel, LEVEL_FLOOR);

			if (rms > signalLevel) {
				signalLevel = rms;
			} else {
				signalLevel = std::max(signalLevel * SIGNAL_DECAY_PER_FRAME, noiseLevel);
			}
		}

		/// Stream SNR (dB): the speech envelope over the noise floor. Zero for
		/// a stream that is silence or constant noise, capped at SNR_MAX_DB.
		float snrDb() const {
			return std::clamp(20.0f * std::log10(signalLevel / noiseLevel), 0.0f, SNR_MAX_DB);
		}

		/// SNR (dB) of a single frame with RMS \p rms against the current
		/// noise floor. Drives the per-frame amplification gate.
		float frameSnrDb(float rms) const {
			rms = std::max(rms, LEVEL_FLOOR);
			return std::clamp(20.0f * std::log10(rms / noiseLevel), 0.0f, SNR_MAX_DB);
		}
	};

	/// How much of the amplification a frame with the given SNR receives:
	/// 0 below GATE_OPEN_DB (noise), 1 above GATE_FULL_DB (signal), linear in
	/// between. This is what keeps the noise from being amplified as much as
	/// the speech.
	inline float speechiness(float frameSnrDb) {
		return std::clamp((frameSnrDb - GATE_OPEN_DB) / (GATE_FULL_DB - GATE_OPEN_DB), 0.0f, 1.0f);
	}

	/// Gain (dB) that lifts the speech envelope \p signalLevel up to the
	/// target level, capped at \p maxGainDb and never negative: an already
	/// loud stream is left alone rather than attenuated.
	inline float desiredGainDb(float signalLevel, float maxGainDb) {
		signalLevel      = std::max(signalLevel, LEVEL_FLOOR);
		const float lift = 20.0f * std::log10(TARGET_LEVEL / signalLevel);
		return std::clamp(lift, 0.0f, std::max(maxGainDb, 0.0f));
	}

	/// Multiply \p count interleaved samples in place by \p factor, clamping
	/// to full scale. Applied identically to every channel, so stereo keeps
	/// its balance.
	inline void applyGainClamped(float *samples, unsigned int count, float factor) {
		if (factor == 1.0f) {
			return;
		}
		for (unsigned int i = 0; i < count; ++i) {
			samples[i] = std::clamp(samples[i] * factor, -1.0f, 1.0f);
		}
	}

	/// The noise-suppression methods a stream can be processed with locally.
	/// Mirrors Settings::NoiseCancel value for value (checked by static_asserts
	/// where the two meet), duplicated here to keep this header Qt-free.
	enum class Suppression { Off = 0, Speex = 1, RNN = 2, Both = 3, WebRTC = 4, DeepFilter = 5 };

	/// Which suppression backends this build provides beyond Speex.
	struct SuppressionSupport {
		bool rnnoise    = false;
		bool webrtc     = false;
		bool deepfilter = false;
	};

	/// Resolve a requested suppression method against the backends this build
	/// actually has, falling back to Speex (which is always available) like
	/// the input pipeline does. Unknown values disable the suppression.
	inline Suppression resolveSuppression(Suppression requested, const SuppressionSupport &support) {
		switch (requested) {
			case Suppression::Off:
			case Suppression::Speex:
				return requested;
			case Suppression::RNN:
			case Suppression::Both:
				return support.rnnoise ? requested : Suppression::Speex;
			case Suppression::WebRTC:
				return support.webrtc ? requested : Suppression::Speex;
			case Suppression::DeepFilter:
				return support.deepfilter ? requested : Suppression::Speex;
		}
		return Suppression::Off;
	}

} // namespace ReceiveProcessing
} // namespace Mumble

#endif
