// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore>
#include <QtTest>

#include "RecordingTarget.h"

using namespace Mumble::RecordingTarget;

/// Unit tests for the file a recording is written to, which is derived from the
/// recording settings whenever a recording is started without the dialog.
class TestRecordingTarget : public QObject {
	Q_OBJECT
private slots:
	void addsMissingExtension();
	void keepsGivenExtension();
	void keepsOnlyTheLastNameAsExtension();
	void emptyNameBecomesUserPattern();
	void keepsPatternsIntact();
	void joinsDirectoryAndName();
	void emptyDirectoryYieldsRelativePath();
};

void TestRecordingTarget::addsMissingExtension() {
	QCOMPARE(resolveFileName(QStringLiteral("recording"), QStringLiteral("wav")), QStringLiteral("recording.wav"));
}

void TestRecordingTarget::keepsGivenExtension() {
	QCOMPARE(resolveFileName(QStringLiteral("recording.flac"), QStringLiteral("wav")),
			 QStringLiteral("recording.flac"));
}

void TestRecordingTarget::keepsOnlyTheLastNameAsExtension() {
	// Mirrors the recorder dialog, which treats everything past the first dot as the
	// extension, so a name with dots in it is left as it is
	QCOMPARE(resolveFileName(QStringLiteral("my.recording.flac"), QStringLiteral("wav")),
			 QStringLiteral("my.recording.flac"));
}

void TestRecordingTarget::emptyNameBecomesUserPattern() {
	QCOMPARE(resolveFileName(QString(), QStringLiteral("opus")), QStringLiteral("%user.opus"));
	QCOMPARE(resolveFileName(QStringLiteral(".flac"), QStringLiteral("wav")), QStringLiteral("%user.flac"));
}

void TestRecordingTarget::keepsPatternsIntact() {
	// The recorder expands these itself, so they have to survive untouched
	QCOMPARE(resolveFileName(QStringLiteral("Mumble-%date-%time-%host-%user"), QStringLiteral("wav")),
			 QStringLiteral("Mumble-%date-%time-%host-%user.wav"));
}

void TestRecordingTarget::joinsDirectoryAndName() {
	QCOMPARE(resolveFilePath(QStringLiteral("/tmp/recordings"), QStringLiteral("session"), QStringLiteral("wav")),
			 QStringLiteral("/tmp/recordings/session.wav"));
	QCOMPARE(resolveFilePath(QStringLiteral("/tmp/recordings/"), QStringLiteral("session.opus"), QStringLiteral("wav")),
			 QStringLiteral("/tmp/recordings/session.opus"));
}

void TestRecordingTarget::emptyDirectoryYieldsRelativePath() {
	// QDir resolves an empty directory against the working directory
	QCOMPARE(resolveFilePath(QString(), QStringLiteral("session"), QStringLiteral("wav")),
			 QDir::current().absoluteFilePath(QStringLiteral("session.wav")));
}

QTEST_MAIN(TestRecordingTarget)
#include "TestRecordingTarget.moc"
