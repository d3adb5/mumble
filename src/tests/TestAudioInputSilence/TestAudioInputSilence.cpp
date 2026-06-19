// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore>
#include <QtTest>

#include "AudioInputSilence.h"

using namespace Mumble::SilenceDetection;

/// Unit tests for the pure silence-detection helpers backing the talking-silent
/// icon, the silent-transmission logging and the silence bitrate reduction.
class TestAudioInputSilence : public QObject {
	Q_OBJECT
private slots:
	void isSilent_threshold();
	void audibleWithinHold_noAudioYet();
	void audibleWithinHold_recentlyAudible();
	void audibleWithinHold_silentPastHold();
	void audibleWithinHold_receiveLapseCountsAudible();
	void transmitBitrate_data();
	void transmitBitrate();
};

void TestAudioInputSilence::isSilent_threshold() {
	// Right at the threshold counts as audible; just below is silence.
	QVERIFY(isSilent(0.0f));
	QVERIFY(isSilent(AUDIBLE_RMS_THRESHOLD - 0.0001f));
	QVERIFY(!isSilent(AUDIBLE_RMS_THRESHOLD));
	QVERIFY(!isSilent(0.5f));
}

void TestAudioInputSilence::audibleWithinHold_noAudioYet() {
	// No frame received yet: nothing to measure, so the user counts as audible.
	QVERIFY(audibleWithinHold(false, 0, false, 0, 1000));
}

void TestAudioInputSilence::audibleWithinHold_recentlyAudible() {
	// Receiving audio and audible within the hold window.
	QVERIFY(audibleWithinHold(true, 20, true, 20, 1000));
}

void TestAudioInputSilence::audibleWithinHold_silentPastHold() {
	// Still receiving frames, but the last audible frame is older than the hold:
	// the stream is being transmitted as silence.
	QVERIFY(!audibleWithinHold(true, 20, true, 1500, 1000));
	// Never been audible while receiving -> silent.
	QVERIFY(!audibleWithinHold(true, 20, false, 0, 1000));
}

void TestAudioInputSilence::audibleWithinHold_receiveLapseCountsAudible() {
	// If no frame has been received for longer than the hold, there is no live
	// stream to judge, so the user is not considered silent.
	QVERIFY(audibleWithinHold(true, 1500, true, 5000, 1000));
}

void TestAudioInputSilence::transmitBitrate_data() {
	QTest::addColumn< int >("base");
	QTest::addColumn< int >("floor");
	QTest::addColumn< bool >("silent");
	QTest::addColumn< bool >("reduce");
	QTest::addColumn< int >("expected");

	// Speech is always sent at the unchanged base bitrate.
	QTest::newRow("speech, reduction on") << 40000 << 8000 << false << true << 40000;
	QTest::newRow("speech, reduction off") << 40000 << 8000 << false << false << 40000;
	// Silence with the reduction enabled drops to the floor.
	QTest::newRow("silence, reduction on") << 40000 << 8000 << true << true << 8000;
	// Silence with the reduction disabled keeps the base bitrate.
	QTest::newRow("silence, reduction off") << 40000 << 8000 << true << false << 40000;
	// The floor never raises a base bitrate that is already below it.
	QTest::newRow("base below floor") << 6000 << 8000 << true << true << 6000;
}

void TestAudioInputSilence::transmitBitrate() {
	QFETCH(int, base);
	QFETCH(int, floor);
	QFETCH(bool, silent);
	QFETCH(bool, reduce);
	QFETCH(int, expected);

	// Qualified to reach the helper rather than this slot of the same name.
	QCOMPARE(Mumble::SilenceDetection::transmitBitrate(base, floor, silent, reduce), expected);
}

QTEST_MAIN(TestAudioInputSilence)
#include "TestAudioInputSilence.moc"
