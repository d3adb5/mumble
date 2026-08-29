// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "TrayIcon.h"

#include "Channel.h"
#include "ChannelListenerManager.h"
#include "ClientUser.h"
#include "Log.h"
#include "MainWindow.h"
#include "TrayMenuModel.h"
#include "UserModel.h"
#include "X11WindowState.h"
#include "Global.h"

#include <QApplication>
#include <QtGui/QFont>
#include <QtWidgets/QHBoxLayout>

#ifdef USE_DBUS
#	include <QtDBus/QDBusInterface>
#	include <QtDBus/QDBusMessage>
#endif

namespace {

/// Interval at which the channel view follows the state of the channel while the
/// context menu is open. Matches the one the recorder dialog polls with.
constexpr int channelViewUpdateInterval = 200;

/// Margin left and right of the row of toggle buttons, roughly lining its icons up
/// with the ones of the entries above and below it.
constexpr int controlsMargin = 4;

Mumble::TrayMenu::TalkState talkStateOf(Settings::TalkState state) {
	switch (state) {
		case Settings::Talking:
			return Mumble::TrayMenu::TalkState::Talking;
		case Settings::MutedTalking:
			return Mumble::TrayMenu::TalkState::MutedTalking;
		case Settings::Whispering:
			return Mumble::TrayMenu::TalkState::Whispering;
		case Settings::Shouting:
			return Mumble::TrayMenu::TalkState::Shouting;
		case Settings::Passive:
			break;
	}

	return Mumble::TrayMenu::TalkState::Passive;
}

/// The symbols of the user list, which the channel view borrows so that a user
/// looks the same in the tray as they do in the main window.
const QIcon &iconOf(Mumble::TrayMenu::UserIcon icon) {
	static const QIcon listener(QLatin1String("skin:ear.svg"));
	static const QIcon deafenedSelf(QLatin1String("skin:deafened_self.svg"));
	static const QIcon deafenedServer(QLatin1String("skin:deafened_server.svg"));
	static const QIcon mutedSelf(QLatin1String("skin:muted_self.svg"));
	static const QIcon mutedServer(QLatin1String("skin:muted_server.svg"));
	static const QIcon mutedSuppressed(QLatin1String("skin:muted_suppressed.svg"));
	static const QIcon mutedLocal(QLatin1String("skin:muted_local.svg"));
	static const QIcon talkingOn(QLatin1String("skin:talking_on.svg"));
	static const QIcon talkingMuted(QLatin1String("skin:talking_muted.svg"));
	static const QIcon talkingWhisper(QLatin1String("skin:talking_whisper.svg"));
	static const QIcon talkingShout(QLatin1String("skin:talking_alt.svg"));
	static const QIcon talkingOff(QLatin1String("skin:talking_off.svg"));

	switch (icon) {
		case Mumble::TrayMenu::UserIcon::Listener:
			return listener;
		case Mumble::TrayMenu::UserIcon::DeafenedSelf:
			return deafenedSelf;
		case Mumble::TrayMenu::UserIcon::DeafenedServer:
			return deafenedServer;
		case Mumble::TrayMenu::UserIcon::MutedSelf:
			return mutedSelf;
		case Mumble::TrayMenu::UserIcon::MutedServer:
			return mutedServer;
		case Mumble::TrayMenu::UserIcon::MutedSuppressed:
			return mutedSuppressed;
		case Mumble::TrayMenu::UserIcon::MutedLocal:
			return mutedLocal;
		case Mumble::TrayMenu::UserIcon::TalkingOn:
			return talkingOn;
		case Mumble::TrayMenu::UserIcon::TalkingSilent:
			// Configurable, so it has to be taken from the user list itself
			return Global::get().mw->pmModel->talkingSilentIcon();
		case Mumble::TrayMenu::UserIcon::TalkingMuted:
			return talkingMuted;
		case Mumble::TrayMenu::UserIcon::TalkingWhisper:
			return talkingWhisper;
		case Mumble::TrayMenu::UserIcon::TalkingShout:
			return talkingShout;
		case Mumble::TrayMenu::UserIcon::TalkingOff:
			break;
	}

	return talkingOff;
}

Mumble::TrayMenu::UserEntry entryFor(const ClientUser &user, bool isListener, const Channel &channel) {
	Mumble::TrayMenu::UserEntry entry;

	entry.name    = user.qsName;
	entry.label   = Mumble::TrayMenu::escapeMenuText(UserModel::createDisplayString(user, isListener, &channel));
	entry.session = user.uiSession;
	entry.self    = user.uiSession == Global::get().uiSession;

	entry.state.listener       = isListener;
	entry.state.selfDeafened   = user.bSelfDeaf;
	entry.state.serverDeafened = user.bDeaf;
	entry.state.selfMuted      = user.bSelfMute;
	entry.state.serverMuted    = user.bMute;
	entry.state.suppressed     = user.bSuppress;
	entry.state.localMuted     = user.bLocalMute;
	entry.state.talkState      = talkStateOf(user.tsState);
	entry.state.audible        = user.isAudible();

	return entry;
}

/// Whether both lists show the same users in the same order, in which case the
/// existing menu entries can be updated instead of being recreated.
bool sameUsers(const QList< Mumble::TrayMenu::UserEntry > &first, const QList< Mumble::TrayMenu::UserEntry > &second) {
	if (first.size() != second.size()) {
		return false;
	}

	for (int i = 0; i < first.size(); ++i) {
		if (first.at(i).session != second.at(i).session || first.at(i).state.listener != second.at(i).state.listener) {
			return false;
		}
	}

	return true;
}

} // namespace

TrayIcon::TrayIcon() : QSystemTrayIcon(Global::get().mw), m_statusIcon(Global::get().mw->qiIcon) {
	setIcon(m_statusIcon);

	setToolTip("Mumble");

	assert(Global::get().mw);
	assert(Global::get().l);

	QObject::connect(Global::get().mw, &MainWindow::talkingStatusChanged, this, &TrayIcon::on_icon_update);
	QObject::connect(Global::get().mw, &MainWindow::disconnectedFromServer, this, &TrayIcon::on_icon_update);
	QObject::connect(Global::get().mw, &MainWindow::windowMinimized, this, &TrayIcon::on_windowMinimized);
	QObject::connect(Global::get().mw, &MainWindow::windowVisibilityToggled, this, &TrayIcon::on_toggleShowHide);
	QObject::connect(Global::get().l, &Log::notificationSpawned, this,
					 [this](QString title, QString body, QSystemTrayIcon::MessageIcon icon) {
						 showNotification(title, body, icon);
					 });

	QObject::connect(Global::get().l, &Log::highlightSpawned, this, &TrayIcon::on_timer_triggered);
	QObject::connect(Global::get().mw, &MainWindow::windowActivated, this, &TrayIcon::on_tray_unhighlight);

	m_highlightTimer = new QTimer(this);
	m_highlightTimer->setSingleShot(true);
	QObject::connect(m_highlightTimer, &QTimer::timeout, this, &TrayIcon::on_timer_triggered);

	QObject::connect(this, &QSystemTrayIcon::activated, this, &TrayIcon::on_icon_clicked);

	// messageClicked is buggy in Qt on some platforms and we can not do anything about this (QTBUG-87329)
	QObject::connect(this, &QSystemTrayIcon::messageClicked, this, &TrayIcon::on_showAction_triggered);

	m_showAction = new QAction(tr("Show"), Global::get().mw);
	QObject::connect(m_showAction, &QAction::triggered, this, &TrayIcon::on_showAction_triggered);

	m_hideAction = new QAction(tr("Hide"), Global::get().mw);
	QObject::connect(m_hideAction, &QAction::triggered, this, &TrayIcon::on_hideAction_triggered);

	m_recordAction =
		new QAction(QIcon(QLatin1String("skin:actions/media-record.svg")), tr("Start Recording"), Global::get().mw);
	m_recordAction->setCheckable(true);
	QObject::connect(m_recordAction, &QAction::triggered, this, [this]() {
		Global::get().mw->toggleRecording();
		// Starting can fail (and stopping takes until the last samples are written),
		// so the button follows what actually happened rather than the click
		updateRecordAction();
	});
	// A recording can also end on its own, which the entry has to reflect even when
	// the menu happens to be open at that moment
	QObject::connect(Global::get().mw, &MainWindow::recordingStateChanged, this, &TrayIcon::updateRecordAction);

	// The toggles share a single row of icon-only buttons: spelled out they would take
	// up a third of the menu's height for what the toolbar fits into a few pixels.
	QWidget *controls        = new QWidget();
	QHBoxLayout *controlsBox = new QHBoxLayout(controls);
	controlsBox->setContentsMargins(controlsMargin, 0, controlsMargin, 0);
	controlsBox->setSpacing(0);

	const auto addControlButton = [controls, controlsBox](QAction *action) {
		QToolButton *button = new QToolButton(controls);
		// Taking the action over gives the button its icon, its tool tip and - for the
		// checkable ones - the icon of whichever state it is in
		button->setDefaultAction(action);
		button->setAutoRaise(true);
		button->setToolButtonStyle(Qt::ToolButtonIconOnly);

		controlsBox->addWidget(button);

		return button;
	};

	addControlButton(Global::get().mw->qaAudioMute);
	addControlButton(Global::get().mw->qaAudioDeaf);
	m_recordButton = addControlButton(m_recordAction);

	controlsBox->addStretch(1);

	m_controlsAction = new QWidgetAction(this);
	m_controlsAction->setDefaultWidget(controls);

	QObject::connect(Global::get().mw->qaTalkingUIToggle, &QAction::triggered, this, &TrayIcon::updateContextMenu);

	// A view of the channel the local user is in, which is kept up to date for as
	// long as the context menu is open.
	m_channelMenu = new QMenu(Global::get().mw);
	m_channelMenu->setIcon(QIcon(QLatin1String("skin:channel_active.svg")));

	m_channelViewTimer = new QTimer(this);
	m_channelViewTimer->setInterval(channelViewUpdateInterval);
	QObject::connect(m_channelViewTimer, &QTimer::timeout, this, &TrayIcon::updateChannelMenu);

	// Submenus mirroring the main window's toolbar dropdowns. They are filled in
	// whenever the context menu is about to be shown, as their entries depend on
	// what the audio backend currently offers.
	m_transmitModeMenu = new QMenu(tr("Transmit Mode"), Global::get().mw);
	m_noiseCancelMenu  = new QMenu(tr("Noise Suppression"), Global::get().mw);
	m_outputDeviceMenu = new QMenu(tr("Output Device"), Global::get().mw);

	m_contextMenu = new QMenu(Global::get().mw);
	QObject::connect(m_contextMenu, &QMenu::aboutToShow, this, &TrayIcon::on_contextMenu_aboutToShow);
	QObject::connect(m_contextMenu, &QMenu::aboutToHide, this, &TrayIcon::on_contextMenu_aboutToHide);

	// Some window managers hate it when a tray icon sets an empty context menu...
	updateContextMenu();

	setContextMenu(m_contextMenu);

	show();
}

void TrayIcon::on_icon_update() {
	std::reference_wrapper< QIcon > newIcon = Global::get().mw->qiIcon;

	const ClientUser *p = ClientUser::get(Global::get().uiSession);

	if (Global::get().s.bDeaf) {
		newIcon = Global::get().mw->qiIconDeafSelf;
	} else if (p && p->bDeaf) {
		newIcon = Global::get().mw->qiIconDeafServer;
	} else if (Global::get().s.bMute) {
		newIcon = Global::get().mw->qiIconMuteSelf;
	} else if (p && p->bMute) {
		newIcon = Global::get().mw->qiIconMuteServer;
	} else if (p && p->bSuppress) {
		newIcon = Global::get().mw->qiIconMuteSuppressed;
	} else if (Global::get().s.bStateInTray && Global::get().bPushToMute) {
		newIcon = Global::get().mw->qiIconMutePushToMute;
	} else if (p && Global::get().s.bStateInTray) {
		switch (p->tsState) {
			case Settings::Talking:
			case Settings::MutedTalking:
				// Match the user list: hint when transmitting nothing but silence.
				if (p->isAudible()) {
					newIcon = Global::get().mw->qiTalkingOn;
				} else {
					newIcon = Global::get().mw->pmModel->talkingSilentIcon();
				}
				break;
			case Settings::Whispering:
				newIcon = Global::get().mw->qiTalkingWhisper;
				break;
			case Settings::Shouting:
				newIcon = Global::get().mw->qiTalkingShout;
				break;
			case Settings::Passive:
				newIcon = Global::get().mw->qiTalkingOff;
				break;
		}
	}

	if (&newIcon.get() != &m_statusIcon.get()) {
		m_statusIcon = newIcon;
		setIcon(m_statusIcon);
	}
}

void TrayIcon::on_icon_clicked(QSystemTrayIcon::ActivationReason reason) {
	switch (reason) {
		case QSystemTrayIcon::Trigger:
#ifndef Q_OS_MAC
			// macOS is special as it both shows the context menu AND triggers the action.
			// We only want at most one of those and since we can not prevent showing
			// the menu, we skip the action.
			on_toggleShowHide();
#endif
			break;
		case QSystemTrayIcon::Unknown:
		case QSystemTrayIcon::Context:
		case QSystemTrayIcon::DoubleClick:
		case QSystemTrayIcon::MiddleClick:
			break;
	}
}

void TrayIcon::updateContextMenu() {
	m_contextMenu->clear();

	// Every section that added something is closed off with a separator, so that no
	// section has to know whether the ones around it are shown at all
	const auto closeSection = [this](bool added) {
		if (added) {
			m_contextMenu->addSeparator();
		}
	};

	closeSection(addWindowVisibilitySection());
	closeSection(addChannelSection());
	closeSection(addAudioDeviceSection());
	closeSection(addControlsSection());

	m_contextMenu->addAction(Global::get().mw->qaQuit);
}

bool TrayIcon::addWindowVisibilitySection() {
	if (Global::get().mw->isVisible() && !Global::get().mw->isMinimized()) {
		m_hideAction->setEnabled(QSystemTrayIcon::isSystemTrayAvailable());
		m_contextMenu->addAction(m_hideAction);
	} else {
		m_contextMenu->addAction(m_showAction);
	}

	return true;
}

bool TrayIcon::addChannelSection() {
	if (!Global::get().s.bTrayShowChannel) {
		return false;
	}

	updateChannelMenu();
	m_contextMenu->addMenu(m_channelMenu);

	return true;
}

bool TrayIcon::addAudioDeviceSection() {
	const Settings &settings = Global::get().s;

	if (settings.bTrayShowTransmitMode) {
		populateChoiceMenu(m_transmitModeMenu, Global::get().mw->transmitModeChoices(),
						   static_cast< int >(settings.atTransmit), [](const QVariant &value) {
							   Global::get().mw->setTransmissionMode(
								   static_cast< Settings::AudioTransmit >(value.toInt()));
						   });
		m_contextMenu->addMenu(m_transmitModeMenu);
	}

	if (settings.bTrayShowNoiseCancel) {
		populateChoiceMenu(m_noiseCancelMenu, Global::get().mw->noiseCancelChoices(),
						   static_cast< int >(settings.noiseCancelMode), [](const QVariant &value) {
							   Global::get().mw->setNoiseCancel(static_cast< Settings::NoiseCancel >(value.toInt()));
						   });
		m_contextMenu->addMenu(m_noiseCancelMenu);
	}

	if (settings.bTrayShowOutputDevice) {
		populateChoiceMenu(m_outputDeviceMenu, Global::get().mw->outputDeviceChoices(),
						   Global::get().mw->currentOutputDevice(),
						   [](const QVariant &value) { Global::get().mw->setOutputDevice(value); });
		m_contextMenu->addMenu(m_outputDeviceMenu);
	}

	return settings.bTrayShowTransmitMode || settings.bTrayShowNoiseCancel || settings.bTrayShowOutputDevice;
}

bool TrayIcon::addControlsSection() {
	m_recordButton->setVisible(Global::get().s.bTrayShowRecording);
	updateRecordAction();

	m_contextMenu->addAction(m_controlsAction);

	if (Global::get().s.bTrayShowTalkingUI) {
		m_contextMenu->addAction(Global::get().mw->qaTalkingUIToggle);
	}

	return true;
}

void TrayIcon::updateRecordAction() {
	const bool recording = Global::get().mw->isRecording();

	m_recordAction->setText(recording ? tr("Stop Recording") : tr("Start Recording"));
	m_recordAction->setChecked(recording);
	// Recording needs a server that allows it, just like the main window's entry does
	m_recordAction->setEnabled(recording || Global::get().mw->qaRecording->isEnabled());
}

void TrayIcon::on_contextMenu_aboutToShow() {
	updateContextMenu();

	if (Global::get().s.bTrayShowChannel) {
		// Follow the channel for as long as the menu is on screen, and no longer
		m_channelViewTimer->start();
	}
}

void TrayIcon::on_contextMenu_aboutToHide() {
	m_channelViewTimer->stop();
}

void TrayIcon::updateChannelMenu() {
	const ClientUser *self = ClientUser::get(Global::get().uiSession);
	const Channel *channel = self ? self->cChannel : nullptr;

	if (!channel) {
		m_channelMenu->clear();
		m_channelEntries.clear();

		m_channelMenu->setTitle(tr("Not Connected"));
		m_channelMenu->setEnabled(false);

		return;
	}

	m_channelMenu->setTitle(Mumble::TrayMenu::escapeMenuText(channel->qsName));
	m_channelMenu->setEnabled(true);

	QList< Mumble::TrayMenu::UserEntry > entries;

	for (const User *user : channel->qlUsers) {
		entries.append(entryFor(*static_cast< const ClientUser * >(user), false, *channel));
	}

	if (Global::get().channelListenerManager) {
		for (unsigned int session : Global::get().channelListenerManager->getListenersForChannel(channel->iId)) {
			const ClientUser *listener = ClientUser::get(session);
			if (listener) {
				entries.append(entryFor(*listener, true, *channel));
			}
		}
	}

	Mumble::TrayMenu::sortEntries(entries);

	if (sameUsers(entries, m_channelEntries)) {
		// Only the users' states changed, so the entries can be updated in place. That
		// keeps the menu from flickering (and from moving under the cursor) while it is
		// open, which happens whenever its entries are recreated.
		const QList< QAction * > actions = m_channelMenu->actions();

		for (int i = 0; i < entries.size() && i < actions.size(); ++i) {
			actions.at(i)->setIcon(iconOf(Mumble::TrayMenu::iconFor(entries.at(i).state)));
			actions.at(i)->setText(entries.at(i).label);
		}
	} else {
		m_channelMenu->clear();

		for (const Mumble::TrayMenu::UserEntry &entry : entries) {
			QAction *action = m_channelMenu->addAction(iconOf(Mumble::TrayMenu::iconFor(entry.state)), entry.label);

			if (entry.self || entry.state.listener) {
				// Emphasise the local user and set listeners apart, like the user list does
				QFont font = m_channelMenu->font();
				font.setBold(entry.self != font.bold());
				font.setItalic(entry.state.listener);
				action->setFont(font);
			}

			const unsigned int session = entry.session;
			QObject::connect(action, &QAction::triggered, this, [this, session]() {
				on_showAction_triggered();
				Global::get().mw->pmModel->setSelectedUser(session);
			});
		}
	}

	m_channelEntries = entries;
}

void TrayIcon::populateChoiceMenu(QMenu *menu, const QList< QPair< QString, QVariant > > &choices,
								  const QVariant &current, std::function< void(const QVariant &) > onPicked) {
	menu->clear();

	for (const Mumble::TrayMenu::Choice &choice : Mumble::TrayMenu::buildChoices(choices, current)) {
		QAction *action = menu->addAction(Mumble::TrayMenu::escapeMenuText(choice.label));
		action->setCheckable(true);
		action->setChecked(choice.checked);

		const QVariant value = choice.value;
		QObject::connect(action, &QAction::triggered, this, [onPicked, value]() { onPicked(value); });
	}

	// A backend that offers nothing to pick from (or none being available at all)
	// would leave an empty submenu behind, which is only confusing.
	menu->setEnabled(!menu->isEmpty());
}

void TrayIcon::on_toggleShowHide() {
	if (Global::get().mw->isVisible() && !Global::get().mw->isMinimized()) {
		on_hideAction_triggered();
	} else {
		on_showAction_triggered();
	}
}

void TrayIcon::on_showAction_triggered() {
	Global::get().mw->showRaiseWindow();
	updateContextMenu();
}

void TrayIcon::on_hideAction_triggered() {
	if (!QSystemTrayIcon::isSystemTrayAvailable()) {
		// The system reports that no system tray is available.
		// If we would hide Mumble now, there would be no way to
		// get it back...
		return;
	}

	if (qApp->activeModalWidget() || qApp->activePopupWidget()) {
		// There is one or multiple modal or popup window(s) active, which
		// would not be hidden by this call. So we also do not hide
		// the MainWindow...
		return;
	}

#ifndef Q_OS_MAC
	Global::get().mw->hide();
#else
	// Qt can not hide the window via the native macOS hide function. This should be re-evaluated with new Qt versions.
	// Instead we just minimize.
	Global::get().mw->setWindowState(Global::get().mw->windowState() | Qt::WindowMinimized);
#endif

	updateContextMenu();
}

void TrayIcon::on_windowMinimized() {
	if (!Global::get().s.bHideInTray) {
		return;
	}

	// Under tiling / EWMH window managers (e.g. XMonad) a window that merely sits
	// on a non-visible workspace is reported to Qt as minimized. Such a "minimize"
	// is really a workspace switch, not the user iconifying the window, so don't
	// hide it to tray - only genuine, same-desktop minimizes should.
	if (Mumble::X11WindowState::windowIsOnOtherDesktop(static_cast< unsigned long >(Global::get().mw->winId()))) {
		return;
	}

	on_hideAction_triggered();
}

void TrayIcon::on_tray_unhighlight() {
	if (m_highlightTimer == nullptr || !m_highlightTimer->isActive()) {
		return;
	}

	m_highlightTimer->stop();
	setIcon(m_statusIcon);
}

void TrayIcon::showNotification(const QString &title, const QString &body, QSystemTrayIcon::MessageIcon icon) {
#ifdef USE_DBUS
	// Deliver the notification through the user's notification daemon if one is running.
	// QSystemTrayIcon::showMessage() only does so for StatusNotifier-based tray icons;
	// on legacy (XEmbed) trays it draws an internal balloon widget instead.
	QString iconName;
	switch (icon) {
		case QSystemTrayIcon::Critical:
			iconName = QLatin1String("dialog-error");
			break;
		case QSystemTrayIcon::Warning:
			iconName = QLatin1String("dialog-warning");
			break;
		case QSystemTrayIcon::NoIcon:
			iconName = QLatin1String("accessories-text-editor");
			break;
		case QSystemTrayIcon::Information:
			iconName = QLatin1String("dialog-information");
			break;
	}

	QDBusInterface notificationService(QLatin1String("org.freedesktop.Notifications"),
									   QLatin1String("/org/freedesktop/Notifications"),
									   QLatin1String("org.freedesktop.Notifications"));
	if (notificationService.isValid()) {
		QVariantMap hints;
		hints.insert(QLatin1String("desktop-entry"), QLatin1String("info.mumble.Mumble"));

		const QDBusMessage response =
			notificationService.call(QLatin1String("Notify"), QLatin1String("Mumble"), m_lastNotificationId, iconName,
									 title, body, QStringList(), hints, -1);

		if (response.type() == QDBusMessage::ReplyMessage && response.arguments().count() == 1) {
			m_lastNotificationId = response.arguments().at(0).toUInt();
			return;
		}
	}
#endif

	showMessage(title, body, icon);
}

void TrayIcon::on_timer_triggered() {
	// We implement tray icon "highlighting" by blinking the
	// current status icon every few seconds until the MainWindow
	// receives focus.
	// This will only be happening, if the user selects "highlight"
	// for a specific message in the messages settings table.
	// Normal window highlighting - which desktops usually implement
	// by blinking the application in the task bar - is invisible
	// if the application is hidden to tray.

	switch (m_blinkState) {
		case BlinkState::RegularIcon:
			setIcon(Global::get().mw->m_iconInformation);
			m_highlightTimer->start(500);
			m_blinkState = BlinkState::BlinkIcon;
			break;
		case BlinkState::BlinkIcon:
			setIcon(m_statusIcon);
			m_highlightTimer->start(2000);
			m_blinkState = BlinkState::RegularIcon;
			break;
	}
}
