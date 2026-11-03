// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore>
#include <QtTest>

#include "IPCUtils.h"

#include <filesystem>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
fs::perms modeOf(const fs::path &path) {
	return fs::status(path).permissions() & fs::perms::mask;
}
} // namespace

class TestIPCUtils : public QObject {
	Q_OBJECT

private:
	QTemporaryDir m_dir;
	QByteArray m_oldXdg;
	QByteArray m_oldTmp;

	fs::path base() const { return fs::path(m_dir.path().toStdString()); }

private slots:
	void init() {
		QVERIFY(m_dir.isValid());
		m_oldXdg = qgetenv("XDG_RUNTIME_DIR");
		m_oldTmp = qgetenv("TMPDIR");
		qputenv("XDG_RUNTIME_DIR", m_dir.path().toLocal8Bit());
	}

	void cleanup() {
		qputenv("XDG_RUNTIME_DIR", m_oldXdg);
		qputenv("TMPDIR", m_oldTmp);
		fs::remove_all(base() / "info.mumble.Mumble");
	}

	void usesXdgRuntimeDir() {
		const fs::path dir = Mumble::getRuntimeDirectory();
		QCOMPARE(QString::fromStdString(dir.string()),
				 QString::fromStdString((base() / "info.mumble.Mumble").string()));
		QVERIFY(fs::is_directory(dir));
		QCOMPARE(modeOf(dir), fs::perms::owner_all);
	}

	void fallsBackToTempDirPerUser() {
		qunsetenv("XDG_RUNTIME_DIR");
		qputenv("TMPDIR", m_dir.path().toLocal8Bit());

		const fs::path dir = Mumble::getRuntimeDirectory();
		QCOMPARE(QString::fromStdString(dir.string()),
				 QString::fromStdString((base() / ("info.mumble.Mumble_" + std::to_string(getuid()))).string()));
		QCOMPARE(modeOf(dir), fs::perms::owner_all);
		fs::remove_all(dir);
	}

	void tightensAnOpenDirectory() {
		// Created by an older version (or with a lax umask): others must not get to the sockets.
		const fs::path dir = base() / "info.mumble.Mumble";
		fs::create_directories(dir);
		fs::permissions(dir, fs::perms::all, fs::perm_options::replace);

		QCOMPARE(Mumble::getRuntimeDirectory(), dir);
		QCOMPARE(modeOf(dir), fs::perms::owner_all);
	}

	void refusesASymlink() {
		// Whoever planted the link controls where it points to, so it is not ours to use.
		const fs::path target = base() / "elsewhere";
		fs::create_directories(target);
		fs::create_directory_symlink(target, base() / "info.mumble.Mumble");

		QVERIFY_THROWS_EXCEPTION(fs::filesystem_error, Mumble::getRuntimeDirectory());
		QVERIFY_THROWS_EXCEPTION(fs::filesystem_error, Mumble::getSocketPath("Mumble"));
	}

	void socketPathIsInsideTheRuntimeDirectory() {
		QCOMPARE(Mumble::getSocketPath("Mumble"), base() / "info.mumble.Mumble" / "MumbleSocket");
		QCOMPARE(Mumble::getOverlayPipePath(), base() / "info.mumble.Mumble" / "MumbleOverlayPipe");
	}
};

QTEST_MAIN(TestIPCUtils)
#include "TestIPCUtils.moc"
