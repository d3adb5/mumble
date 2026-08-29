// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_WIDGETS_TRAYICON_H_
#define MUMBLE_MUMBLE_WIDGETS_TRAYICON_H_

#include <functional>

#include <QAction>
#include <QTimer>
#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QPair>
#include <QtCore/QVariant>
#include <QtWidgets/QMenu>
#include <QtWidgets/QSystemTrayIcon>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QWidgetAction>

#include "TrayMenuModel.h"

class TrayIcon : public QSystemTrayIcon {
	Q_OBJECT

public:
	TrayIcon();

public slots:
	void on_hideAction_triggered();
	void on_showAction_triggered();
	void on_toggleShowHide();

	void on_icon_update();
	void on_tray_unhighlight();

private:
	enum class BlinkState {
		RegularIcon,
		BlinkIcon,
	};

	/// The menu showing one channel of the tray's channel tree, along with what it
	/// currently lists - so that a refresh can tell a changed state from a changed
	/// channel and only rebuild the menu for the latter.
	struct ChannelMenu {
		QMenu *menu = nullptr;
		/// The users the menu lists, in the order they are shown
		QList< Mumble::TrayMenu::UserEntry > users;
		/// The entries showing those users, in the same order
		QList< QAction * > userActions;
		/// The channels the menu lists, in the order they are shown
		QList< unsigned int > subChannels;
		/// False until the menu has been filled for the first time
		bool filled = false;
	};

	std::reference_wrapper< QIcon > m_statusIcon;
	BlinkState m_blinkState   = BlinkState::RegularIcon;
	bool m_blinkingIcon       = false;
	QMenu *m_contextMenu      = nullptr;
	QMenu *m_channelMenu      = nullptr;
	QMenu *m_transmitModeMenu = nullptr;
	QMenu *m_noiseCancelMenu  = nullptr;
	QMenu *m_outputDeviceMenu = nullptr;
	QAction *m_showAction     = nullptr;
	QAction *m_hideAction     = nullptr;
	QAction *m_recordAction   = nullptr;
	/// Row of icon-only buttons for the toggles that would otherwise take up a menu
	/// entry each, laid out like the main window's toolbar
	QWidgetAction *m_controlsAction = nullptr;
	QToolButton *m_recordButton     = nullptr;
	QTimer *m_highlightTimer        = nullptr;
	/// Keeps the channel tree up to date for as long as the context menu is open
	QTimer *m_channelViewTimer = nullptr;
	/// The menus of the channel tree, by the ID of the channel they show
	QHash< unsigned int, ChannelMenu > m_channelMenus;
#ifdef USE_DBUS
	/// ID of the last notification posted via org.freedesktop.Notifications, so that
	/// a new notification replaces the previous one instead of stacking up
	quint32 m_lastNotificationId = 0;
#endif

	void updateContextMenu();

	/// Adds the entry showing or hiding the main window.
	/// @returns Whether anything was added
	bool addWindowVisibilitySection();
	/// Adds the view of the channel the local user is in.
	/// @returns Whether anything was added
	bool addChannelSection();
	/// Adds the submenus mirroring the main window's audio dropdowns.
	/// @returns Whether anything was added
	bool addAudioDeviceSection();
	/// Adds the row of toggles and the entries that go with them.
	/// @returns Whether anything was added
	bool addControlsSection();

	/// Fills a submenu with one checkable entry per choice, checking the one that
	/// is currently configured and invoking the callback with the picked value.
	void populateChoiceMenu(QMenu *menu, const QList< QPair< QString, QVariant > > &choices, const QVariant &current,
							std::function< void(const QVariant &) > onPicked);

	/// Brings the channel tree in line with the server's, dropping the menus of
	/// channels that are gone.
	void updateChannelMenu();
	/// @returns The menu showing the given channel, creating it if there is none yet
	QMenu *channelMenu(unsigned int channelId);
	/// Fills the menu of the given channel with its users and subchannels. Entries are
	/// only recreated when the channel's contents changed, so that talking states can
	/// be followed while the menu stays open; a rebuild that is not allowed right now
	/// is skipped rather than done anyway.
	void fillChannelMenu(unsigned int channelId, bool allowRebuild = true);
	/// Refreshes the channel menus that are on screen.
	void refreshChannelMenus();
	/// Forgets every channel menu, which is what leaving a server calls for.
	void clearChannelMenus();

	/// Points the main window's context menus at the given user, channel or listener,
	/// the way selecting them in its user list does.
	void selectUser(unsigned int session);
	void selectChannel(unsigned int channelId);
	void selectListener(unsigned int session, unsigned int channelId);

	/// Labels the recording entry after what triggering it would do and disables it
	/// while recording is not possible.
	void updateRecordAction();

	/// Shows a pop-up notification, preferring the freedesktop.org notification
	/// service over the Qt tray icon balloon
	void showNotification(const QString &title, const QString &body, QSystemTrayIcon::MessageIcon icon);

private slots:
	void on_contextMenu_aboutToShow();
	void on_contextMenu_aboutToHide();
	void on_icon_clicked(QSystemTrayIcon::ActivationReason reason);
	void on_windowMinimized();
	void on_timer_triggered();
};

#endif // MUMBLE_MUMBLE_WIDGETS_TRAYICON_H_
