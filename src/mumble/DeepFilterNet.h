// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_DEEPFILTERNET_H_
#define MUMBLE_MUMBLE_DEEPFILTERNET_H_

#include <algorithm>
#include <cstdint>
#include <limits>

// Pure, dependency-free helpers backing the DeepFilterNet3 noise suppressor: the
// 16 bit PCM <-> normalized float conversion the model works in, and the
// clamping/scaling of the user-facing settings into the ranges the library
// expects. Kept free of Qt and of the (Rust) DeepFilterNet headers so they can
// be unit tested on their own, mirroring src/mumble/AudioInputSilence.h.
namespace Mumble {
namespace DeepFilter {

	/// DeepFilterNet operates at a fixed 48 kHz and a 10 ms hop, i.e. 480 samples
	/// per processed frame - the same cadence as Mumble's input pipeline.
	constexpr unsigned int FRAME_SIZE = 480;

	/// Attenuation limit (dB) bounds, mirroring DeepFilterNet's own range. The
	/// limit caps how much the suppressor may attenuate noise: the maximum leaves
	/// the model unconstrained (full suppression), lower values keep more of the
	/// ambient signal. The maximum is the sensible default for voice chat.
	constexpr int ATTEN_LIMIT_MIN_DB     = 0;
	constexpr int ATTEN_LIMIT_MAX_DB     = 100;
	constexpr int ATTEN_LIMIT_DEFAULT_DB = ATTEN_LIMIT_MAX_DB;

	/// Post-filter strength, stored as an integer (beta x 1000) so it fits a
	/// slider. DeepFilterNet's post filter sharpens the speech/noise separation to
	/// curb residual "musical" noise; a beta of 0 disables it and the useful range
	/// tops out around 0.05. Off by default so the unmodified model output is used.
	constexpr int POST_FILTER_MIN         = 0;
	constexpr int POST_FILTER_MAX         = 50;
	constexpr int POST_FILTER_DEFAULT     = 0;
	constexpr float POST_FILTER_BETA_SCALE = 1000.0f;

	/// Convert a 16 bit PCM sample to the normalized float in [-1, 1) the model
	/// expects.
	inline float int16ToFloat(short sample) {
		return static_cast< float >(sample) / 32768.0f;
	}

	/// Convert a model-domain float sample back to 16 bit PCM, clipping to the
	/// representable range so the cast cannot overflow.
	inline short floatToInt16(float sample) {
		const float scaled = sample * 32768.0f;
		return static_cast< short >(std::clamp(scaled, static_cast< float >(std::numeric_limits< short >::min()),
											   static_cast< float >(std::numeric_limits< short >::max())));
	}

	/// Clamp the configured attenuation limit into DeepFilterNet's valid dB range.
	inline float clampAttenLimitDb(int settingDb) {
		return static_cast< float >(std::clamp(settingDb, ATTEN_LIMIT_MIN_DB, ATTEN_LIMIT_MAX_DB));
	}

	/// Convert the configured post-filter setting (beta x 1000) into the beta the
	/// library takes, clamped to the supported range.
	inline float postFilterBeta(int setting) {
		return static_cast< float >(std::clamp(setting, POST_FILTER_MIN, POST_FILTER_MAX)) / POST_FILTER_BETA_SCALE;
	}

} // namespace DeepFilter
} // namespace Mumble

#endif
