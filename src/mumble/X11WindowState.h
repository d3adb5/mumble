// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_X11WINDOWSTATE_H_
#define MUMBLE_MUMBLE_X11WINDOWSTATE_H_

namespace Mumble {
namespace X11WindowState {

	/// Whether the given top-level window currently sits on a different EWMH
	/// virtual desktop / workspace than the active one.
	///
	/// Tiling, EWMH-aware window managers (e.g. XMonad) unmap and mark a window
	/// that merely lives on a non-visible workspace with _NET_WM_STATE_HIDDEN,
	/// which Qt in turn reports as the window being "minimized". This lets the
	/// tray code tell such a pseudo-minimize (a workspace switch) apart from a
	/// genuine, user-initiated one so it does not wrongly hide Mumble to tray.
	///
	/// Always returns false off X11 (Windows, macOS, Wayland) and whenever the
	/// relevant EWMH properties are unavailable, leaving behaviour unchanged.
	///
	/// \param window Native window id (an X11 Window) of the top-level window.
	bool windowIsOnOtherDesktop(unsigned long window);

} // namespace X11WindowState
} // namespace Mumble

#endif // MUMBLE_MUMBLE_X11WINDOWSTATE_H_
