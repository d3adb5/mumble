// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "WebcamGate.h"

#include "Global.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QSslConfiguration>

using namespace Mumble::Webcam;

namespace {
/// Long enough for a gate that makes a wrong login wait, short enough to show a stuck relay.
constexpr int TIMEOUT_MS = 10000;

std::optional< std::string > currentBase() {
	const Settings &s = Global::get().s;
	return gateBase(s.qsWebcamRelay.trimmed().toStdString(), s.qsWebcamGateUrl.toStdString());
}
} // namespace

WebcamGate::WebcamGate(QObject *parent) : QObject(parent), m_network(new QNetworkAccessManager(this)) {
}

bool WebcamGate::configured() {
	return currentBase().has_value();
}

void WebcamGate::putShare(const std::string &path, Callback callback) {
	call("PUT", QLatin1String("/shares/") + QString::fromStdString(path), {}, std::move(callback));
}

void WebcamGate::deleteShare(const std::string &path) {
	call("DELETE", QLatin1String("/shares/") + QString::fromStdString(path), {}, nullptr);
}

void WebcamGate::createTicket(const std::string &path, Callback callback) {
	call("POST", QLatin1String("/shares/") + QString::fromStdString(path) + QLatin1String("/tickets"), {},
		 std::move(callback));
}

void WebcamGate::deleteTicket(const std::string &path, const std::string &id) {
	call("DELETE",
		 QLatin1String("/shares/") + QString::fromStdString(path) + QLatin1String("/tickets/")
			 + QString::fromLatin1(QUrl::toPercentEncoding(QString::fromStdString(id))),
		 {}, nullptr);
}

void WebcamGate::open(Callback callback) {
	call("POST", QLatin1String("/open"), {}, std::move(callback));
}

void WebcamGate::redeem(const GateTicket &ticket, Callback callback) {
	QJsonObject body;
	body.insert(QLatin1String("id"), QString::fromStdString(ticket.id));
	body.insert(QLatin1String("secret"), QString::fromStdString(ticket.secret));
	call("POST", QLatin1String("/redeem"), body, std::move(callback));
}

std::optional< GateTicket > WebcamGate::ticketFrom(const Reply &reply) {
	const QString id     = reply.body.value(QLatin1String("id")).toString();
	const QString secret = reply.body.value(QLatin1String("secret")).toString();
	if (reply.answer != GateAnswer::Ok || id.isEmpty() || secret.isEmpty()) {
		return std::nullopt;
	}
	return GateTicket{ id.toStdString(), secret.toStdString() };
}

void WebcamGate::call(const QByteArray &verb, const QString &path, const QJsonObject &body, Callback callback) {
	const std::optional< std::string > base = currentBase();
	if (!base) {
		if (callback) {
			callback({});
		}
		return;
	}
	const Settings &s = Global::get().s;

	QNetworkRequest request(QUrl(QString::fromStdString(*base) + path));
	request.setTransferTimeout(TIMEOUT_MS);
	// The owner login must never follow a redirect elsewhere.
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
	request.setRawHeader(
		"Authorization",
		"Basic " + (s.qsWebcamPublishUser + QLatin1Char(':') + s.qsWebcamPublishPassword).toUtf8().toBase64());
	QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
	if (!s.qsWebcamCaFile.isEmpty()) {
		ssl.addCaCertificates(s.qsWebcamCaFile);
	}
	request.setSslConfiguration(ssl);

	QByteArray payload;
	if (!body.isEmpty()) {
		payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
		request.setHeader(QNetworkRequest::ContentTypeHeader, QLatin1String("application/json"));
	}
	QNetworkReply *reply = m_network->sendCustomRequest(request, verb, payload);
	connect(reply, &QNetworkReply::finished, this, [reply, callback] {
		reply->deleteLater();
		if (!callback) {
			return;
		}
		Reply out;
		const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
		if (status.isValid()) {
			const QJsonDocument json = QJsonDocument::fromJson(reply->readAll());
			out.answer               = gateAnswer(status.toInt(), reply->hasRawHeader("X-Mtx-Gate"), json.isObject());
			out.body                 = json.object();
		}
		callback(out);
	});
}
