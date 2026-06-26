// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "X11WindowState.h"

#include <QtGlobal>

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)

#	include "EnvUtils.h"

#	include <X11/Xatom.h>
#	include <X11/Xlib.h>

namespace {
// Read the first value of a 32 bit CARDINAL property. Returns whether it could
// be read. Format-32 properties are handed back by Xlib as an array of C long.
bool readCardinal(Display *display, Window window, Atom property, unsigned long &value) {
	Atom actualType          = None;
	int actualFormat         = 0;
	unsigned long nItems     = 0;
	unsigned long bytesAfter = 0;
	unsigned char *data      = nullptr;

	if (XGetWindowProperty(display, window, property, 0, 1, False, XA_CARDINAL, &actualType, &actualFormat, &nItems,
						   &bytesAfter, &data)
		!= Success) {
		return false;
	}

	bool ok = false;
	if (data) {
		if (actualType == XA_CARDINAL && actualFormat == 32 && nItems >= 1) {
			value = *reinterpret_cast< unsigned long * >(data);
			ok    = true;
		}
		XFree(data);
	}
	return ok;
}
} // namespace

namespace Mumble {
namespace X11WindowState {

bool windowIsOnOtherDesktop(unsigned long window) {
	if (EnvUtils::waylandIsUsed()) {
		return false;
	}

	// A single private connection, reused for these infrequent (minimize-time)
	// queries so we do not reconnect to the X server on every workspace switch.
	static Display *display = XOpenDisplay(nullptr);
	if (!display) {
		return false;
	}

	const Atom netWmDesktop      = XInternAtom(display, "_NET_WM_DESKTOP", True);
	const Atom netCurrentDesktop = XInternAtom(display, "_NET_CURRENT_DESKTOP", True);
	if (netWmDesktop == None || netCurrentDesktop == None) {
		return false;
	}

	unsigned long windowDesktop  = 0;
	unsigned long currentDesktop = 0;
	if (!readCardinal(display, static_cast< Window >(window), netWmDesktop, windowDesktop)
		|| !readCardinal(display, DefaultRootWindow(display), netCurrentDesktop, currentDesktop)) {
		return false;
	}

	// 0xFFFFFFFF marks a window shown on all desktops (sticky); it is never
	// "elsewhere".
	const unsigned long ALL_DESKTOPS = 0xFFFFFFFFUL;
	return (windowDesktop != ALL_DESKTOPS) && (windowDesktop != currentDesktop);
}

} // namespace X11WindowState
} // namespace Mumble

#else

namespace Mumble {
namespace X11WindowState {

bool windowIsOnOtherDesktop(unsigned long) {
	return false;
}

} // namespace X11WindowState
} // namespace Mumble

#endif
