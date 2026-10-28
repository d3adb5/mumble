// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "Webcam.h"

#include "ClientUser.h"
#include "Log.h"
#include "MainWindow.h"
#include "ServerHandler.h"
#include "WebcamGate.h"
#include "WebcamStream.h"
#include "WebcamWindow.h"
#include "Global.h"

#include <QtCore/QReadLocker>
#include <QtCore/QTimer>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>

using namespace Mumble::Webcam;

namespace {
/// How long a viewer waits for an answer: long enough for a person to click "allow".
constexpr int ANSWER_TIMEOUT_MS = 60000;
/// How often and how many times a viewer tries the relay before giving up. The sender's camera
/// may still be opening (it opens on demand), or the stream may just have moved.
constexpr int RETRY_INTERVAL_MS = 1500;
constexpr int RETRY_ATTEMPTS    = 10;

constexpr int POLICY_ALLOW_ALL = 0;
constexpr int POLICY_DENY_ALL  = 2;

/// Stops a stream and deletes it once control is back in the event loop: this may be reached
/// from one of the stream's own signals.
void discardStream(std::unique_ptr< WebcamViewerStream > &stream) {
	if (WebcamViewerStream *old = stream.release()) {
		old->disconnect();
		old->stop();
		old->deleteLater();
	}
}

QString userName(unsigned int session) {
	ClientUser *user = ClientUser::get(session);
	return user ? Log::formatClientUser(user, Log::Source) : QObject::tr("Someone");
}

void logInfo(const QString &message) {
	Global::get().l->log(Log::Information, message);
}

/// The relay login set under Settings, Camera: the same one that shares, and the one that watches.
Login ownLogin() {
	const Settings &s = Global::get().s;
	return { s.qsWebcamPublishUser.toStdString(), s.qsWebcamPublishPassword.toStdString() };
}

/// Asks for the relay and a login on it, and keeps them in the settings; \p refused says the
/// relay turned the current one down. False if the user gave none.
bool askRelayLogin(bool refused = false) {
	Settings &s = Global::get().s;
	QDialog dialog(Global::get().mw);
	dialog.setWindowTitle(QObject::tr("Relay login"));
	auto *layout = new QFormLayout(&dialog);
	auto *info   = new QLabel(refused ? QObject::tr("The camera relay refused your login. Check it: it is kept under "
													"Settings, Camera, and also used to share your camera.")
									  : QObject::tr("The camera relay wants your own login before you can watch. It is "
													"kept under Settings, Camera, and also used to share your camera."),
							  &dialog);
	info->setWordWrap(true);
	layout->addRow(info);
	auto *relay    = new QLineEdit(s.qsWebcamRelay, &dialog);
	auto *user     = new QLineEdit(s.qsWebcamPublishUser, &dialog);
	auto *password = new QLineEdit(s.qsWebcamPublishPassword, &dialog);
	relay->setPlaceholderText(QLatin1String("rtsps://relay.example.org:8322"));
	password->setEchoMode(QLineEdit::Password);
	layout->addRow(QObject::tr("Relay"), relay);
	layout->addRow(QObject::tr("User"), user);
	layout->addRow(QObject::tr("Password"), password);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addRow(buttons);

	if (dialog.exec() != QDialog::Accepted || relay->text().trimmed().isEmpty() || user->text().isEmpty()) {
		return false;
	}
	s.qsWebcamRelay           = QString::fromStdString(withDefaultPort(relay->text().trimmed().toStdString()));
	s.qsWebcamPublishUser     = user->text();
	s.qsWebcamPublishPassword = password->text();
	s.save();
	return true;
}
} // namespace

WebcamManager::WebcamManager(QObject *parent) : QObject(parent), m_gate(new WebcamGate(this)) {
	m_shareRenew = new QTimer(this);
	m_shareRenew->setInterval(GATE_SHARE_RENEW_MS);
	connect(m_shareRenew, &QTimer::timeout, this, [this] {
		if (m_sharing && m_gateMode == GateMode::Gated) {
			claimShare();
		}
	});
}

WebcamManager::~WebcamManager() {
	serverDisconnected();
}

// -- plumbing ----------------------------------------------------------------------------------

void WebcamManager::send(const QList< unsigned int > &sessions, const char *dataId, const std::string &data) {
	const ServerHandlerPtr sh = Global::get().sh;
	if (!sh || sessions.isEmpty() || data.size() > MAX_MESSAGE_SIZE) {
		return;
	}
	// The server relays plugin data since 1.4.0.
	if (sh->m_version < Version::fromComponents(1, 4, 0)) {
		return;
	}
	MumbleProto::PluginDataTransmission mpdt;
	mpdt.set_sendersession(Global::get().uiSession);
	for (unsigned int session : sessions) {
		mpdt.add_receiversessions(session);
	}
	mpdt.set_data(data);
	mpdt.set_dataid(dataId);
	sh->sendMessage(mpdt);
}

void WebcamManager::broadcastShareState() {
	QList< unsigned int > sessions;
	{
		QReadLocker lock(&ClientUser::c_qrwlUsers);
		for (auto it = ClientUser::c_qmUsers.constBegin(); it != ClientUser::c_qmUsers.constEnd(); ++it) {
			if (it.key() != Global::get().uiSession) {
				sessions << it.key();
			}
		}
	}
	const Quality quality = wantedQuality();
	send(sessions, ID_SHARE_STATE, encodeShareState({ m_sharing, quality.width, quality.height, quality.fps }));
}

Quality WebcamManager::wantedQuality() const {
	const Settings &s = Global::get().s;
	return qualityAt(s.iWebcamQuality, s.iWebcamCustomHeight, s.iWebcamCustomFps, s.iWebcamCustomKbps);
}

QString WebcamManager::relayBase() const {
	QString relay = Global::get().s.qsWebcamRelay.trimmed();
	while (relay.endsWith(QLatin1Char('/'))) {
		relay.chop(1);
	}
	// Without a port, GStreamer would publish to 554, where the relay is not.
	return QString::fromStdString(withDefaultPort(relay.toStdString()));
}

QString WebcamManager::streamUrl() const {
	return QString(relayBase() + QLatin1Char('/') + QString::fromStdString(m_path));
}

void WebcamManager::handleData(unsigned int sender, const std::string &dataId, const std::string &data) {
	if (sender == Global::get().uiSession) {
		return;
	}
	if (dataId == ID_WATCH_REQUEST) {
		onRequest(sender, data);
	} else if (dataId == ID_WATCH_LEAVE) {
		if (decodeEmpty(data)) {
			onLeave(sender);
		}
	} else if (dataId == ID_WATCH_GRANT) {
		onGrant(sender, data);
	} else if (dataId == ID_WATCH_DENY) {
		onDeny(sender, data);
	} else if (dataId == ID_WATCH_REKEY) {
		onRekey(sender, data);
	} else if (dataId == ID_WATCH_REVOKE) {
		if (decodeEmpty(data) && m_watches.contains(sender)) {
			endWatch(sender, false, tr("%1 stopped letting you watch their camera.").arg(userName(sender)));
		}
	} else if (dataId == ID_SHARE_STATE) {
		onShareState(sender, data);
	}
}

void WebcamManager::userJoined(unsigned int session) {
	if (m_sharing && session != Global::get().uiSession) {
		const Quality quality = wantedQuality();
		send({ session }, ID_SHARE_STATE, encodeShareState({ true, quality.width, quality.height, quality.fps }));
	}
}

void WebcamManager::userLeft(unsigned int session) {
	if (m_asks.contains(session)) {
		PendingAsk ask = m_asks.take(session);
		if (ask.box) {
			ask.box->deleteLater();
		}
	}
	m_pendingGrants.remove(session);
	if (m_viewers.contains(session)) {
		viewerGone(session, false);
	}
	if (m_watches.contains(session)) {
		endWatch(session, false);
	}
}

void WebcamManager::serverDisconnected() {
	for (unsigned int session : m_watches.keys()) {
		endWatch(session, false);
	}
	for (PendingAsk &ask : m_asks) {
		if (ask.box) {
			ask.box->deleteLater();
		}
	}
	m_asks.clear();
	m_pendingGrants.clear();
	m_viewers.clear();
	stopPublishing();
	m_shareRenew->stop();
	if (m_gateMode == GateMode::Gated) {
		if (!m_path.empty()) {
			m_staleShares.push_back(m_path);
		}
		deleteStaleShares();
	}
	m_staleShares.clear();
	m_shareClaimed    = false;
	m_publishClaiming = false;
	closePreview();
	m_path.clear();
	m_key.clear();
	if (m_sharing) {
		m_sharing = false;
		emit sharingChanged(false);
	}
}

// -- sharing -----------------------------------------------------------------------------------

bool WebcamManager::startSharing() {
	if (m_sharing) {
		return true;
	}
	const Settings &s = Global::get().s;
	QString error;
	if (!WebcamStream::ensureInitialized(&error)) {
		logInfo(tr("Camera sharing is unavailable: %1").arg(error.toHtmlEscaped()));
		return false;
	}
	if (!relayBase().startsWith(QLatin1String("rtsps://"))) {
		logInfo(tr("Set an RTSPS relay (rtsps://host:port) under Settings, Camera, before sharing your camera."));
		return false;
	}
	if (s.qsWebcamPublishUser.isEmpty()) {
		logInfo(tr("Set your relay login under Settings, Camera, before sharing your camera."));
		return false;
	}
	if (!Global::get().sh || Global::get().sh->m_version < Version::fromComponents(1, 4, 0)) {
		logInfo(tr("This server is too old to relay camera sharing (it needs 1.4.0 or later)."));
		return false;
	}
	if (!newStreamSecrets()) {
		logInfo(tr("Camera sharing could not create its keys."));
		return false;
	}

	m_sharing         = true;
	m_gateMode        = GateMode::Unknown;
	m_shareClaimed    = false;
	m_publishClaiming = false;
	m_pendingMove     = false;
	// The relay opens to our IP only once the gate holds the path, so publishing (and any grant)
	// waits for the claim.
	m_shareRenew->start();
	claimShare();
	if (ClientUser *self = ClientUser::get(Global::get().uiSession)) {
		self->setWebcamSharing(true);
	}
	broadcastShareState();
	if (s.bWebcamPreview) {
		showPreview();
	}
	logInfo(tr("You are sharing your camera. Others can ask to watch it from your context menu."));
	emit sharingChanged(true);
	return true;
}

void WebcamManager::stopSharing(const QString &reason) {
	if (!m_sharing) {
		return;
	}
	m_sharing = false;
	for (PendingAsk &ask : m_asks) {
		if (ask.box) {
			ask.box->deleteLater();
		}
	}
	m_asks.clear();
	m_pendingGrants.clear();
	m_viewers.clear();
	stopPublishing();
	m_shareRenew->stop();
	if (m_gateMode == GateMode::Gated) {
		if (!m_path.empty()) {
			m_staleShares.push_back(m_path);
		}
		deleteStaleShares();
	}
	m_staleShares.clear();
	m_shareClaimed    = false;
	m_publishClaiming = false;
	closePreview();
	m_path.clear();
	m_key.clear();

	broadcastShareState();
	if (ClientUser *self = ClientUser::get(Global::get().uiSession)) {
		self->setWebcamSharing(false);
	}
	logInfo(reason.isEmpty() ? tr("You stopped sharing your camera.")
							 : tr("Your camera share stopped: %1").arg(reason.toHtmlEscaped()));
	emit sharingChanged(false);
}

bool WebcamManager::newStreamSecrets() {
	const std::string path = randomStreamPath();
	const Bytes key        = randomBytes(KEY_SIZE);
	if (path.empty() || key.size() != KEY_SIZE) {
		return false;
	}
	m_path = path;
	m_key  = key;
	m_kid  = static_cast< std::uint8_t >(m_kid + 1);
	return true;
}

StreamSecrets WebcamManager::currentSecrets() const {
	const Settings &s = Global::get().s;
	StreamSecrets secrets;
	secrets.urls = { streamUrl().toStdString() };
	if (!s.qsWebcamReadUser.isEmpty()) {
		secrets.user = s.qsWebcamReadUser.toStdString();
		secrets.pass = s.qsWebcamReadPassword.toStdString();
	}
	secrets.kid = m_kid;
	secrets.key = m_key;
	return secrets;
}

bool WebcamManager::ensurePublishing() {
	if (m_publisher && m_publisher->isRunning()) {
		return true;
	}
	if (m_gateMode == GateMode::Gated) {
		// Every publish connection claims the path first, which also opens our IP.
		if (!m_publishClaiming) {
			m_publishClaiming = true;
			claimShare(true);
		}
		return true;
	}
	return publishNow();
}

bool WebcamManager::publishNow() {
	if (m_publisher && m_publisher->isRunning()) {
		return true;
	}
	const Settings &s = Global::get().s;
	WebcamPublisher::Config config;
	config.device   = s.qsWebcamDevice;
	config.quality  = wantedQuality();
	config.encoder  = s.qsWebcamEncoder;
	config.url      = streamUrl();
	config.user     = s.qsWebcamPublishUser;
	config.password = s.qsWebcamPublishPassword;
	config.caFile   = s.qsWebcamCaFile;
	// Cheap (small and leaky), and it lets a preview opened later show at once.
	config.preview = true;

	m_publisher = std::make_unique< WebcamPublisher >();
	connect(m_publisher.get(), &WebcamPipeline::frame, this, [this](const QImage &image) {
		if (m_preview) {
			m_preview->setFrame(image);
		}
	});
	connect(m_publisher.get(), &WebcamPipeline::failed, this, [this](const QString &message) { stopSharing(message); });
	connect(m_publisher.get(), &WebcamPipeline::ended, this,
			[this] { stopSharing(tr("the relay closed the stream.")); });

	QString error;
	if (!m_publisher->start(config, m_kid, m_key, &error)) {
		stopSharing(error);
		return false;
	}
	updatePreviewStatus();
	return true;
}

void WebcamManager::stopPublishing() {
	if (m_publisher) {
		// Destroyed later: stop() may be reached from one of its own signals.
		WebcamPublisher *publisher = m_publisher.release();
		publisher->disconnect(this);
		publisher->stop();
		publisher->deleteLater();
	}
	updatePreviewStatus();
}

void WebcamManager::showPreview() {
	if (!m_sharing) {
		return;
	}
	if (!m_preview) {
		m_preview = new WebcamWindow(tr("Your camera (only you see this)"), QByteArray(), nullptr);
		connect(m_preview, &WebcamWindow::closedByUser, this, [this] { closePreview(); });
		updatePreviewStatus();
	}
	m_preview->show();
	m_preview->raise();
	m_preview->activateWindow();
}

void WebcamManager::updatePreviewStatus() {
	if (!m_preview) {
		return;
	}
	if (m_publisher && m_publisher->isRunning()) {
		// The first picture replaces this.
		m_preview->setStatus(tr("Opening your camera…"));
		return;
	}
	m_preview->clearFrame();
	m_preview->setStatus(Global::get().s.bWebcamOnDemand ? tr("Your camera stays off until someone watches it.")
														 : tr("Your camera is off."));
}

void WebcamManager::closePreview() {
	if (WebcamWindow *preview = m_preview) {
		m_preview = nullptr;
		preview->disconnect(this);
		preview->close();
		preview->deleteLater();
	}
}

void WebcamManager::onRequest(unsigned int sender, const std::string &data) {
	const std::optional< WatchRequest > request = decodeWatchRequest(data);
	if (!request) {
		return;
	}
	if (!m_sharing) {
		deny(sender, request->nonce, DENY_NOT_SHARING);
		return;
	}
	if (request->publicKey.empty()) {
		deny(sender, request->nonce, DENY_UNSUPPORTED);
		return;
	}

	const int policy = Global::get().s.iWebcamPolicy;
	if (policy == POLICY_DENY_ALL) {
		deny(sender, request->nonce, DENY_POLICY);
		return;
	}
	// Someone already allowed during this share is not asked again.
	if (policy == POLICY_ALLOW_ALL || m_viewers.contains(sender)) {
		grant(sender, request->nonce, request->publicKey);
		return;
	}

	if (m_asks.contains(sender) && m_asks[sender].box) {
		m_asks[sender].box->deleteLater();
	}
	auto *box =
		new QMessageBox(QMessageBox::Question, tr("Camera"), tr("%1 wants to watch your camera.").arg(userName(sender)),
						QMessageBox::NoButton, Global::get().mw);
	box->setTextFormat(Qt::RichText);
	QPushButton *allow = box->addButton(tr("Allow"), QMessageBox::AcceptRole);
	box->addButton(tr("Deny"), QMessageBox::RejectRole);
	box->setAttribute(Qt::WA_DeleteOnClose);
	box->setModal(false);
	m_asks.insert(sender, { request->nonce, request->publicKey, box });

	connect(box, &QMessageBox::buttonClicked, this, [this, sender, allow, box](QAbstractButton *button) {
		if (!m_asks.contains(sender) || m_asks[sender].box != box) {
			return;
		}
		const PendingAsk ask = m_asks.take(sender);
		if (!m_sharing) {
			deny(sender, ask.nonce, DENY_NOT_SHARING);
		} else if (button == allow) {
			grant(sender, ask.nonce, ask.publicKey);
		} else {
			deny(sender, ask.nonce, DENY_DECLINED);
		}
	});
	box->show();
}

void WebcamManager::grant(unsigned int session, const std::string &nonce, const Bytes &publicKey) {
	if (m_gateMode == GateMode::Unknown || (m_gateMode == GateMode::Gated && !m_shareClaimed)) {
		// Goes out once the gate holds the path (shareReady).
		m_pendingGrants.insert(session, { nonce, publicKey });
		return;
	}
	if (!ensurePublishing()) {
		deny(session, nonce, DENY_NOT_SHARING);
		return;
	}
	auto sendGrant = [this, session, nonce, publicKey](const StreamSecrets &secrets, const std::string &ticketId) {
		const std::optional< std::string > message = encodeWatchGrant(nonce, publicKey, secrets);
		if (!message) {
			deny(session, nonce, DENY_UNSUPPORTED);
			return;
		}
		const bool already = m_viewers.contains(session);
		m_viewers.insert(session, { publicKey, ticketId });
		send({ session }, ID_WATCH_GRANT, *message);
		if (!already) {
			logInfo(tr("%1 is watching your camera.").arg(userName(session)));
		}
	};
	if (m_gateMode == GateMode::Ungated) {
		sendGrant(currentSecrets(), {});
		return;
	}

	const std::string path = m_path;
	m_gate->createTicket(path, [this, session, nonce, publicKey, path, sendGrant](const WebcamGate::Reply &reply) {
		if (!m_sharing) {
			deny(session, nonce, DENY_NOT_SHARING);
			return;
		}
		if (path != m_path) {
			// The stream moved meanwhile: a ticket for the new path instead.
			grant(session, nonce, publicKey);
			return;
		}
		if (const std::optional< GateTicket > ticket = WebcamGate::ticketFrom(reply)) {
			StreamSecrets secrets = currentSecrets();
			secrets.gate          = ticket;
			sendGrant(secrets, ticket->id);
		} else if (reply.answer == GateAnswer::Unknown) {
			// The gate forgot the share (it restarted): claim it again, the grant waits.
			m_shareClaimed = false;
			m_pendingGrants.insert(session, { nonce, publicKey });
			claimShare();
		} else {
			deny(session, nonce, DENY_UNSUPPORTED);
		}
	});
}

void WebcamManager::claimShare(bool thenPublish) {
	const std::string path = m_path;
	m_gate->putShare(path, [this, path, thenPublish](const WebcamGate::Reply &reply) {
		if (thenPublish) {
			m_publishClaiming = false;
		}
		if (!m_sharing || path != m_path) {
			return;
		}
		switch (reply.answer) {
			case GateAnswer::Ok:
			case GateAnswer::Created: {
				const bool first = !m_shareClaimed;
				m_gateMode       = GateMode::Gated;
				m_shareClaimed   = true;
				if (first) {
					// The new path is ours: the old ones go, and the viewers move over.
					deleteStaleShares();
					shareReady();
				} else {
					if (reply.answer == GateAnswer::Created) {
						// The gate forgot the share and every ticket on it.
						reticket(false);
					}
					if (thenPublish) {
						publishNow();
					}
				}
				break;
			}
			case GateAnswer::BadLogin:
				stopSharing(tr("the relay refused your login. Check it under Settings, Camera."));
				break;
			case GateAnswer::Forbidden:
				stopSharing(tr("the relay holds the stream's path for someone else."));
				break;
			case GateAnswer::Limited:
				if (!m_shareClaimed) {
					stopSharing(tr("the relay is at its limit."));
				}
				break;
			case GateAnswer::NoGate:
				if (m_gateMode == GateMode::Unknown) {
					// A relay without a gate (v1, as production is today): everything goes out
					// as before, without tickets.
					m_gateMode = GateMode::Ungated;
					m_shareRenew->stop();
					shareReady();
				} else if (thenPublish && m_shareClaimed) {
					// The gate is unreachable for now; the claim may still hold, so try. The
					// next renewal tries the gate again.
					publishNow();
				}
				break;
			case GateAnswer::Unknown:
			case GateAnswer::Expired:
				break;
		}
	});
}

void WebcamManager::shareReady() {
	if (m_pendingMove) {
		m_pendingMove = false;
		if (m_gateMode == GateMode::Gated) {
			reticket(true);
		} else {
			const StreamSecrets secrets = currentSecrets();
			for (auto it = m_viewers.constBegin(); it != m_viewers.constEnd(); ++it) {
				if (const std::optional< std::string > message = encodeWatchRekey(it->publicKey, secrets)) {
					send({ it.key() }, ID_WATCH_REKEY, *message);
				}
			}
		}
	}
	// Reached right after a claim (or with no gate), so the publish connection may go now.
	if (!m_viewers.isEmpty() || !Global::get().s.bWebcamOnDemand) {
		if (!publishNow()) {
			return;
		}
	}
	const QHash< unsigned int, PendingGrant > pending = std::move(m_pendingGrants);
	m_pendingGrants.clear();
	for (auto it = pending.constBegin(); it != pending.constEnd(); ++it) {
		grant(it.key(), it->nonce, it->publicKey);
	}
}

void WebcamManager::deleteStaleShares() {
	for (const std::string &path : m_staleShares) {
		m_gate->deleteShare(path);
	}
	m_staleShares.clear();
}

void WebcamManager::reticket(bool moved) {
	const std::string path = m_path;
	for (auto it = m_viewers.constBegin(); it != m_viewers.constEnd(); ++it) {
		const unsigned int session = it.key();
		m_gate->createTicket(path, [this, session, path, moved](const WebcamGate::Reply &reply) {
			const std::optional< GateTicket > ticket = WebcamGate::ticketFrom(reply);
			// A failure leaves the viewer waiting; the next renewal that finds the share
			// forgotten mints again.
			if (!m_sharing || path != m_path || !ticket || !m_viewers.contains(session)) {
				return;
			}
			Viewer &viewer        = m_viewers[session];
			viewer.ticketId       = ticket->id;
			StreamSecrets secrets = currentSecrets();
			if (!moved) {
				// The stream carries on; only the ticket is new.
				secrets.urls.clear();
			}
			secrets.gate = ticket;
			if (const std::optional< std::string > message = encodeWatchRekey(viewer.publicKey, secrets)) {
				send({ session }, ID_WATCH_REKEY, *message);
			}
		});
	}
}

void WebcamManager::deny(unsigned int session, const std::string &nonce, const char *reason) {
	send({ session }, ID_WATCH_DENY, encodeWatchDeny({ nonce, reason }));
}

void WebcamManager::onLeave(unsigned int sender) {
	if (m_viewers.contains(sender)) {
		logInfo(tr("%1 stopped watching your camera.").arg(userName(sender)));
		viewerGone(sender, false);
	}
}

void WebcamManager::revoke(unsigned int session) {
	if (m_viewers.contains(session)) {
		logInfo(tr("You stopped letting %1 watch your camera.").arg(userName(session)));
		viewerGone(session, true);
	}
}

void WebcamManager::viewerGone(unsigned int session, bool sendRevoke) {
	const Viewer gone = m_viewers.take(session);
	if (sendRevoke) {
		send({ session }, ID_WATCH_REVOKE, encodeEmpty());
	}
	if (m_gateMode == GateMode::Gated) {
		// The relay cuts them off itself; the move below is for the key. The old path is let go
		// once the new one is claimed.
		if (!gone.ticketId.empty()) {
			m_gate->deleteTicket(m_path, gone.ticketId);
		}
		m_staleShares.push_back(m_path);
	}

	// Whoever is gone still knows the path and key. Nobody left: the camera closes (when it opens
	// on demand) and the next viewer gets a fresh path. Others left: they move to a fresh path
	// now. The camera cannot be opened twice, so the old stream stops first and the viewers keep
	// retrying until the new one is up.
	stopPublishing();
	if (!newStreamSecrets()) {
		stopSharing(tr("new keys could not be created."));
		return;
	}
	m_pendingMove = !m_viewers.isEmpty();
	if (m_gateMode == GateMode::Gated) {
		m_shareClaimed = false;
		claimShare();
	} else {
		shareReady();
	}
}

// -- watching ----------------------------------------------------------------------------------

void WebcamManager::watch(unsigned int session) {
	if (Watch *existing = m_watches.value(session)) {
		if (existing->window) {
			existing->window->show();
			existing->window->raise();
			existing->window->activateWindow();
		}
		return;
	}
	if (!ClientUser::get(session) || session == Global::get().uiSession) {
		return;
	}
	QString error;
	if (!WebcamStream::ensureInitialized(&error)) {
		logInfo(tr("Watching cameras is unavailable: %1").arg(error.toHtmlEscaped()));
		return;
	}
	// The relay lets nobody read without a login of their own. Watching needs only that: no
	// camera, and no sharing.
	if ((ownLogin().user.empty() || relayBase().isEmpty()) && !askRelayLogin()) {
		return;
	}
	// The user may have left while the login was asked for.
	ClientUser *user = ClientUser::get(session);
	if (!user || m_watches.contains(session)) {
		return;
	}
	auto *watch = new Watch();
	watch->window =
		new WebcamWindow(tr("%1's camera").arg(user->qsName), Global::get().s.qbaWebcamWindowGeometry, nullptr);
	// Every window restores the same geometry, so step each further one aside instead of
	// stacking it exactly on top of the others.
	const int cascade = static_cast< int >(m_watches.size()) * 32;
	watch->window->move(watch->window->pos() - QPoint(cascade, cascade));
	connect(watch->window, &WebcamWindow::closedByUser, this, [this, session] { endWatch(session, true); });
	watch->window->show();

	watch->answerTimeout = new QTimer(this);
	watch->answerTimeout->setSingleShot(true);
	connect(watch->answerTimeout, &QTimer::timeout, this, [this, session] {
		Watch *w = m_watches.value(session);
		if (w && !w->granted) {
			endWatch(session, false,
					 tr("%1 did not answer your request to watch their camera.").arg(userName(session)));
		}
	});
	m_watches.insert(session, watch);
	requestWatch(session);
}

void WebcamManager::requestWatch(unsigned int session) {
	Watch *watch = m_watches.value(session);
	if (!watch) {
		return;
	}
	std::optional< KeyPair > keys = x25519KeyPair();
	const std::string nonce       = randomNonce();
	if (!keys || nonce.empty()) {
		endWatch(session, false, tr("Watching cameras is unavailable: no keys could be created."));
		return;
	}
	if (watch->retry) {
		watch->retry->stop();
	}
	discardStream(watch->stream);
	watch->keys.privateKey.assign(watch->keys.privateKey.size(), 0);
	watch->secrets.key.assign(watch->secrets.key.size(), 0);
	watch->nonce    = nonce;
	watch->keys     = *keys;
	watch->granted  = false;
	watch->secrets  = {};
	watch->redeemed = false;
	watch->attempts = 0;
	watch->gateRefusedSince.invalidate();
	if (watch->window) {
		const ClientUser *user = ClientUser::get(session);
		watch->window->setStatus(tr("Asking %1…").arg(user ? user->qsName : QString()));
	}
	watch->answerTimeout->start(ANSWER_TIMEOUT_MS);
	send({ session }, ID_WATCH_REQUEST, encodeWatchRequest({ nonce, keys->publicKey }));
}

void WebcamManager::onGrant(unsigned int sender, const std::string &data) {
	Watch *watch = m_watches.value(sender);
	if (!watch || watch->granted) {
		return;
	}
	std::optional< StreamSecrets > secrets = decodeWatchGrant(data, watch->nonce, watch->keys);
	if (!secrets) {
		return;
	}
	if (secrets->codec != CODEC_H264 || secrets->enc != ENC_NAL_V1 || !pickUrl(secrets->urls)) {
		endWatch(sender, true,
				 tr("%1's camera uses a format this version of Mumble cannot show.").arg(userName(sender)));
		return;
	}
	watch->granted = true;
	watch->answerTimeout->stop();
	watch->secrets = *secrets;
	connectWatch(sender);
}

void WebcamManager::onDeny(unsigned int sender, const std::string &data) {
	Watch *watch = m_watches.value(sender);
	if (!watch || watch->granted) {
		return;
	}
	const std::optional< WatchDeny > deny = decodeWatchDeny(data);
	if (!deny || deny->nonce != watch->nonce) {
		return;
	}
	QString message;
	if (deny->reason == DENY_DECLINED) {
		message = tr("%1 declined your request to watch their camera.");
	} else if (deny->reason == DENY_NOT_SHARING) {
		message = tr("%1 is not sharing their camera.");
	} else if (deny->reason == DENY_POLICY) {
		message = tr("%1 does not let anyone watch their camera.");
	} else {
		message = tr("%1's client cannot share its camera with you.");
	}
	endWatch(sender, false, message.arg(userName(sender)));
}

void WebcamManager::onRekey(unsigned int sender, const std::string &data) {
	Watch *watch = m_watches.value(sender);
	if (!watch || !watch->granted) {
		return;
	}
	const std::optional< StreamSecrets > secrets = decodeWatchRekey(data, watch->keys);
	if (!secrets) {
		return;
	}
	watch->secrets.kid = secrets->kid;
	watch->secrets.key = secrets->key;
	if (secrets->gate) {
		watch->secrets.gate = secrets->gate;
		watch->redeemed     = false;
		watch->gateRefusedSince.invalidate();
	}
	if (secrets->urls.empty()) {
		// A key-only rotation, or a new ticket for the same path: the stream carries on.
		if (watch->stream) {
			watch->stream->addKey(secrets->kid, secrets->key);
		}
		if (secrets->gate) {
			redeemWatch(sender, !watch->stream);
		}
		return;
	}
	if (!pickUrl(secrets->urls)) {
		endWatch(sender, true);
		return;
	}
	watch->secrets.urls = secrets->urls;
	watch->secrets.user = secrets->user;
	watch->secrets.pass = secrets->pass;
	watch->attempts     = 0;
	connectWatch(sender);
}

void WebcamManager::onShareState(unsigned int sender, const std::string &data) {
	const std::optional< ShareState > state = decodeShareState(data);
	if (!state) {
		return;
	}
	if (ClientUser *user = ClientUser::get(sender)) {
		user->setWebcamSharing(state->sharing);
	}
	if (!state->sharing && m_watches.contains(sender)) {
		endWatch(sender, false, tr("%1 stopped sharing their camera.").arg(userName(sender)));
	}
}

void WebcamManager::connectWatch(unsigned int session) {
	Watch *watch = m_watches.value(session);
	if (!watch) {
		return;
	}
	if (watch->retry) {
		watch->retry->stop();
	}
	// Our own login goes only to our own relay; a grant naming another one ends the view.
	const std::string url              = withDefaultPort(*pickUrl(watch->secrets.urls));
	const std::optional< Login > login = readLogin(watch->secrets, url, ownLogin(), relayBase().toStdString());
	if (!login) {
		endWatch(session, true,
				 tr("%1's camera is on a relay you have no login for: watching needs your own login on the same "
					"relay, under Settings, Camera.")
					 .arg(userName(session)));
		return;
	}
	discardStream(watch->stream);
	// Every connection is redeemed first (our IP may have changed), at our own relay's gate, so
	// only for a stream there.
	const bool ownRelay = relayAuthority(url) == relayAuthority(relayBase().toStdString());
	if (watch->secrets.gate && ownRelay && !watch->redeemed) {
		redeemWatch(session, true);
		return;
	}
	watch->redeemed = false;
	watch->stream   = std::make_unique< WebcamViewerStream >();
	watch->stream->addKey(watch->secrets.kid, watch->secrets.key);

	WebcamWindow *window = watch->window;
	connect(watch->stream.get(), &WebcamViewerStream::frame, window, &WebcamWindow::setFrame);
	connect(watch->stream.get(), &WebcamPipeline::live, this, [this, session] {
		if (Watch *w = m_watches.value(session)) {
			w->attempts = 0;
		}
	});
	connect(watch->stream.get(), &WebcamPipeline::failed, this, [this, session](const QString &message) {
		Watch *w = m_watches.value(session);
		if (w && w->stream && w->stream->wasRefused() && w->secrets.gate) {
			relayRefused(session, message);
		} else {
			scheduleRetry(session, message);
		}
	});
	connect(watch->stream.get(), &WebcamPipeline::ended, this,
			[this, session] { scheduleRetry(session, tr("The stream paused.")); });

	if (window) {
		window->setStatus(tr("Connecting…"));
	}
	WebcamViewerStream::Config config;
	config.url       = QString::fromStdString(url);
	config.user      = QString::fromStdString(login->user);
	config.password  = QString::fromStdString(login->pass);
	config.caFile    = Global::get().s.qsWebcamCaFile;
	config.latencyMs = Global::get().s.iWebcamViewLatency;
	QString error;
	if (!watch->stream->start(config, &error)) {
		endWatch(session, true, tr("The camera could not be shown: %1").arg(error.toHtmlEscaped()));
	}
}

void WebcamManager::redeemWatch(unsigned int session, bool reconnect) {
	Watch *watch = m_watches.value(session);
	if (!watch || !watch->secrets.gate || watch->redeeming) {
		return;
	}
	if (relayAuthority(*pickUrl(watch->secrets.urls)) != relayAuthority(relayBase().toStdString())) {
		// Our gate is our own relay's: a ticket for another relay means nothing there.
		return;
	}
	watch->redeeming        = true;
	const GateTicket ticket = *watch->secrets.gate;
	m_gate->redeem(ticket, [this, session, ticket, reconnect](const WebcamGate::Reply &reply) {
		Watch *w = m_watches.value(session);
		if (!w) {
			return;
		}
		w->redeeming = false;
		if (!w->secrets.gate || !(*w->secrets.gate == ticket)) {
			// A newer ticket came meanwhile.
			redeemWatch(session, reconnect || !w->stream);
			return;
		}
		if (gateRetries(reply.answer)) {
			// The sender may not have claimed the ticket's path yet (a move, a gate restart), or
			// the gate is busy: keep trying for a while.
			if (!w->gateRefusedSince.isValid()) {
				w->gateRefusedSince.start();
			}
			if (w->gateRefusedSince.elapsed() > GATE_RETRY_WINDOW_MS) {
				endWatch(session, true,
						 tr("%1's camera could not be reached: the relay refused the ticket.").arg(userName(session)));
				return;
			}
			if (reconnect && w->window) {
				w->window->setStatus(tr("Waiting for the camera…"));
			}
			armRetry(session, reconnect);
			return;
		}
		w->gateRefusedSince.invalidate();
		switch (reply.answer) {
			case GateAnswer::Ok:
			case GateAnswer::Created:
				w->redeemed = true;
				if (reconnect) {
					connectWatch(session);
				}
				break;
			case GateAnswer::NoGate:
				// No gate answered (a v1 relay, or it timed out): read as in v1, without a ticket.
				if (reconnect) {
					w->redeemed = true;
					connectWatch(session);
				}
				break;
			case GateAnswer::Expired:
				// Too late for the first redeem: ask again from the start.
				requestWatch(session);
				break;
			case GateAnswer::BadLogin:
				askLoginAndRetry(session, reconnect);
				break;
			case GateAnswer::Forbidden:
			case GateAnswer::Unknown:
			case GateAnswer::Limited:
				break;
		}
	});
}

void WebcamManager::relayRefused(unsigned int session, const QString &message) {
	// MediaMTX refused the read after a good redeem: either the login is wrong, or our IP has no
	// ticket binding (it changed). open tells them apart.
	m_gate->open([this, session, message](const WebcamGate::Reply &reply) {
		if (!m_watches.contains(session)) {
			return;
		}
		if (reply.answer == GateAnswer::BadLogin) {
			askLoginAndRetry(session, true);
		} else {
			scheduleRetry(session, message);
		}
	});
}

void WebcamManager::askLoginAndRetry(unsigned int session, bool reconnect) {
	if (!askRelayLogin(true)) {
		endWatch(session, true, tr("The camera relay refused your login. Check it under Settings, Camera."));
		return;
	}
	// The dialog ran the event loop: the view may be gone.
	if (m_watches.contains(session)) {
		reconnect ? connectWatch(session) : redeemWatch(session, false);
	}
}

void WebcamManager::armRetry(unsigned int session, bool reconnect) {
	Watch *watch = m_watches.value(session);
	if (!watch) {
		return;
	}
	if (!reconnect) {
		// The stream carries on meanwhile; only the redeem is tried again.
		QTimer::singleShot(RETRY_INTERVAL_MS, this, [this, session] { redeemWatch(session, false); });
		return;
	}
	if (!watch->retry) {
		watch->retry = new QTimer(this);
		watch->retry->setSingleShot(true);
		connect(watch->retry, &QTimer::timeout, this, [this, session] { connectWatch(session); });
	}
	watch->retry->start(RETRY_INTERVAL_MS);
}

void WebcamManager::scheduleRetry(unsigned int session, const QString &status) {
	Watch *watch = m_watches.value(session);
	if (!watch) {
		return;
	}
	if (++watch->attempts > RETRY_ATTEMPTS) {
		endWatch(session, true,
				 tr("%1's camera could not be reached: %2").arg(userName(session), status.toHtmlEscaped()));
		return;
	}
	if (watch->window) {
		watch->window->setStatus(tr("Waiting for the camera…"));
	}
	armRetry(session, true);
}

void WebcamManager::endWatch(unsigned int session, bool notify, const QString &message) {
	Watch *watch = m_watches.take(session);
	if (!watch) {
		return;
	}
	if (notify) {
		send({ session }, ID_WATCH_LEAVE, encodeEmpty());
	}
	discardStream(watch->stream);
	delete watch->answerTimeout;
	delete watch->retry;
	if (watch->window) {
		Global::get().s.qbaWebcamWindowGeometry = watch->window->saveGeometry();
		watch->window->disconnect(this);
		watch->window->close();
		watch->window->deleteLater();
	}
	// The keys, path and credentials go with it.
	watch->keys.privateKey.assign(watch->keys.privateKey.size(), 0);
	watch->secrets.key.assign(watch->secrets.key.size(), 0);
	delete watch;

	if (!message.isEmpty()) {
		logInfo(message);
	}
}
