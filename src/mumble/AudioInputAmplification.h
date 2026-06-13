// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_AUDIOINPUTAMPLIFICATION_H_
#define MUMBLE_MUMBLE_AUDIOINPUTAMPLIFICATION_H_

#include <algorithm>
#include <cmath>
#include <cstdint>

// Pure, dependency-free helpers backing Mumble's input amplification. They turn
// the loudness knobs into gains, combine the base/adaptive/maximum levels and
// apply a gain to a PCM frame. Kept free of Qt and Speex so they can be unit
// tested on their own.
namespace Mumble {
namespace Amplification {

	/// Loudness the Speex AGC drives the analyzed signal towards, on the
	/// 0-32768 scale. Also doubles as the peak target in peak mode.
	constexpr float AGC_TARGET = 30000.0f;

	/// Maximum AGC gain (dB) that lifts a signal sitting at \p loudness up to
	/// AGC_TARGET. \p loudness is the "min loudness" knob (1-32768): smaller
	/// values request more amplification. Mirrors the historic
	/// 20*log10(target/loudness) formula, rounded down like Speex expects.
	inline int gainDbForLoudness(int loudness) {
		loudness = std::clamp(loudness, 1, static_cast< int >(AGC_TARGET));
		return static_cast< int >(std::floor(20.0f * std::log10(AGC_TARGET / static_cast< float >(loudness))));
	}

	/// Like gainDbForLoudness but without the rounding, for precise gain math.
	/// The rounded variant is what Speex's integer max-gain expects; this one is
	/// used to enforce the configured levels exactly.
	inline float gainDbForLoudnessF(int loudness) {
		loudness = std::clamp(loudness, 1, static_cast< int >(AGC_TARGET));
		return 20.0f * std::log10(AGC_TARGET / static_cast< float >(loudness));
	}

	/// Linear factor for a gain given in dB.
	inline float dbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }

	/// Resolve the adaptive loudness knob. A non-positive value is the "follow
	/// the maximum" sentinel, i.e. no extra capping while the input is noise.
	inline int resolveAdaptiveLoudness(int adaptiveLoudness, int maxLoudness) {
		return adaptiveLoudness <= 0 ? maxLoudness : adaptiveLoudness;
	}

	/// Gain ceiling (dB) for the current frame, interpolated between the
	/// adaptive (noise) ceiling and the maximum (speech) ceiling by
	/// \p speechiness in [0, 1]. 0 means noise (cap at \p adaptiveDb), 1 means
	/// speech (allow up to \p maxDb).
	inline float gainCeilingDb(float adaptiveDb, float maxDb, float speechiness) {
		speechiness = std::clamp(speechiness, 0.0f, 1.0f);
		return adaptiveDb + (maxDb - adaptiveDb) * speechiness;
	}

	/// Effective gain (dB) to apply: what the AGC computed (\p agcGainDb), raised
	/// to at least the base floor (\p baseDb) and capped at the ceiling
	/// (\p ceilingDb). Because the ceiling is enforced here as a precise
	/// (fractional) value, the configured levels are hit exactly even though the
	/// AGC's own max gain is integer. The base floor only ever raises the
	/// amplification of quiet input; it never undoes an attenuation the AGC
	/// applies to keep an already-loud signal from clipping.
	inline float effectiveGainDb(float agcGainDb, float baseDb, float ceilingDb) {
		if (agcGainDb < 0.0f) {
			return agcGainDb;
		}
		return std::min(std::max(agcGainDb, baseDb), ceilingDb);
	}

	/// Speech/noise classification with hysteresis, mirroring Mumble's SNR voice
	/// activity detection: it becomes speech once \p level rises above
	/// \p maxThreshold and noise once it falls to or below \p minThreshold,
	/// holding the previous state in between. The adaptive amplification reacts
	/// to the same thresholds as voice activity, so "noise" means exactly what it
	/// means for transmission.
	inline bool classifySpeech(float level, float minThreshold, float maxThreshold, bool wasSpeech) {
		if (level > maxThreshold) {
			return true;
		}
		if (level <= minThreshold) {
			return false;
		}
		return wasSpeech;
	}

	/// Move \p current toward \p target by at most \p step. Used to ease the
	/// amplification ceiling between the noise and speech levels so it never
	/// jumps abruptly.
	inline float approach(float current, float target, float step) {
		return current + std::clamp(target - current, -step, step);
	}

	/// Multiply \p count interleaved samples in place by \p factor, clamping to
	/// the int16 range. Applied identically to every channel, so stereo keeps
	/// its balance.
	inline void applyGain(short *samples, unsigned int count, float factor) {
		if (factor == 1.0f) {
			return;
		}
		for (unsigned int i = 0; i < count; ++i) {
			const float v = static_cast< float >(samples[i]) * factor;
			samples[i]    = static_cast< short >(std::clamp(v, -32768.0f, 32767.0f));
		}
	}

	/// Factor that brings the loudest of \p count interleaved samples back down
	/// to \p targetPeak, or 1.0 if it already fits. Drives the peak-targeting
	/// mode that keeps amplified peaks from clipping.
	inline float peakLimitFactor(const short *samples, unsigned int count, float targetPeak) {
		int peak = 1;
		for (unsigned int i = 0; i < count; ++i) {
			const int a = std::abs(static_cast< int >(samples[i]));
			if (a > peak) {
				peak = a;
			}
		}
		return static_cast< float >(peak) > targetPeak ? targetPeak / static_cast< float >(peak) : 1.0f;
	}

	/// Downmix \p frames interleaved stereo L/R pairs into mono by averaging.
	inline void downmixStereoToMono(const short *stereo, short *mono, unsigned int frames) {
		for (unsigned int i = 0; i < frames; ++i) {
			mono[i] = static_cast< short >(
				(static_cast< int >(stereo[2 * i]) + static_cast< int >(stereo[2 * i + 1])) / 2);
		}
	}

} // namespace Amplification
} // namespace Mumble

#endif
