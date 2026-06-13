// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore>
#include <QtTest>

#include <array>

#include "AudioInputAmplification.h"

using namespace Mumble::Amplification;

/// Unit tests for the pure input amplification helpers. These back the
/// base/adaptive/maximum amplification levels and the uniform gain that is
/// applied to mono and stereo frames alike.
class TestAudioAmplification : public QObject {
	Q_OBJECT
private slots:
	void gainForLoudness_data();
	void gainForLoudness();
	void gainForLoudness_monotonic();
	void linearFromDb();
	void adaptiveSentinel();
	void ceilingInterpolation();
	void baseFloor();
	void gainScalesAndClamps();
	void gainKeepsStereoBalance();
	void peakLimiter();
	void stereoDownmix();
};

void TestAudioAmplification::gainForLoudness_data() {
	QTest::addColumn< int >("loudness");
	QTest::addColumn< int >("expected");

	// At the target there is nothing to amplify.
	QTest::newRow("target") << 30000 << 0;
	// Historic default: 20*log10(30000/1000) = 29.54, floored.
	QTest::newRow("default") << 1000 << 29;
	QTest::newRow("3000") << 3000 << 20;
	// Out-of-range values are clamped, never producing a negative gain.
	QTest::newRow("above-target") << 40000 << 0;
	QTest::newRow("zero") << 0 << static_cast< int >(20.0 * std::log10(30000.0));
}

void TestAudioAmplification::gainForLoudness() {
	QFETCH(int, loudness);
	QFETCH(int, expected);

	QCOMPARE(gainDbForLoudness(loudness), expected);
}

void TestAudioAmplification::gainForLoudness_monotonic() {
	// Asking for a lower loudness (a quieter floor) must never decrease the gain.
	int previous = -1;
	for (int loudness = 30000; loudness >= 500; loudness -= 500) {
		const int gain = gainDbForLoudness(loudness);
		QVERIFY(gain >= previous);
		previous = gain;
	}
}

void TestAudioAmplification::linearFromDb() {
	QVERIFY(qFuzzyCompare(dbToLinear(0.0f), 1.0f));
	QVERIFY(std::abs(dbToLinear(20.0f) - 10.0f) < 0.001f);
	QVERIFY(std::abs(dbToLinear(-20.0f) - 0.1f) < 0.001f);
	QVERIFY(std::abs(dbToLinear(6.0206f) - 2.0f) < 0.001f);
}

void TestAudioAmplification::adaptiveSentinel() {
	// The non-positive sentinel means "follow the maximum".
	QCOMPARE(resolveAdaptiveLoudness(0, 1000), 1000);
	QCOMPARE(resolveAdaptiveLoudness(-1, 2500), 2500);
	// A real value is kept as-is.
	QCOMPARE(resolveAdaptiveLoudness(5000, 1000), 5000);
}

void TestAudioAmplification::ceilingInterpolation() {
	// Pure noise caps at the adaptive ceiling, pure speech reaches the maximum.
	QCOMPARE(gainCeilingDb(10.0f, 30.0f, 0.0f), 10.0f);
	QCOMPARE(gainCeilingDb(10.0f, 30.0f, 1.0f), 30.0f);
	QCOMPARE(gainCeilingDb(10.0f, 30.0f, 0.5f), 20.0f);
	// Speechiness is clamped to [0, 1].
	QCOMPARE(gainCeilingDb(10.0f, 30.0f, -5.0f), 10.0f);
	QCOMPARE(gainCeilingDb(10.0f, 30.0f, 5.0f), 30.0f);
}

void TestAudioAmplification::baseFloor() {
	// The base floor lifts quiet input up to itself...
	QCOMPARE(effectiveGainDb(3.0f, 10.0f), 10.0f);
	// ...but leaves a louder AGC gain untouched.
	QCOMPARE(effectiveGainDb(20.0f, 10.0f), 20.0f);
	// A zero base is a no-op.
	QCOMPARE(effectiveGainDb(5.0f, 0.0f), 5.0f);
	// The base floor must not fight an attenuation of an already-loud signal.
	QCOMPARE(effectiveGainDb(-4.0f, 6.0f), -4.0f);
}

void TestAudioAmplification::gainScalesAndClamps() {
	std::array< short, 4 > samples = { 100, -200, 20000, -20000 };
	applyGain(samples.data(), static_cast< unsigned int >(samples.size()), 2.0f);
	QCOMPARE(samples[0], static_cast< short >(200));
	QCOMPARE(samples[1], static_cast< short >(-400));
	// 20000 * 2 saturates rather than wrapping around.
	QCOMPARE(samples[2], static_cast< short >(32767));
	QCOMPARE(samples[3], static_cast< short >(-32768));

	// A unity factor leaves the data untouched.
	std::array< short, 2 > unchanged = { 1234, -4321 };
	applyGain(unchanged.data(), static_cast< unsigned int >(unchanged.size()), 1.0f);
	QCOMPARE(unchanged[0], static_cast< short >(1234));
	QCOMPARE(unchanged[1], static_cast< short >(-4321));
}

void TestAudioAmplification::gainKeepsStereoBalance() {
	// Interleaved L/R, with R consistently half of L. The same factor applies
	// to both channels, so the ratio is preserved.
	std::array< short, 6 > stereo = { 1000, 500, 2000, 1000, 4000, 2000 };
	applyGain(stereo.data(), static_cast< unsigned int >(stereo.size()), 1.5f);
	for (unsigned int i = 0; i < stereo.size(); i += 2) {
		QCOMPARE(stereo[i + 1], static_cast< short >(stereo[i] / 2));
	}
}

void TestAudioAmplification::peakLimiter() {
	// Comfortably below the target: no limiting.
	std::array< short, 3 > quiet = { 100, -200, 300 };
	QCOMPARE(peakLimitFactor(quiet.data(), static_cast< unsigned int >(quiet.size()), 30000.0f), 1.0f);

	// A 32000 peak is pulled back down to the 30000 target.
	std::array< short, 3 > loud = { 1000, -32000, 500 };
	const float factor          = peakLimitFactor(loud.data(), static_cast< unsigned int >(loud.size()), 30000.0f);
	QVERIFY(std::abs(factor - 30000.0f / 32000.0f) < 0.0001f);
}

void TestAudioAmplification::stereoDownmix() {
	std::array< short, 6 > stereo = { 1000, 2000, -1000, -3000, 32767, 32767 };
	std::array< short, 3 > mono   = {};
	downmixStereoToMono(stereo.data(), mono.data(), static_cast< unsigned int >(mono.size()));
	QCOMPARE(mono[0], static_cast< short >(1500));
	QCOMPARE(mono[1], static_cast< short >(-2000));
	// The average of two equal peaks stays at the peak (no overflow).
	QCOMPARE(mono[2], static_cast< short >(32767));
}

QTEST_MAIN(TestAudioAmplification)
#include "TestAudioAmplification.moc"
