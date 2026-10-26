// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_WEBCAM_H_
#define MUMBLE_MUMBLE_WEBCAM_H_

#include "WebcamMode.h"
#include "WebcamProtocol.h"

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>

#include <memory>
#include <string>

class QMessageBox;
class QTimer;
class WebcamPublisher;
class WebcamViewerStream;
class WebcamWindow;

/// Webcam sharing (protocol v1, shared with Mumla): this client's camera share and the cameras
/// it watches. Signalling goes over PluginDataTransmission to single sessions; the video goes
/// through a MediaMTX relay on a random path, encrypted end to end, and every secret reaches a
/// viewer only sealed to that viewer's key.
class WebcamManager : public QObject {
private:
	Q_OBJECT
	Q_DISABLE_COPY(WebcamManager)

public:
	explicit WebcamManager(QObject *parent = nullptr);
	~WebcamManager() override;

	// -- sharing -----------------------------------------------------------------------------
	bool isSharing() const { return m_sharing; }
	/// Announces the share, after checking the relay is configured. False (with a message
	/// logged) if it cannot start.
	bool startSharing();
	void stopSharing(const QString &reason = QString());
	bool isWatchedBy(unsigned int session) const { return m_viewers.contains(session); }
	/// Stops letting \p session watch: the stream moves to a fresh path and key, which only the
	/// remaining viewers learn.
	void revoke(unsigned int session);

	// -- watching ----------------------------------------------------------------------------
	bool isWatching(unsigned int session) const { return m_watches.contains(session); }
	/// Asks \p session to let us watch their camera, or raises the window if it is open.
	void watch(unsigned int session);

	// -- hooks -------------------------------------------------------------------------------
	/// Plugin data with a "webcam/" data id from \p sender.
	void handleData(unsigned int sender, const std::string &dataId, const std::string &data);
	/// A user joined after our own sync: they learn about our share.
	void userJoined(unsigned int session);
	void userLeft(unsigned int session);
	void serverDisconnected();

signals:
	void sharingChanged(bool sharing);

private:
	struct Viewer {
		Mumble::Webcam::Bytes publicKey;
	};
	struct PendingAsk {
		std::string nonce;
		Mumble::Webcam::Bytes publicKey;
		QPointer< QMessageBox > box;
	};
	struct Watch {
		std::string nonce;
		Mumble::Webcam::KeyPair keys;
		bool granted = false;
		Mumble::Webcam::StreamSecrets secrets;
		QPointer< WebcamWindow > window;
		std::unique_ptr< WebcamViewerStream > stream;
		QTimer *answerTimeout = nullptr;
		QTimer *retry         = nullptr;
		int attempts          = 0;
	};

	void send(const QList< unsigned int > &sessions, const char *dataId, const std::string &data);
	void broadcastShareState();
	Mumble::Webcam::Quality wantedQuality() const;
	QString relayBase() const;
	/// Where the current stream is, a secret like the key.
	QString streamUrl() const;

	// sender side
	void onRequest(unsigned int sender, const std::string &data);
	void onLeave(unsigned int sender);
	void grant(unsigned int session, const std::string &nonce, const Mumble::Webcam::Bytes &publicKey);
	void deny(unsigned int session, const std::string &nonce, const char *reason);
	/// The stream's secrets as one viewer receives them.
	Mumble::Webcam::StreamSecrets currentSecrets() const;
	/// A fresh path and key (and the next kid); the old ones are forgotten.
	bool newStreamSecrets();
	bool ensurePublishing();
	void stopPublishing();
	/// After a viewer is gone: the remaining ones move to a fresh path, or the camera closes
	/// when nobody is left and it opens on demand.
	void viewerGone(unsigned int session, bool sendRevoke);

	// viewer side
	void onGrant(unsigned int sender, const std::string &data);
	void onDeny(unsigned int sender, const std::string &data);
	void onRekey(unsigned int sender, const std::string &data);
	void onShareState(unsigned int sender, const std::string &data);
	void connectWatch(unsigned int session);
	void scheduleRetry(unsigned int session, const QString &status);
	/// Closes the view on \p session; \p notify sends watch-leave.
	void endWatch(unsigned int session, bool notify, const QString &message = QString());

	bool m_sharing = false;
	std::string m_path;
	Mumble::Webcam::Bytes m_key;
	std::uint8_t m_kid = 0;
	std::unique_ptr< WebcamPublisher > m_publisher;
	QHash< unsigned int, Viewer > m_viewers;
	QHash< unsigned int, PendingAsk > m_asks;

	QHash< unsigned int, Watch * > m_watches;
};

#endif
