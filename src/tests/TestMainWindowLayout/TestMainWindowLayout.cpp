// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QApplication>
#include <QDockWidget>
#include <QLayout>
#include <QMainWindow>
#include <QTextEdit>
#include <QToolBar>
#include <QTreeView>
#include <QtTest>

#include "MainWindowLayout.h"

/// Verifies the log and chat docks end up where each window layout wants them,
/// and - the reason these functions exist - that arranging them does not trip
/// over a dock layout restore Qt has not been able to apply yet.
///
/// Qt keeps a copy of such a restore around and re-applies it later, but
/// removeDockWidget() deletes the layout items that copy points at. Re-applying
/// it then hands freed pointers back to the layout, and the next traversal
/// dereferences them, which crashes the process rather than failing an
/// assertion. Mumble used to do exactly that on every start after the first one
/// whenever the window had been closed maximized.
class TestMainWindowLayout : public QObject {
	Q_OBJECT
private slots:
	void classicSurvivesDeferredRestore();
	void stackedSurvivesDeferredRestore();
	void hybridSurvivesDeferredRestore();
	void visibleWindowNeedsNoFlush();
	void unmaximizedWindowNeedsNoFlush();
};

namespace {

/// The main window's docks and toolbar, named as MainWindow.ui names them so
/// that a saved state applies to them.
struct Window {
	QMainWindow window;
	QDockWidget *log  = nullptr;
	QDockWidget *chat = nullptr;

	Window() {
		window.setCentralWidget(new QTreeView(&window));

		// The log's minimum size is what keeps a state saved for a maximized
		// window from fitting a smaller one.
		log  = addDock(QStringLiteral("qdwLog"), Qt::LeftDockWidgetArea, QSize(250, 121));
		chat = addDock(QStringLiteral("qdwChat"), Qt::LeftDockWidgetArea, QSize());
		addDock(QStringLiteral("qdwMinimalViewNote"), Qt::TopDockWidgetArea, QSize());

		QToolBar *toolbar = new QToolBar(QStringLiteral("Icon Toolbar"), &window);
		toolbar->setObjectName(QStringLiteral("qtIconToolbar"));
		toolbar->addAction(QStringLiteral("dummy"));
		window.addToolBar(Qt::TopToolBarArea, toolbar);
	}

	QDockWidget *addDock(const QString &name, Qt::DockWidgetArea area, QSize minimumSize) {
		QDockWidget *dock = new QDockWidget(name, &window);
		dock->setObjectName(name);
		dock->setWidget(new QTextEdit(dock));
		if (minimumSize.isValid()) {
			dock->setMinimumSize(minimumSize);
		}
		window.addDockWidget(area, dock);

		return dock;
	}
};

constexpr int STATE_VERSION = 1;

/// A dock layout as it would have been saved from a maximized window.
QByteArray maximizedState() {
	Window saved;
	saved.window.resize(1615, 963);
	saved.window.splitDockWidget(saved.log, saved.chat, Qt::Vertical);
	saved.window.layout()->activate();

	return saved.window.saveState(STATE_VERSION);
}

/// A window in the state Mumble's is in while its constructor runs: not shown
/// yet, flagged maximized by the restored geometry, and holding a dock layout
/// that does not fit its current size.
void restoreWhileHidden(Window &target) {
	target.window.resize(735, 435);
	target.window.setWindowState(Qt::WindowMaximized);
	QVERIFY(target.window.restoreState(maximizedState(), STATE_VERSION));
	QVERIFY(Mumble::WindowLayout::restoreMayBePending(target.window));
}

} // namespace

void TestMainWindowLayout::classicSurvivesDeferredRestore() {
	Window target;
	restoreWhileHidden(target);

	Mumble::WindowLayout::applyClassic(target.window, *target.log, *target.chat);

	QCOMPARE(target.window.dockWidgetArea(target.log), Qt::LeftDockWidgetArea);
	QCOMPARE(target.window.dockWidgetArea(target.chat), Qt::LeftDockWidgetArea);
}

void TestMainWindowLayout::stackedSurvivesDeferredRestore() {
	Window target;
	restoreWhileHidden(target);

	Mumble::WindowLayout::applyStacked(target.window, *target.log, *target.chat);

	QCOMPARE(target.window.dockWidgetArea(target.log), Qt::BottomDockWidgetArea);
	QCOMPARE(target.window.dockWidgetArea(target.chat), Qt::BottomDockWidgetArea);
}

void TestMainWindowLayout::hybridSurvivesDeferredRestore() {
	Window target;
	restoreWhileHidden(target);

	Mumble::WindowLayout::applyHybrid(target.window, *target.log, *target.chat);

	QCOMPARE(target.window.dockWidgetArea(target.log), Qt::LeftDockWidgetArea);
	QCOMPARE(target.window.dockWidgetArea(target.chat), Qt::BottomDockWidgetArea);
}

/// Switching layouts from the settings dialog happens on a window that is up on
/// screen, where Qt has long applied everything it restored. Nothing to flush,
/// so the docks are laid out exactly as they were before.
void TestMainWindowLayout::visibleWindowNeedsNoFlush() {
	Window target;
	target.window.resize(1615, 963);
	target.window.setWindowState(Qt::WindowMaximized);
	target.window.show();
	QVERIFY(target.window.isVisible());

	QVERIFY(!Mumble::WindowLayout::restoreMayBePending(target.window));

	Mumble::WindowLayout::applyClassic(target.window, *target.log, *target.chat);

	QCOMPARE(target.window.dockWidgetArea(target.log), Qt::LeftDockWidgetArea);
	QCOMPARE(target.window.dockWidgetArea(target.chat), Qt::LeftDockWidgetArea);
}

/// A window that is not headed for maximized or full screen gets its state
/// applied right away, so there is nothing to flush there either.
void TestMainWindowLayout::unmaximizedWindowNeedsNoFlush() {
	Window target;
	target.window.resize(735, 435);
	QVERIFY(target.window.restoreState(maximizedState(), STATE_VERSION));

	QVERIFY(!Mumble::WindowLayout::restoreMayBePending(target.window));

	Mumble::WindowLayout::applyClassic(target.window, *target.log, *target.chat);

	QCOMPARE(target.window.dockWidgetArea(target.log), Qt::LeftDockWidgetArea);
	QCOMPARE(target.window.dockWidgetArea(target.chat), Qt::LeftDockWidgetArea);
}

QTEST_MAIN(TestMainWindowLayout)
#include "TestMainWindowLayout.moc"
