// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_WIDGETS_TRAYICON_H_
#define MUMBLE_MUMBLE_WIDGETS_TRAYICON_H_

#include <functional>

#include <QAction>
#include <QTimer>
#include <QtCore/QList>
#include <QtCore/QPair>
#include <QtCore/QVariant>
#include <QtWidgets/QMenu>
#include <QtWidgets/QSystemTrayIcon>

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
	QTimer *m_highlightTimer  = nullptr;
	/// Keeps the channel view up to date for as long as the context menu is open
	QTimer *m_channelViewTimer = nullptr;
	/// The users the channel view currently lists, in the order they are shown
	QList< Mumble::TrayMenu::UserEntry > m_channelEntries;
#ifdef USE_DBUS
	/// ID of the last notification posted via org.freedesktop.Notifications, so that
	/// a new notification replaces the previous one instead of stacking up
	quint32 m_lastNotificationId = 0;
#endif

	void updateContextMenu();

	/// Fills a submenu with one checkable entry per choice, checking the one that
	/// is currently configured and invoking the callback with the picked value.
	void populateChoiceMenu(QMenu *menu, const QList< QPair< QString, QVariant > > &choices, const QVariant &current,
							std::function< void(const QVariant &) > onPicked);

	/// Brings the view of the local user's channel in line with the current state of
	/// that channel. Entries are only recreated when the users themselves changed, so
	/// that talking states can be followed while the menu stays open.
	void updateChannelMenu();

	/// Labels the recording entry after what triggering it would do and disables it
	/// while recording is not possible.
	void updateRecordAction();

	/// Shows a pop-up notification, preferring the freedesktop.org notification
	/// service over the Qt tray icon balloon
	void showNotification(const QString &title, const QString &body, QSystemTrayIcon::MessageIcon icon);

private slots:
	void on_contextMenu_aboutToHide();
	void on_icon_clicked(QSystemTrayIcon::ActivationReason reason);
	void on_windowMinimized();
	void on_timer_triggered();
};

#endif // MUMBLE_MUMBLE_WIDGETS_TRAYICON_H_
