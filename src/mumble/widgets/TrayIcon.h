// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_WIDGETS_TRAYICON_H_
#define MUMBLE_MUMBLE_WIDGETS_TRAYICON_H_

#include <functional>

#include <QAction>
#include <QTimer>
#include <QtWidgets/QMenu>
#include <QtWidgets/QSystemTrayIcon>

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
	BlinkState m_blinkState  = BlinkState::RegularIcon;
	bool m_blinkingIcon      = false;
	QMenu *m_contextMenu     = nullptr;
	QAction *m_showAction    = nullptr;
	QAction *m_hideAction    = nullptr;
	QTimer *m_highlightTimer = nullptr;
#ifdef USE_DBUS
	/// ID of the last notification posted via org.freedesktop.Notifications, so a
	/// new notification replaces the previous one instead of stacking up.
	quint32 m_lastNotificationId = 0;
#endif

	void updateContextMenu();

	/// Shows a pop-up notification for the given message. On systems exposing the
	/// freedesktop.org notification service it is delivered through the user's
	/// notification daemon; otherwise it falls back to the Qt tray icon balloon.
	void showNotification(const QString &title, const QString &body, QSystemTrayIcon::MessageIcon icon);

private slots:
	void on_icon_clicked(QSystemTrayIcon::ActivationReason reason);
	void on_windowMinimized();
	void on_timer_triggered();
};

#endif // MUMBLE_MUMBLE_WIDGETS_TRAYICON_H_
