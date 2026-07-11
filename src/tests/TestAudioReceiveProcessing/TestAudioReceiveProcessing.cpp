// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore>
#include <QtTest>

#include <array>
#include <cmath>

#include "AudioInputAmplification.h"
#include "AudioReceiveProcessing.h"

using namespace Mumble::ReceiveProcessing;

/// Unit tests for the pure receive-side processing helpers: the SNR estimate
/// of a decoded stream and the SNR-gated amplification derived from it. These
/// helpers only ever run on audio received from other users - the disabled
/// state must therefore be an exact identity, which is covered explicitly.
class TestAudioReceiveProcessing : public QObject {
	Q_OBJECT
private slots:
	void rmsOfKnownSignals();
	void trackerConstantNoiseHasZeroSnr();
	void trackerSeparatesSpeechFromNoise();
	void trackerNoiseFloorFallsQuicklyRisesSlowly();
	void frameSnrGatesNoiseFrames();
	void speechinessGate();
	void desiredGainCappedAndNeverNegative();
	void gainIdentityAtUnity();
	void gainScalesAndClamps();
	void gainKeepsStereoBalance();
	void disabledAmplificationIsIdentity();
	void suppressionFallbacks();
};

void TestAudioReceiveProcessing::rmsOfKnownSignals() {
	QCOMPARE(frameRms(nullptr, 0), 0.0f);

	std::array< float, 4 > constant = { 0.5f, 0.5f, 0.5f, 0.5f };
	QVERIFY(std::abs(frameRms(constant.data(), static_cast< unsigned int >(constant.size())) - 0.5f) < 1e-6f);

	std::array< float, 4 > alternating = { 0.5f, -0.5f, 0.5f, -0.5f };
	QVERIFY(std::abs(frameRms(alternating.data(), static_cast< unsigned int >(alternating.size())) - 0.5f) < 1e-6f);

	std::array< float, 4 > silence = {};
	QCOMPARE(frameRms(silence.data(), static_cast< unsigned int >(silence.size())), 0.0f);
}

void TestAudioReceiveProcessing::trackerConstantNoiseHasZeroSnr() {
	SnrTracker tracker;
	// A stream that is nothing but steady noise: envelope and floor coincide.
	for (int i = 0; i < 500; ++i) {
		tracker.update(0.01f);
	}
	QVERIFY(tracker.snrDb() < 1.0f);
}

void TestAudioReceiveProcessing::trackerSeparatesSpeechFromNoise() {
	SnrTracker tracker;
	// Speech bursts (0.3) over background noise (0.01), i.e. a true SNR of
	// roughly 29.5 dB. The pauses let the floor re-anchor on the noise.
	for (int burst = 0; burst < 5; ++burst) {
		for (int i = 0; i < 60; ++i) {
			tracker.update(0.01f);
		}
		for (int i = 0; i < 40; ++i) {
			tracker.update(0.3f);
		}
	}
	QVERIFY(tracker.snrDb() > 20.0f);
	QVERIFY(tracker.signalLevel > 0.25f);
	QVERIFY(tracker.noiseLevel < 0.05f);
}

void TestAudioReceiveProcessing::trackerNoiseFloorFallsQuicklyRisesSlowly() {
	SnrTracker tracker;
	for (int i = 0; i < 100; ++i) {
		tracker.update(0.1f);
	}
	const float floorBefore = tracker.noiseLevel;

	// A handful of quiet frames pulls the floor most of the way down...
	for (int i = 0; i < 20; ++i) {
		tracker.update(0.001f);
	}
	QVERIFY(tracker.noiseLevel < floorBefore * 0.1f);

	// ...but a loud stretch of the same length barely lifts it: this is what
	// keeps ongoing speech from being mistaken for a rising noise floor.
	const float floorLow = tracker.noiseLevel;
	for (int i = 0; i < 20; ++i) {
		tracker.update(0.1f);
	}
	QVERIFY(tracker.noiseLevel < floorLow * 1.2f);
}

void TestAudioReceiveProcessing::frameSnrGatesNoiseFrames() {
	SnrTracker tracker;
	for (int i = 0; i < 200; ++i) {
		tracker.update(0.01f);
	}

	// A frame at the noise floor carries no amplifiable signal, a burst well
	// above it opens the gate completely.
	QCOMPARE(speechiness(tracker.frameSnrDb(0.01f)), 0.0f);
	QCOMPARE(speechiness(tracker.frameSnrDb(0.3f)), 1.0f);
}

void TestAudioReceiveProcessing::speechinessGate() {
	QCOMPARE(speechiness(GATE_OPEN_DB), 0.0f);
	QCOMPARE(speechiness(GATE_FULL_DB), 1.0f);
	QCOMPARE(speechiness(0.0f), 0.0f);
	QCOMPARE(speechiness(SNR_MAX_DB), 1.0f);

	const float mid = 0.5f * (GATE_OPEN_DB + GATE_FULL_DB);
	QVERIFY(std::abs(speechiness(mid) - 0.5f) < 1e-6f);
}

void TestAudioReceiveProcessing::desiredGainCappedAndNeverNegative() {
	// A signal already at (or above) the target is left alone.
	QCOMPARE(desiredGainDb(TARGET_LEVEL, 30.0f), 0.0f);
	QCOMPARE(desiredGainDb(1.0f, 30.0f), 0.0f);

	// A quiet signal is lifted, but never beyond the configured maximum.
	QCOMPARE(desiredGainDb(0.001f, 12.0f), 12.0f);

	// Halving the level asks for +6 dB.
	QVERIFY(std::abs(desiredGainDb(TARGET_LEVEL / 2.0f, 30.0f) - 6.0206f) < 0.001f);

	// A non-positive maximum disables the amplification outright.
	QCOMPARE(desiredGainDb(0.001f, 0.0f), 0.0f);
	QCOMPARE(desiredGainDb(0.001f, -10.0f), 0.0f);
}

void TestAudioReceiveProcessing::gainIdentityAtUnity() {
	std::array< float, 6 > samples  = { 0.1f, -0.2f, 0.999f, -0.999f, 0.0f, 0.5f };
	std::array< float, 6 > original = samples;

	applyGainClamped(samples.data(), static_cast< unsigned int >(samples.size()), 1.0f);
	QVERIFY(samples == original);
}

void TestAudioReceiveProcessing::gainScalesAndClamps() {
	std::array< float, 3 > samples = { 0.25f, -0.25f, 0.9f };
	applyGainClamped(samples.data(), static_cast< unsigned int >(samples.size()), 2.0f);
	QVERIFY(std::abs(samples[0] - 0.5f) < 1e-6f);
	QVERIFY(std::abs(samples[1] + 0.5f) < 1e-6f);
	// Amplified past full scale, the sample is clamped instead of wrapping.
	QCOMPARE(samples[2], 1.0f);
}

void TestAudioReceiveProcessing::gainKeepsStereoBalance() {
	// Interleaved L/R pairs with a fixed ratio between the channels.
	std::array< float, 6 > stereo = { 0.1f, 0.2f, -0.15f, -0.3f, 0.05f, 0.1f };
	applyGainClamped(stereo.data(), static_cast< unsigned int >(stereo.size()), 1.5f);
	for (std::size_t frame = 0; frame < 3; ++frame) {
		QVERIFY(std::abs(stereo[2 * frame + 1] - 2.0f * stereo[2 * frame]) < 1e-6f);
	}
}

void TestAudioReceiveProcessing::disabledAmplificationIsIdentity() {
	// With the feature disabled the engine eases its gain towards 0 dB; once
	// there, the applied factor is exactly 1 and the audio passes through
	// bit-identical. This is the invariant that guarantees streams (and in
	// particular anything outside the receive path) are not altered unless
	// the user opts in.
	float gainDb = 3.0f;
	for (int i = 0; i < 100; ++i) {
		gainDb = Mumble::Amplification::approach(gainDb, 0.0f, GAIN_STEP_DB_PER_FRAME);
	}
	QCOMPARE(gainDb, 0.0f);
	QCOMPARE(Mumble::Amplification::dbToLinear(gainDb), 1.0f);

	std::array< float, 4 > samples  = { 0.3f, -0.7f, 0.1f, 0.0f };
	std::array< float, 4 > original = samples;
	applyGainClamped(samples.data(), static_cast< unsigned int >(samples.size()),
					 Mumble::Amplification::dbToLinear(gainDb));
	QVERIFY(samples == original);
}

void TestAudioReceiveProcessing::suppressionFallbacks() {
	const SuppressionSupport none{ false, false, false };
	const SuppressionSupport all{ true, true, true };

	// Off and Speex are always available.
	QCOMPARE(resolveSuppression(Suppression::Off, none), Suppression::Off);
	QCOMPARE(resolveSuppression(Suppression::Speex, none), Suppression::Speex);

	// Everything else falls back to Speex when the backend is missing...
	QCOMPARE(resolveSuppression(Suppression::RNN, none), Suppression::Speex);
	QCOMPARE(resolveSuppression(Suppression::Both, none), Suppression::Speex);
	QCOMPARE(resolveSuppression(Suppression::WebRTC, none), Suppression::Speex);
	QCOMPARE(resolveSuppression(Suppression::DeepFilter, none), Suppression::Speex);

	// ...and is kept as-is when it is provided.
	QCOMPARE(resolveSuppression(Suppression::RNN, all), Suppression::RNN);
	QCOMPARE(resolveSuppression(Suppression::Both, all), Suppression::Both);
	QCOMPARE(resolveSuppression(Suppression::WebRTC, all), Suppression::WebRTC);
	QCOMPARE(resolveSuppression(Suppression::DeepFilter, all), Suppression::DeepFilter);
}

QTEST_MAIN(TestAudioReceiveProcessing)
#include "TestAudioReceiveProcessing.moc"
