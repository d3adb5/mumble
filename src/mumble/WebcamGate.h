// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_WEBCAMGATE_H_
#define MUMBLE_MUMBLE_WEBCAMGATE_H_

#include "WebcamGateApi.h"

#include <QtCore/QJsonObject>
#include <QtCore/QObject>

#include <functional>
#include <optional>
#include <string>

class QNetworkAccessManager;

/// Calls the relay's gate API with the user's own relay login, at the address the user's own
/// settings give (see Mumble::Webcam::gateBase). Nothing it handles (paths, tickets, logins) is
/// ever logged.
class WebcamGate : public QObject {
private:
	Q_OBJECT
	Q_DISABLE_COPY(WebcamGate)

public:
	struct Reply {
		Mumble::Webcam::GateAnswer answer = Mumble::Webcam::GateAnswer::NoGate;
		QJsonObject body;
	};
	using Callback = std::function< void(const Reply &) >;

	explicit WebcamGate(QObject *parent = nullptr);

	/// Whether the settings give a gate address at all.
	static bool configured();

	/// PUT /shares/<path>: claims or renews the share.
	void putShare(const std::string &path, Callback callback);
	/// DELETE /shares/<path>: ends the share.
	void deleteShare(const std::string &path);
	/// POST /shares/<path>/tickets: a ticket for one viewer, in the reply's id and secret.
	void createTicket(const std::string &path, Callback callback);
	/// DELETE /shares/<path>/tickets/<id>: revokes one viewer.
	void deleteTicket(const std::string &path, const std::string &id);
	/// POST /open: opens our IP; tells a wrong login (BadLogin) from a missing ticket binding.
	void open(Callback callback);
	/// POST /redeem: binds a ticket to our login and IP.
	void redeem(const Mumble::Webcam::GateTicket &ticket, Callback callback);

	/// The ticket in a createTicket reply, if it has one.
	static std::optional< Mumble::Webcam::GateTicket > ticketFrom(const Reply &reply);

private:
	void call(const QByteArray &verb, const QString &path, const QJsonObject &body, Callback callback);

	QNetworkAccessManager *m_network;
};

#endif
