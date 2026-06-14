// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtTest>

#include <nlohmann/json.hpp>

#include "JSONSerialization.h"
#include "Settings.h"

/// Tests for input settings profiles: capturing the input subset of the
/// settings under a name and restoring it, leaving everything else untouched.
class TestSettingsProfiles : public QObject {
	Q_OBJECT
private slots:
	void saveAndApply();
	void excludesNonInputAndDevice();
	void applyMissingIsNoop();
	void removeProfile();
	void survivesSerialization();
};

void TestSettingsProfiles::saveAndApply() {
	Settings s;
	s.iMinLoudness         = 1234;
	s.iAmplificationRiseMs = 700;
	s.saveSettingsProfile(QStringLiteral("input"), QStringLiteral("Gaming"));

	QVERIFY(s.settingsProfileNames(QStringLiteral("input")).contains(QStringLiteral("Gaming")));

	// Change the captured settings, then restore them from the profile.
	s.iMinLoudness         = 5678;
	s.iAmplificationRiseMs = 100;
	s.applySettingsProfile(QStringLiteral("input"), QStringLiteral("Gaming"));
	QCOMPARE(s.iMinLoudness, 1234);
	QCOMPARE(s.iAmplificationRiseMs, 700);
}

void TestSettingsProfiles::excludesNonInputAndDevice() {
	Settings s;
	s.iMinLoudness = 1234;                       // input -> captured
	s.fVolume      = 0.30f;                       // output (audio category) -> not captured
	s.qsAudioInput = QStringLiteral("DeviceA");  // device selection -> not captured
	s.saveSettingsProfile(QStringLiteral("input"), QStringLiteral("P"));

	s.iMinLoudness = 9999;
	s.fVolume      = 0.90f;
	s.qsAudioInput = QStringLiteral("DeviceB");
	s.applySettingsProfile(QStringLiteral("input"), QStringLiteral("P"));

	QCOMPARE(s.iMinLoudness, 1234);                          // restored from the profile
	QCOMPARE(s.fVolume, 0.90f);                              // untouched (not an input setting)
	QCOMPARE(s.qsAudioInput, QStringLiteral("DeviceB"));     // untouched (device excluded)
}

void TestSettingsProfiles::applyMissingIsNoop() {
	Settings s;
	s.iMinLoudness = 42;
	s.applySettingsProfile(QStringLiteral("input"), QStringLiteral("DoesNotExist"));
	QCOMPARE(s.iMinLoudness, 42);
}

void TestSettingsProfiles::removeProfile() {
	Settings s;
	s.saveSettingsProfile(QStringLiteral("input"), QStringLiteral("A"));
	s.saveSettingsProfile(QStringLiteral("input"), QStringLiteral("B"));
	QCOMPARE(s.settingsProfileNames(QStringLiteral("input")).size(), 2);

	s.removeSettingsProfile(QStringLiteral("input"), QStringLiteral("A"));
	QCOMPARE(s.settingsProfileNames(QStringLiteral("input")), QStringList() << QStringLiteral("B"));
}

void TestSettingsProfiles::survivesSerialization() {
	Settings s;
	s.iMinLoudness = 1111;
	s.saveSettingsProfile(QStringLiteral("input"), QStringLiteral("Persisted"));

	const nlohmann::json j = s;  // to_json
	const Settings loaded  = j;  // from_json

	QVERIFY(loaded.settingsProfileNames(QStringLiteral("input")).contains(QStringLiteral("Persisted")));

	Settings t     = loaded;
	t.iMinLoudness = 2222;
	t.applySettingsProfile(QStringLiteral("input"), QStringLiteral("Persisted"));
	QCOMPARE(t.iMinLoudness, 1111);
}

QTEST_MAIN(TestSettingsProfiles)
#include "TestSettingsProfiles.moc"
