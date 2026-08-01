// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "TouchBar_macx.h"

#include "MainWindow.h"
#include "Settings.h"
#include "Global.h"

#include <QtGui/QWindow>

#import <AppKit/AppKit.h>

static NSString *const MUTouchBarItemMute       = @"info.mumble.touchbar.mute";
static NSString *const MUTouchBarItemDeafen     = @"info.mumble.touchbar.deafen";
static NSString *const MUTouchBarItemNoise      = @"info.mumble.touchbar.noise";
static NSString *const MUTouchBarItemEcho       = @"info.mumble.touchbar.echo";
static NSString *const MUTouchBarItemCycleInput = @"info.mumble.touchbar.cycleinput";
static NSString *const MUTouchBarItemReconnect  = @"info.mumble.touchbar.reconnect";

@interface MUTouchBarController : NSObject <NSTouchBarDelegate> {
	MainWindow *m_mainWindow;
}
@property (retain) NSButton *muteButton;
@property (retain) NSButton *deafenButton;
@property (retain) NSButton *noiseButton;
@property (retain) NSButton *echoButton;

- (instancetype)initWithMainWindow:(MainWindow *)mainWindow;
- (NSTouchBar *)makeBar;
- (void)refresh;
@end

// An SF Symbol button, falling back to a plain title on symbol lookup failure.
static NSButton *makeButton(id target, SEL action, NSString *symbol, NSString *fallbackTitle) {
	NSImage *image = nil;
	if (@available(macOS 11.0, *)) {
		image = [NSImage imageWithSystemSymbolName:symbol accessibilityDescription:fallbackTitle];
	}
	if (image) {
		return [NSButton buttonWithImage:image target:target action:action];
	}
	return [NSButton buttonWithTitle:fallbackTitle target:target action:action];
}

@implementation MUTouchBarController

- (instancetype)initWithMainWindow:(MainWindow *)mainWindow {
	if ((self = [super init])) {
		m_mainWindow = mainWindow;
	}
	return self;
}

- (NSTouchBar *)makeBar {
	NSTouchBar *bar            = [[[NSTouchBar alloc] init] autorelease];
	bar.delegate               = self;
	bar.defaultItemIdentifiers = @[
		MUTouchBarItemMute, MUTouchBarItemDeafen, MUTouchBarItemNoise, MUTouchBarItemEcho, MUTouchBarItemCycleInput,
		MUTouchBarItemReconnect
	];
	return bar;
}

- (NSTouchBarItem *)touchBar:(NSTouchBar *)touchBar makeItemForIdentifier:(NSTouchBarItemIdentifier)identifier {
	Q_UNUSED(touchBar);

	NSButton *button = nil;
	if ([identifier isEqualToString:MUTouchBarItemMute]) {
		self.muteButton = makeButton(self, @selector(toggleMute), @"mic.slash.fill", @"Mute");
		button          = self.muteButton;
	} else if ([identifier isEqualToString:MUTouchBarItemDeafen]) {
		self.deafenButton = makeButton(self, @selector(toggleDeafen), @"speaker.slash.fill", @"Deafen");
		button            = self.deafenButton;
	} else if ([identifier isEqualToString:MUTouchBarItemNoise]) {
		self.noiseButton = makeButton(self, @selector(toggleNoise), @"waveform.badge.minus", @"Noise");
		button           = self.noiseButton;
	} else if ([identifier isEqualToString:MUTouchBarItemEcho]) {
		self.echoButton = makeButton(self, @selector(toggleEcho), @"dot.radiowaves.left.and.right", @"Echo");
		button          = self.echoButton;
	} else if ([identifier isEqualToString:MUTouchBarItemCycleInput]) {
		button = makeButton(self, @selector(cycleInput), @"arrow.triangle.2.circlepath", @"Input");
	} else if ([identifier isEqualToString:MUTouchBarItemReconnect]) {
		button = makeButton(self, @selector(reconnect), @"arrow.clockwise", @"Reconnect");
	}

	if (!button) {
		return nil;
	}

	NSCustomTouchBarItem *item = [[[NSCustomTouchBarItem alloc] initWithIdentifier:identifier] autorelease];
	item.view                  = button;

	[self refresh];

	return item;
}

- (void)refresh {
	const Settings &s = Global::get().s;

	self.muteButton.bezelColor   = s.bMute ? [NSColor systemRedColor] : nil;
	self.deafenButton.bezelColor = s.bDeaf ? [NSColor systemRedColor] : nil;
	self.noiseButton.bezelColor =
		(s.noiseCancelMode != Settings::NoiseCancelOff) ? [NSColor systemBlueColor] : nil;
	self.echoButton.bezelColor = (s.echoOption != EchoCancelOptionID::DISABLED) ? [NSColor systemBlueColor] : nil;
}

- (void)toggleMute {
	if (m_mainWindow) {
		m_mainWindow->setAudioMute(!Global::get().s.bMute);
	}
}

- (void)toggleDeafen {
	if (m_mainWindow) {
		m_mainWindow->setAudioDeaf(!Global::get().s.bDeaf);
	}
}

- (void)toggleNoise {
	if (m_mainWindow) {
		m_mainWindow->toggleNoiseCancel();
	}
}

- (void)toggleEcho {
	if (m_mainWindow) {
		m_mainWindow->toggleEchoCancel();
	}
}

- (void)cycleInput {
	if (m_mainWindow) {
		m_mainWindow->cycleInputDevice();
	}
}

- (void)reconnect {
	if (m_mainWindow) {
		m_mainWindow->reconnectToServer();
	}
}

@end

static MUTouchBarController *touchBarController = nil;

void MUInstallTouchBar(MainWindow *mainWindow) {
	if (!mainWindow) {
		return;
	}

	@try {
		QWindow *window = mainWindow->windowHandle();
		if (!window) {
			return;
		}

		NSView *view = reinterpret_cast< NSView * >(window->winId());
		if (!view) {
			return;
		}

		NSWindow *nsWindow = [view window];
		if (!nsWindow || nsWindow.touchBar) {
			return;
		}

		if (!touchBarController) {
			touchBarController = [[MUTouchBarController alloc] initWithMainWindow:mainWindow];
		}

		nsWindow.touchBar = [touchBarController makeBar];
	} @catch (NSException *exception) {
		qWarning("TouchBar: installation failed: %s", [[exception reason] UTF8String]);
	}
}

void MUUpdateTouchBar() {
	if (!touchBarController) {
		return;
	}

	@try {
		[touchBarController refresh];
	} @catch (NSException *exception) {
		qWarning("TouchBar: refresh failed: %s", [[exception reason] UTF8String]);
	}
}
