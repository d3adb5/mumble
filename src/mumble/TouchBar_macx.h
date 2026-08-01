// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_TOUCHBAR_MACX_H_
#define MUMBLE_MUMBLE_TOUCHBAR_MACX_H_

class MainWindow;

/// Installs the Touch Bar on the main window. A no-op when the window has no
/// native handle yet or a bar is already installed; any failure is swallowed
/// so it can never take the client down. Call again after the window is shown.
void MUInstallTouchBar(MainWindow *mainWindow);

/// Refreshes the toggle states (mute/deafen/suppression) shown on the Touch Bar.
void MUUpdateTouchBar();

#endif
