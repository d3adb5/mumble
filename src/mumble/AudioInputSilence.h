// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_AUDIOINPUTSILENCE_H_
#define MUMBLE_MUMBLE_AUDIOINPUTSILENCE_H_

#include <algorithm>

// Pure, dependency-free helpers backing Mumble's silence detection: classifying a
// frame as silence, deciding whether a transmitting user is still audible within
// the hold window, and lowering the transmit bitrate while the stream is silent.
// Kept free of Qt so they can be unit tested on their own.
namespace Mumble {
namespace SilenceDetection {

	/// RMS (on float samples in [-1, 1]) at or above this counts as audible rather
	/// than silence (-60 dBFS). Shared so the transmit side classifies its own
	/// outgoing frames the same way receivers classify the decoded audio.
	constexpr float AUDIBLE_RMS_THRESHOLD = 0.001f;

	/// Whether a frame at the given RMS power is silence.
	inline bool isSilent(float rmsPower) { return rmsPower < AUDIBLE_RMS_THRESHOLD; }

	/// Whether a transmitting user counts as currently audible, bridging the natural
	/// pauses within speech with a hold window. \p receivedMs and \p audibleMs are the
	/// times (ms) since the last received frame and the last audible frame; pass the
	/// matching \p ...Started = false when no such frame has been seen yet. Without
	/// received audio to measure there is no basis to call the user silent, so they
	/// count as audible.
	inline bool audibleWithinHold(bool receivedStarted, long long receivedMs, bool audibleStarted,
								  long long audibleMs, long long holdMs) {
		if (!receivedStarted || receivedMs > holdMs) {
			return true;
		}
		return audibleStarted && audibleMs < holdMs;
	}

	/// Transmit bitrate to encode at: dropped to \p silenceFloor while the outgoing
	/// stream is silent and the reduction is enabled, otherwise the unchanged
	/// \p baseBitrate.
	inline int transmitBitrate(int baseBitrate, int silenceFloor, bool silent, bool reduceOnSilence) {
		if (silent && reduceOnSilence) {
			return std::min(baseBitrate, silenceFloor);
		}
		return baseBitrate;
	}

} // namespace SilenceDetection
} // namespace Mumble

#endif
