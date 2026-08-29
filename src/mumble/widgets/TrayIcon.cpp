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
#include "MumbleConstants.h"
#include "ServerHandler.h"
#include "TrayMenuModel.h"
#include "UserModel.h"
#include "X11WindowState.h"
#include "Global.h"

#include <QApplication>
#include <QtGui/QFont>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
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

/// The symbol a channel's menu carries, which is the one the user list shows for it:
/// the channel the local user is in stands out from the ones linked to it, and those
/// from the rest.
const QIcon &channelIconOf(Channel &channel) {
	static const QIcon plain(QLatin1String("skin:channel.svg"));
	static const QIcon active(QLatin1String("skin:channel_active.svg"));
	static const QIcon linked(QLatin1String("skin:channel_linked.svg"));

	const ClientUser *self = ClientUser::get(Global::get().uiSession);
	if (self && self->cChannel) {
		if (self->cChannel == &channel) {
			return active;
		}
		if (self->cChannel->allLinks().contains(&channel)) {
			return linked;
		}
	}

	return plain;
}

/// The name a channel's menu goes by, which is the one the user list shows for it.
QString channelTitle(const Channel &channel, int userCount) {
	const QString name = Mumble::TrayMenu::escapeMenuText(channel.qsName);

	if (!Global::get().s.bShowUserCount || userCount == 0) {
		return name;
	}

	return QString::fromLatin1("%1 (%2)").arg(name).arg(userCount);
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
		// The row is handed the menu's full width, which the buttons share evenly -
		// including when one of them is left out
		button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

		controlsBox->addWidget(button, 1);

		return button;
	};

	addControlButton(Global::get().mw->qaAudioMute);
	addControlButton(Global::get().mw->qaAudioDeaf);
	m_recordButton = addControlButton(m_recordAction);

	m_controlsAction = new QWidgetAction(this);
	m_controlsAction->setDefaultWidget(controls);

	QObject::connect(Global::get().mw->qaTalkingUIToggle, &QAction::triggered, this, &TrayIcon::updateContextMenu);

	// A view of the channel the local user is in, which is kept up to date for as
	// long as the context menu is open.
	m_channelMenu = new QMenu(Global::get().mw);
	m_channelMenu->setIcon(QIcon(QLatin1String("skin:channel.svg")));

	m_channelViewTimer = new QTimer(this);
	m_channelViewTimer->setInterval(channelViewUpdateInterval);
	QObject::connect(m_channelViewTimer, &QTimer::timeout, this, &TrayIcon::refreshChannelMenus);

	// Submenus mirroring the main window's toolbar dropdowns. They are filled in
	// whenever the context menu is about to be shown, as their entries depend on
	// what the audio backend currently offers.
	m_transmitModeMenu = new QMenu(tr("Transmit Mode"), Global::get().mw);
	m_noiseCancelMenu  = new QMenu(tr("Noise Suppression"), Global::get().mw);
	m_outputDeviceMenu = new QMenu(tr("Output Device"), Global::get().mw);

	m_contextMenu = new QMenu(Global::get().mw);
	QObject::connect(m_contextMenu, &QMenu::aboutToShow, this, &TrayIcon::on_contextMenu_aboutToShow);
	QObject::connect(m_contextMenu, &QMenu::aboutToHide, this, &TrayIcon::on_contextMenu_aboutToHide);

	// Once a context menu is done with, its entry goes back to carrying none. This has
	// to wait for the menu to be gone, hence the queued connections.
	QObject::connect(Global::get().mw->qmUser, &QMenu::aboutToHide, this, &TrayIcon::disarmUserContextMenu,
					 Qt::QueuedConnection);
	QObject::connect(Global::get().mw->qmListener, &QMenu::aboutToHide, this, &TrayIcon::disarmUserContextMenu,
					 Qt::QueuedConnection);

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

	for (Mumble::TrayMenu::Section section : Mumble::TrayMenu::parseSectionOrder(Global::get().s.qslTrayMenuOrder)) {
		switch (section) {
			case Mumble::TrayMenu::Section::Channels:
				closeSection(addChannelSection());
				break;
			case Mumble::TrayMenu::Section::AudioDevices:
				closeSection(addAudioDeviceSection());
				break;
			case Mumble::TrayMenu::Section::Controls:
				closeSection(addControlsSection());
				break;
		}
	}

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

	QMetaObject::invokeMethod(this, [this]() { disarmUserContextMenu(); }, Qt::QueuedConnection);
}

void TrayIcon::updateChannelMenu() {
	if (!Global::get().uiSession) {
		clearChannelMenus();

		m_channelMenu->setTitle(tr("Not Connected"));
		m_channelMenu->setIcon(QIcon(QLatin1String("skin:channel.svg")));
		m_channelMenu->setEnabled(false);

		return;
	}

	// Channels that were removed while the menu was closed leave their menu behind
	for (auto it = m_channelMenus.begin(); it != m_channelMenus.end();) {
		if (it.key() != Mumble::ROOT_CHANNEL_ID && !Channel::get(it.key())) {
			delete it->menu;
			it = m_channelMenus.erase(it);
		} else {
			++it;
		}
	}

	m_channelMenu->setEnabled(true);

	// Filling the root menu right away gives the entry its title, which is the one
	// thing of the tree that is visible without opening it
	fillChannelMenu(Mumble::ROOT_CHANNEL_ID);
}

QMenu *TrayIcon::channelMenu(unsigned int channelId) {
	const auto it = m_channelMenus.constFind(channelId);
	if (it != m_channelMenus.constEnd()) {
		return it->menu;
	}

	ChannelMenu record;
	// The tree's root is the entry the menu itself hangs off of, the rest are its
	// descendants and are owned by it
	record.menu = channelId == Mumble::ROOT_CHANNEL_ID ? m_channelMenu : new QMenu(m_channelMenu);

	// Only the branches the user actually walks into are built
	QObject::connect(record.menu, &QMenu::aboutToShow, this, [this, channelId]() { fillChannelMenu(channelId); });

	record.menu->installEventFilter(this);

	m_channelMenus.insert(channelId, record);

	return record.menu;
}

void TrayIcon::fillChannelMenu(unsigned int channelId, bool allowRebuild) {
	Channel *channel = Channel::get(channelId);
	if (!channel) {
		return;
	}

	QList< Mumble::TrayMenu::UserEntry > users;
	for (const User *user : channel->qlUsers) {
		users.append(entryFor(*static_cast< const ClientUser * >(user), false, *channel));
	}

	if (Global::get().channelListenerManager) {
		for (unsigned int session : Global::get().channelListenerManager->getListenersForChannel(channelId)) {
			const ClientUser *listener = ClientUser::get(session);
			if (listener) {
				users.append(entryFor(*listener, true, *channel));
			}
		}
	}

	Mumble::TrayMenu::sortEntries(users);

	QList< Channel * > subChannels;
	for (Channel *subChannel : channel->qlChannels) {
		// The user list hides filtered channels, and so does this
		if (!subChannel->isFiltered()) {
			subChannels.append(subChannel);
		}
	}
	std::sort(subChannels.begin(), subChannels.end(), Channel::lessThan);

	QList< unsigned int > subChannelIds;
	QList< QMenu * > subChannelMenus;
	for (const Channel *subChannel : subChannels) {
		subChannelIds.append(subChannel->iId);
		// Before taking the record below: this may insert into the very hash it lives in
		subChannelMenus.append(channelMenu(subChannel->iId));
	}

	QMenu *menu        = channelMenu(channelId);
	ChannelMenu record = m_channelMenus.value(channelId);
	const bool contents =
		record.filled && record.subChannels == subChannelIds && Mumble::TrayMenu::sameUsers(users, record.users);

	menu->setTitle(channelTitle(*channel, static_cast< int >(users.size())));
	menu->setIcon(channelIconOf(*channel));

	if (contents) {
		// Only the users' states changed, so the entries can be updated in place. That
		// keeps the menu from flickering (and from moving under the cursor) while it is
		// open, which happens whenever its entries are recreated.
		for (int i = 0; i < users.size() && i < record.userActions.size(); ++i) {
			record.userActions.at(i)->setIcon(iconOf(Mumble::TrayMenu::iconFor(users.at(i).state)));
			record.userActions.at(i)->setText(users.at(i).label);
		}

		record.users = users;
		m_channelMenus.insert(channelId, record);

		return;
	}

	if (!allowRebuild) {
		// One of the shared context menus is open, and its entry must not be pulled
		// out from under it. The channel is picked up again on the next refresh.
		return;
	}

	for (QAction *action : record.userActions) {
		m_userContextMenus.remove(action);
	}

	menu->clear();
	record.userActions.clear();

	const ClientUser *self = ClientUser::get(Global::get().uiSession);

	if (self && self->cChannel != channel) {
		QAction *join = menu->addAction(tr("Join Channel"));
		QObject::connect(join, &QAction::triggered, this, [channelId]() {
			if (Global::get().sh) {
				Global::get().sh->joinChannel(Global::get().uiSession, channelId);
			}
		});
	}

	QAction *channelActions = menu->addAction(tr("Channel Actions"));
	// The main window's own channel menu, which reads what it acts on from the user
	// list - hence pointing that at this channel as soon as the entry is highlighted
	channelActions->setMenu(Global::get().mw->qmChannel);
	QObject::connect(channelActions, &QAction::hovered, this, [this, channelId]() { selectChannel(channelId); });

	menu->addSeparator();

	const auto addUsers = [this, menu, &record, &users, channelId]() {
		for (const Mumble::TrayMenu::UserEntry &entry : users) {
			QAction *action = menu->addAction(iconOf(Mumble::TrayMenu::iconFor(entry.state)), entry.label);

			if (entry.self || entry.state.listener) {
				// Emphasise the local user and set listeners apart, like the user list does
				QFont font = menu->font();
				font.setBold(entry.self != font.bold());
				font.setItalic(entry.state.listener);
				action->setFont(font);
			}

			// The entry is handed its context menu when it is clicked, not before, so
			// that pointing at a user does not open one
			const unsigned int session = entry.session;
			if (entry.state.listener) {
				m_userContextMenus.insert(action, Global::get().mw->qmListener);
				QObject::connect(action, &QAction::hovered, this,
								 [this, session, channelId]() { selectListener(session, channelId); });
			} else {
				m_userContextMenus.insert(action, Global::get().mw->qmUser);
				QObject::connect(action, &QAction::hovered, this, [this, session]() { selectUser(session); });
			}

			record.userActions.append(action);
		}
	};

	const auto addSubChannels = [menu, &subChannelMenus]() {
		for (QMenu *subChannelMenu : subChannelMenus) {
			menu->addMenu(subChannelMenu);
		}
	};

	// The user list can be configured to put the users of a channel above or below
	// its subchannels, and the tray follows suit
	if (Global::get().s.bUserTop) {
		addUsers();
		addSubChannels();
	} else {
		addSubChannels();
		addUsers();
	}

	record.users       = users;
	record.subChannels = subChannelIds;
	record.filled      = true;

	m_channelMenus.insert(channelId, record);
}

void TrayIcon::refreshChannelMenus() {
	if (!Global::get().uiSession) {
		updateChannelMenu();
		return;
	}

	// Rebuilding a menu deletes the entries the shared context menus hang off of, so
	// while one of those is open the tree only gets its states refreshed
	const MainWindow *mw       = Global::get().mw;
	const bool contextMenuOpen = mw->qmUser->isVisible() || mw->qmChannel->isVisible() || mw->qmListener->isVisible();

	// The root menu is refreshed even when it is not on screen, as its title is what
	// the tray menu shows for the whole tree
	fillChannelMenu(Mumble::ROOT_CHANNEL_ID, !contextMenuOpen);

	for (unsigned int channelId : m_channelMenus.keys()) {
		const auto it = m_channelMenus.constFind(channelId);
		if (it != m_channelMenus.constEnd() && it->menu->isVisible()) {
			fillChannelMenu(channelId, !contextMenuOpen);
		}
	}
}

void TrayIcon::clearChannelMenus() {
	disarmUserContextMenu();
	m_userContextMenus.clear();

	for (auto it = m_channelMenus.begin(); it != m_channelMenus.end(); ++it) {
		if (it.key() == Mumble::ROOT_CHANNEL_ID) {
			it->menu->clear();
		} else {
			delete it->menu;
		}
	}

	m_channelMenus.clear();
}

bool TrayIcon::eventFilter(QObject *object, QEvent *event) {
	QMenu *menu = qobject_cast< QMenu * >(object);
	if (!menu) {
		return QSystemTrayIcon::eventFilter(object, event);
	}

	switch (event->type()) {
		case QEvent::MouseButtonPress:
			// Qt opens the entry's menu right after this, if it has one by then
			armUserContextMenu(menu->actionAt(static_cast< QMouseEvent * >(event)->position().toPoint()));
			break;
		case QEvent::KeyPress: {
			const int key = static_cast< QKeyEvent * >(event)->key();
			if (key != Qt::Key_Right && key != Qt::Key_Return && key != Qt::Key_Enter) {
				break;
			}

			QAction *action = menu->activeAction();
			if (!m_userContextMenus.contains(action)) {
				break;
			}

			armUserContextMenu(action);
			// Opens what the key press would have opened by itself, had the entry
			// been carrying its menu all along
			menu->setActiveAction(action);

			return true;
		}
		default:
			break;
	}

	return QSystemTrayIcon::eventFilter(object, event);
}

void TrayIcon::armUserContextMenu(QAction *action) {
	const auto it = m_userContextMenus.constFind(action);
	if (it == m_userContextMenus.constEnd() || m_armedUserAction == action) {
		// Clicking the entry whose menu is already open must not take that menu away
		// from underneath it
		return;
	}

	disarmUserContextMenu();

	action->setMenu(*it);
	m_armedUserAction = action;
}

void TrayIcon::disarmUserContextMenu() {
	if (m_armedUserAction) {
		m_armedUserAction->setMenu(nullptr);
		m_armedUserAction.clear();
	}
}

void TrayIcon::selectUser(unsigned int session) {
	Global::get().mw->pmModel->setSelectedUser(session);
}

void TrayIcon::selectChannel(unsigned int channelId) {
	Global::get().mw->pmModel->setSelectedChannel(channelId);
}

void TrayIcon::selectListener(unsigned int session, unsigned int channelId) {
	Global::get().mw->pmModel->setSelectedChannelListener(session, channelId);
	// A listener's entry in the user list does not name a channel of its own, so the
	// channel it listens to has to be handed over separately
	Global::get().mw->setContextMenuTarget(nullptr, Channel::get(channelId));
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
