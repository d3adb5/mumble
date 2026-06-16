// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_WEBRTCAUDIOPROCESSING_H_
#define MUMBLE_MUMBLE_WEBRTCAUDIOPROCESSING_H_

#include <memory>

/// Thin wrapper around the WebRTC audio processing module (APM).
///
/// It exposes the two features Mumble cares about - the AEC3 acoustic echo
/// canceller and the WebRTC noise suppressor - while hiding the (heavyweight)
/// WebRTC and Abseil headers behind a PIMPL so that they only ever get included
/// by the single corresponding translation unit.
///
/// The processor works on 10 ms frames of interleaved 16 bit PCM at a fixed
/// sample rate (48 kHz in Mumble's pipeline). Echo cancellation is only ever
/// used with a mono microphone and a mono speaker reference.
class WebRTCAudioProcessor {
public:
	/// Aggressiveness of the noise suppressor, mirrors WebRTC's levels.
	enum class NoiseLevel { Low, Moderate, High, VeryHigh };

	/// Construct and configure the APM.
	///
	/// \param sampleRate        Sample rate of the frames in Hz (e.g. 48000).
	/// \param channels          Number of (interleaved) channels per frame.
	/// \param echoCancel        Enable the AEC3 echo canceller.
	/// \param noiseSuppression  Enable the noise suppressor.
	/// \param nsLevel           Noise suppressor aggressiveness.
	/// \param gainControl       Enable WebRTC's adaptive digital gain controller.
	WebRTCAudioProcessor(int sampleRate, int channels, bool echoCancel, bool noiseSuppression, NoiseLevel nsLevel,
						 bool gainControl);
	~WebRTCAudioProcessor();

	WebRTCAudioProcessor(const WebRTCAudioProcessor &)            = delete;
	WebRTCAudioProcessor &operator=(const WebRTCAudioProcessor &) = delete;

	/// \return whether the APM was created successfully.
	bool isValid() const;

	/// \return whether echo cancellation is active in this instance.
	bool echoCancelEnabled() const;

	/// \return whether noise suppression is active in this instance.
	bool noiseSuppressionEnabled() const;

	/// Feed one 10 ms frame of the speaker (render/far-end) signal to the echo
	/// canceller. Must be called before processCapture() for the same period.
	///
	/// \param speaker Interleaved 16 bit PCM, one 10 ms frame.
	void analyzeReverseStream(const short *speaker);

	/// Process one 10 ms frame of the microphone (capture/near-end) signal in
	/// place, applying echo cancellation and/or noise suppression.
	///
	/// \param mic           Interleaved 16 bit PCM, one 10 ms frame; modified in place.
	/// \param streamDelayMs Best estimate of the render-to-capture delay in ms.
	void processCapture(short *mic, int streamDelayMs);

private:
	struct Impl;
	std::unique_ptr< Impl > m_impl;
};

#endif // MUMBLE_MUMBLE_WEBRTCAUDIOPROCESSING_H_
