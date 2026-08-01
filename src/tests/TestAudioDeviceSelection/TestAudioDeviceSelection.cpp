// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore>
#include <QtTest>

#include "AudioDeviceSelection.h"

using namespace Mumble::AudioDeviceSelection;

typedef QList< QPair< QString, QVariant > > ChoiceList;

/// Unit tests for the device combo box model: the offered choices plus a
/// placeholder for the configured device while it is unplugged, so hotplug
/// refreshes never silently move the selection to another device.
class TestAudioDeviceSelection : public QObject {
	Q_OBJECT
private slots:
	void selectsCurrentChoice();
	void keepsChoiceOrder();
	void missingCurrentGetsPlaceholder();
	void placeholderRoundTrip();
	void emptyCurrentFallsBackToFirst();
	void invalidCurrentFallsBackToFirst();
	void noChoicesNoCurrent();
	void noChoicesWithCurrent();
};

static ChoiceList someDevices() {
	ChoiceList choices;
	choices << qMakePair(QStringLiteral("Default Input"), QVariant(QString()));
	choices << qMakePair(QStringLiteral("Headset Microphone"), QVariant(QStringLiteral("headset")));
	choices << qMakePair(QStringLiteral("USB Microphone"), QVariant(QStringLiteral("usb-mic")));
	return choices;
}

void TestAudioDeviceSelection::selectsCurrentChoice() {
	const ComboModel model = buildComboModel(someDevices(), QStringLiteral("usb-mic"));

	QCOMPARE(model.entries.size(), 3);
	QCOMPARE(model.currentIndex, 2);
	QVERIFY(!model.entries.at(2).unavailable);
}

void TestAudioDeviceSelection::keepsChoiceOrder() {
	const ComboModel model = buildComboModel(someDevices(), QStringLiteral("headset"));

	QCOMPARE(model.entries.at(0).label, QStringLiteral("Default Input"));
	QCOMPARE(model.entries.at(1).label, QStringLiteral("Headset Microphone"));
	QCOMPARE(model.entries.at(2).label, QStringLiteral("USB Microphone"));
	QCOMPARE(model.currentIndex, 1);
}

void TestAudioDeviceSelection::missingCurrentGetsPlaceholder() {
	const ComboModel model = buildComboModel(someDevices(), QStringLiteral("unplugged-mic"));

	QCOMPARE(model.entries.size(), 4);
	QCOMPARE(model.currentIndex, 3);
	QVERIFY(model.entries.at(3).unavailable);
	QCOMPARE(model.entries.at(3).label, QStringLiteral("unplugged-mic"));
	QCOMPARE(model.entries.at(3).value.toString(), QStringLiteral("unplugged-mic"));
}

void TestAudioDeviceSelection::placeholderRoundTrip() {
	// Once the device returns, the same current value must match the real
	// entry again instead of keeping the placeholder.
	ChoiceList choices = someDevices();
	choices << qMakePair(QStringLiteral("Unplugged Microphone"), QVariant(QStringLiteral("unplugged-mic")));

	const ComboModel model = buildComboModel(choices, QStringLiteral("unplugged-mic"));

	QCOMPARE(model.entries.size(), 4);
	QCOMPARE(model.currentIndex, 3);
	QVERIFY(!model.entries.at(3).unavailable);
	QCOMPARE(model.entries.at(3).label, QStringLiteral("Unplugged Microphone"));
}

void TestAudioDeviceSelection::emptyCurrentFallsBackToFirst() {
	// An empty device id that is offered (PulseAudio's default device) matches
	// its entry; one that is not offered cannot get a placeholder.
	ChoiceList choices;
	choices << qMakePair(QStringLiteral("Speakers"), QVariant(QStringLiteral("speakers")));

	const ComboModel model = buildComboModel(choices, QVariant(QString()));

	QCOMPARE(model.entries.size(), 1);
	QCOMPARE(model.currentIndex, 0);
}

void TestAudioDeviceSelection::invalidCurrentFallsBackToFirst() {
	const ComboModel model = buildComboModel(someDevices(), QVariant());

	QCOMPARE(model.entries.size(), 3);
	QCOMPARE(model.currentIndex, 0);
}

void TestAudioDeviceSelection::noChoicesNoCurrent() {
	const ComboModel model = buildComboModel(ChoiceList(), QVariant());

	QVERIFY(model.entries.isEmpty());
	QCOMPARE(model.currentIndex, -1);
}

void TestAudioDeviceSelection::noChoicesWithCurrent() {
	// Mid-requery the backend may briefly offer nothing; the configured device
	// still has to stay visible.
	const ComboModel model = buildComboModel(ChoiceList(), QStringLiteral("headset"));

	QCOMPARE(model.entries.size(), 1);
	QCOMPARE(model.currentIndex, 0);
	QVERIFY(model.entries.at(0).unavailable);
}

QTEST_MAIN(TestAudioDeviceSelection)
#include "TestAudioDeviceSelection.moc"
