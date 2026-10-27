// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore>
#include <QtTest>

#include "WebcamGateApi.h"

using namespace Mumble::Webcam;

Q_DECLARE_METATYPE(GateAnswer)

/// Unit tests for the rules a client follows with the relay's gate (protocol v1.1).
class TestWebcamGate : public QObject {
	Q_OBJECT
private slots:
	void gateBaseFromRelay();
	void gateBaseOverride();
	void gateAnswer_data();
	void gateAnswer();
	void gateRetries();
};

void TestWebcamGate::gateBaseFromRelay() {
	using Base = std::optional< std::string >;
	// The relay's own host, on the HTTPS port, whatever the RTSPS port.
	QCOMPARE(gateBase("rtsps://Stream.Example.org:8322", ""), Base("https://stream.example.org/gate/v1"));
	QCOMPARE(gateBase("rtsps://stream.example.org", ""), Base("https://stream.example.org/gate/v1"));
	QCOMPARE(gateBase("rtsps://[::1]:18322/", ""), Base("https://[::1]/gate/v1"));
	// A user part in the relay setting is not the host.
	QCOMPARE(gateBase("rtsps://someone@stream.example.org:8322", ""), Base("https://stream.example.org/gate/v1"));
	QVERIFY(!gateBase("", ""));
	QVERIFY(!gateBase("rtsp://stream.example.org:8554", ""));
}

void TestWebcamGate::gateBaseOverride() {
	using Base = std::optional< std::string >;
	QCOMPARE(gateBase("rtsps://127.0.0.1:18322", "https://127.0.0.1:19443/gate/v1"),
			 Base("https://127.0.0.1:19443/gate/v1"));
	QCOMPARE(gateBase("rtsps://127.0.0.1:18322", "  https://127.0.0.1:19443/gate/v1// "),
			 Base("https://127.0.0.1:19443/gate/v1"));
	// The override works without a relay, but never over plain HTTP: the login goes there.
	QCOMPARE(gateBase("", "https://gate.example.org/gate/v1"), Base("https://gate.example.org/gate/v1"));
	QVERIFY(!gateBase("rtsps://127.0.0.1:18322", "http://127.0.0.1:19080/gate/v1"));
	QVERIFY(!gateBase("rtsps://127.0.0.1:18322", "https://"));
	QVERIFY(!gateBase("rtsps://127.0.0.1:18322", "127.0.0.1:19443"));
	// Blank counts as unset.
	QCOMPARE(gateBase("rtsps://127.0.0.1:18322", "   "), Base("https://127.0.0.1/gate/v1"));
}

void TestWebcamGate::gateAnswer_data() {
	QTest::addColumn< int >("status");
	QTest::addColumn< bool >("fromGate");
	QTest::addColumn< bool >("jsonBody");
	QTest::addColumn< GateAnswer >("expected");

	QTest::newRow("ok") << 200 << false << true << GateAnswer::Ok;
	QTest::newRow("no content") << 204 << false << false << GateAnswer::Ok;
	QTest::newRow("created") << 201 << false << true << GateAnswer::Created;
	QTest::newRow("bad login") << 401 << true << false << GateAnswer::BadLogin;
	QTest::newRow("forbidden") << 403 << true << false << GateAnswer::Forbidden;
	QTest::newRow("unknown") << 404 << true << false << GateAnswer::Unknown;
	QTest::newRow("expired") << 410 << true << false << GateAnswer::Expired;
	QTest::newRow("limited") << 429 << true << false << GateAnswer::Limited;
	QTest::newRow("gate bad request") << 400 << true << false << GateAnswer::NoGate;
	// A page answering 200 is not a gate: the gate's 2xx always carry JSON.
	QTest::newRow("200 page") << 200 << false << false << GateAnswer::NoGate;
	QTest::newRow("201 page") << 201 << false << false << GateAnswer::NoGate;
	// Without the gate's header, a refusal comes from something else: there is no gate.
	QTest::newRow("plain 404") << 404 << false << false << GateAnswer::NoGate;
	QTest::newRow("plain 401") << 401 << false << false << GateAnswer::NoGate;
	QTest::newRow("server error") << 502 << false << false << GateAnswer::NoGate;
	QTest::newRow("no answer (timeout)") << 0 << false << false << GateAnswer::NoGate;
}

void TestWebcamGate::gateAnswer() {
	QFETCH(int, status);
	QFETCH(bool, fromGate);
	QFETCH(bool, jsonBody);
	QFETCH(GateAnswer, expected);
	QCOMPARE(Mumble::Webcam::gateAnswer(status, fromGate, jsonBody), expected);
}

void TestWebcamGate::gateRetries() {
	QVERIFY(Mumble::Webcam::gateRetries(GateAnswer::Forbidden));
	QVERIFY(Mumble::Webcam::gateRetries(GateAnswer::Unknown));
	QVERIFY(Mumble::Webcam::gateRetries(GateAnswer::Limited));
	// A bad login asks the user, an expired ticket asks the sender again, and no gate means v1.
	for (GateAnswer answer :
		 { GateAnswer::Ok, GateAnswer::Created, GateAnswer::BadLogin, GateAnswer::Expired, GateAnswer::NoGate }) {
		QVERIFY(!Mumble::Webcam::gateRetries(answer));
	}
}

QTEST_MAIN(TestWebcamGate)
#include "TestWebcamGate.moc"
