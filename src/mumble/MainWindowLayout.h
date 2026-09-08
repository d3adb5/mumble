// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_MAINWINDOWLAYOUT_H_
#define MUMBLE_MUMBLE_MAINWINDOWLAYOUT_H_

#include <QtCore/Qt>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QMainWindow>

// Arranging the main window's log and chat docks for the configured window
// layout. Kept out of MainWindow so the ordering these functions rely on (see
// flushRestoredDockState) can be exercised on a plain QMainWindow by a test.
namespace Mumble {
namespace WindowLayout {

	/// Whether Qt might still be sitting on a deferred restore of the dock layout.
	///
	/// QMainWindow::restoreState() cannot lay the docks out while the window is
	/// still hidden and is about to be shown maximized or full screen: the state
	/// was saved for the maximized size and does not fit the size the window
	/// currently has. Qt then keeps a copy of the restored layout around and
	/// re-applies it on every relayout until it fits, or until it gives up 150 ms
	/// after the last resize (QMainWindowLayout::applyRestoredState()).
	///
	/// These are the conditions under which Qt makes that copy in the first place,
	/// so a window failing them has nothing pending. Mumble only ever rearranges
	/// its docks from its constructor, which is the hidden case, and from menu
	/// entries the user picks once the window is up, which is long past the point
	/// where Qt has stopped holding on to anything.
	inline bool restoreMayBePending(const QMainWindow &window) {
		const Qt::WindowStates states = window.windowState();

		return !window.isVisible() && (states.testFlag(Qt::WindowMaximized) || states.testFlag(Qt::WindowFullScreen));
	}

	/// Forces out a deferred restore before the caller starts moving docks around.
	///
	/// The copy Qt holds on to shares the live layout's QLayoutItem pointers, but
	/// removeDockWidget() deletes those items without telling it: it only scrubs
	/// them from the layout state saved while a dock is being dragged. Re-applying
	/// the copy afterwards puts freed pointers back into the live layout, and the
	/// next traversal walks them - splitDockWidget() a few lines down, or, for a
	/// layout that does not split, whichever resize applies the copy later.
	///
	/// splitDockWidget() is the only public entry point that applies and thereby
	/// discards the copy, so we trigger it here, while the copy still agrees with
	/// the live layout and nothing can dangle. The split is either repeated
	/// (classic, stacked) or undone (hybrid) by the caller right after, so this
	/// stays a no-op as far as the resulting layout is concerned, and it costs
	/// nothing at all in the common case of a window that is already visible.
	///
	/// Once Qt discards the copy in removeDockWidget() as well, this can go.
	inline void flushRestoredDockState(QMainWindow &window, QDockWidget &first, QDockWidget &second) {
		if (!restoreMayBePending(window)) {
			return;
		}

		window.splitDockWidget(&first, &second, Qt::Vertical);
	}

	/// Log on the left, chat below it.
	inline void applyClassic(QMainWindow &window, QDockWidget &log, QDockWidget &chat) {
		flushRestoredDockState(window, log, chat);

		window.removeDockWidget(&log);
		window.addDockWidget(Qt::LeftDockWidgetArea, &log);
		log.show();
		window.splitDockWidget(&log, &chat, Qt::Vertical);
		chat.show();
	}

	/// Log at the bottom, chat below it.
	inline void applyStacked(QMainWindow &window, QDockWidget &log, QDockWidget &chat) {
		flushRestoredDockState(window, log, chat);

		window.removeDockWidget(&log);
		window.addDockWidget(Qt::BottomDockWidgetArea, &log);
		log.show();
		window.splitDockWidget(&log, &chat, Qt::Vertical);
		chat.show();
	}

	/// Log on the left, chat spanning the bottom.
	inline void applyHybrid(QMainWindow &window, QDockWidget &log, QDockWidget &chat) {
		flushRestoredDockState(window, log, chat);

		window.removeDockWidget(&log);
		window.removeDockWidget(&chat);
		window.addDockWidget(Qt::LeftDockWidgetArea, &log);
		log.show();
		window.addDockWidget(Qt::BottomDockWidgetArea, &chat);
		chat.show();
	}

} // namespace WindowLayout
} // namespace Mumble

#endif
