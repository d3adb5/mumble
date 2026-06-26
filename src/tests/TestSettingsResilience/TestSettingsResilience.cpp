// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore>
#include <QtTest>

#include <nlohmann/json.hpp>

#include "JSONSerialization.h"
#include "Settings.h"

// Verifies that an unreadable value in the serialized settings does not discard
// the rest: from_json keeps every value it can, resets only the broken one to its
// default, and reports the broken key via settingsLoadFailures().
class TestSettingsResilience : public QObject {
	Q_OBJECT
private slots:
	void badValue_keepsOtherSettingsAndReportsFailure();
	void allValid_reportsNoFailures();
};

void TestSettingsResilience::badValue_keepsOtherSettingsAndReportsFailure() {
	// Start from a valid serialization with a couple of non-default values.
	Settings original;
	original.iQuality        = 40000;
	original.noiseCancelMode = Settings::NoiseCancelOff;

	nlohmann::json j;
	to_json(j, original);

	// Corrupt one value: an enum stored as a string this build does not know.
	j["audio"]["noise_cancel_mode"] = "NoSuchNoiseCancelMode";

	Settings loaded;
	bool threw = false;
	try {
		from_json(j, loaded);
	} catch (...) {
		threw = true;
	}

	// Loading must not abort, the unrelated value must survive, and the broken one
	// must fall back to its default and be reported.
	QVERIFY(!threw);
	QCOMPARE(loaded.iQuality, 40000);
	QCOMPARE(loaded.noiseCancelMode, Settings().noiseCancelMode);
	QVERIFY(settingsLoadFailures().contains(QStringLiteral("noise_cancel_mode")));
}

void TestSettingsResilience::allValid_reportsNoFailures() {
	Settings original;
	original.iQuality = 12345;

	nlohmann::json j;
	to_json(j, original);

	Settings loaded;
	from_json(j, loaded);

	QCOMPARE(loaded.iQuality, 12345);
	QVERIFY(settingsLoadFailures().isEmpty());
}

QTEST_MAIN(TestSettingsResilience)
#include "TestSettingsResilience.moc"
