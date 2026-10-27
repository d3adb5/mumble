// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore>
#include <QtTest>

#include "WebcamProtocol.h"

using namespace Mumble::Webcam;

namespace {
StreamSecrets sampleSecrets() {
	StreamSecrets secrets;
	secrets.urls = { "rtsps://192.168.1.237:18322/mcam/0123456789abcdef0123456789abcdef",
					 "https://192.168.1.237:8889/mcam/0123456789abcdef0123456789abcdef/whep" };
	secrets.user = "reader";
	secrets.pass = "s3cret";
	secrets.kid  = 200;
	secrets.key  = randomBytes(KEY_SIZE);
	return secrets;
}
} // namespace

/// Unit tests for webcam sharing's signalling messages (protocol v1).
class TestWebcamProtocol : public QObject {
	Q_OBJECT
private slots:
	void base64_data();
	void base64();
	void base64Malformed();
	void dataIds();
	void shareState();
	void versionAndJunkIgnored();
	void watchRequest();
	void watchRequestWithoutKey();
	void watchDenyAndEmpty();
	void grantRoundTrip();
	void grantHidesSecrets();
	void grantWrongNonceOrViewer();
	void grantTamperedNonceInAad();
	void grantFitsMurmurLimit();
	void rekeyKeyOnlyAndMove();
	void rekeyIsNotAGrant();
	void pickUrl();
	void relayAuthority();
	void readLogin();
};

void TestWebcamProtocol::base64_data() {
	QTest::addColumn< QByteArray >("raw");
	QTest::newRow("empty") << QByteArray();
	QTest::newRow("one") << QByteArray("f");
	QTest::newRow("two") << QByteArray("fo");
	QTest::newRow("three") << QByteArray("foo");
	QTest::newRow("binary") << QByteArray::fromHex("00ff10fe2080");
}

void TestWebcamProtocol::base64() {
	QFETCH(QByteArray, raw);
	const Bytes bytes(raw.begin(), raw.end());
	const std::string encoded = toBase64(bytes);
	QCOMPARE(QString::fromStdString(encoded), QString::fromLatin1(raw.toBase64()));
	const std::optional< Bytes > decoded = fromBase64(encoded);
	QVERIFY(decoded);
	QCOMPARE(*decoded, bytes);
}

void TestWebcamProtocol::base64Malformed() {
	QVERIFY(!fromBase64("abc"));
	QVERIFY(!fromBase64("ab!d"));
	QVERIFY(!fromBase64("a=bc"));
	QVERIFY(!fromBase64("ab=c"));
}

void TestWebcamProtocol::dataIds() {
	QVERIFY(isWebcamDataId(ID_WATCH_GRANT));
	QVERIFY(isWebcamDataId("webcam/future-thing"));
	QVERIFY(!isWebcamDataId("webcamx/grant"));
	QVERIFY(!isWebcamDataId("some-plugin"));
}

void TestWebcamProtocol::shareState() {
	const std::string on                    = encodeShareState({ true, 640, 480, 24 });
	const std::optional< ShareState > state = decodeShareState(on);
	QVERIFY(state);
	QVERIFY(state->sharing);
	QCOMPARE(state->width, 640);
	QCOMPARE(state->height, 480);
	QCOMPARE(state->fps, 24);

	const std::optional< ShareState > off = decodeShareState(encodeShareState({ false, 0, 0, 0 }));
	QVERIFY(off && !off->sharing);

	// Unknown fields are ignored.
	QVERIFY(decodeShareState(R"({"v":1,"sharing":true,"w":1,"h":1,"fps":1,"later":"x"})"));
}

void TestWebcamProtocol::versionAndJunkIgnored() {
	QVERIFY(!decodeShareState(R"({"v":2,"sharing":true})"));
	QVERIFY(!decodeShareState(R"({"sharing":true})"));
	QVERIFY(!decodeShareState(R"({"v":"1","sharing":true})"));
	QVERIFY(!decodeShareState("not json"));
	QVERIFY(!decodeShareState("[1,2]"));
	QVERIFY(!decodeShareState(std::string(MAX_MESSAGE_SIZE + 1, ' ')));
	QVERIFY(!decodeEmpty(R"({"v":2})"));
}

void TestWebcamProtocol::watchRequest() {
	const std::optional< KeyPair > viewer = x25519KeyPair();
	QVERIFY(viewer);
	const std::string nonce = randomNonce();
	QVERIFY(isNonce(nonce));

	const std::optional< WatchRequest > request = decodeWatchRequest(encodeWatchRequest({ nonce, viewer->publicKey }));
	QVERIFY(request);
	QCOMPARE(request->nonce, nonce);
	QCOMPARE(request->publicKey, viewer->publicKey);

	QVERIFY(!decodeWatchRequest(R"({"v":1,"nonce":"short"})"));
	QVERIFY(!decodeWatchRequest(R"({"v":1,"nonce":"0123456789ABCDEF"})"));
}

void TestWebcamProtocol::watchRequestWithoutKey() {
	// A request without (or with a malformed) pk still parses, so the sender can deny it as
	// unsupported instead of ignoring it.
	std::optional< WatchRequest > request = decodeWatchRequest(R"({"v":1,"nonce":"0123456789abcdef"})");
	QVERIFY(request && request->publicKey.empty());
	request = decodeWatchRequest(R"({"v":1,"nonce":"0123456789abcdef","pk":"AAAA"})");
	QVERIFY(request && request->publicKey.empty());
}

void TestWebcamProtocol::watchDenyAndEmpty() {
	const std::optional< WatchDeny > deny = decodeWatchDeny(encodeWatchDeny({ "0123456789abcdef", DENY_POLICY }));
	QVERIFY(deny);
	QCOMPARE(deny->nonce, std::string("0123456789abcdef"));
	QCOMPARE(deny->reason, std::string(DENY_POLICY));

	QVERIFY(decodeEmpty(encodeEmpty()));
	QCOMPARE(encodeEmpty(), std::string(R"({"v":1})"));
}

void TestWebcamProtocol::grantRoundTrip() {
	const std::optional< KeyPair > viewer = x25519KeyPair();
	QVERIFY(viewer);
	const std::string nonce     = randomNonce();
	const StreamSecrets secrets = sampleSecrets();

	const std::optional< std::string > grant = encodeWatchGrant(nonce, viewer->publicKey, secrets);
	QVERIFY(grant);
	const std::optional< StreamSecrets > opened = decodeWatchGrant(*grant, nonce, *viewer);
	QVERIFY(opened);
	QCOMPARE(opened->urls, secrets.urls);
	QCOMPARE(opened->user, secrets.user);
	QCOMPARE(opened->pass, secrets.pass);
	QCOMPARE(opened->codec, std::string(CODEC_H264));
	QCOMPARE(opened->enc, std::string(ENC_NAL_V1));
	QCOMPARE(opened->kid, secrets.kid);
	QCOMPARE(opened->key, secrets.key);
}

void TestWebcamProtocol::grantHidesSecrets() {
	// What Murmur relays holds none of the secrets in the clear.
	const std::optional< KeyPair > viewer = x25519KeyPair();
	QVERIFY(viewer);
	const StreamSecrets secrets              = sampleSecrets();
	const std::optional< std::string > grant = encodeWatchGrant(randomNonce(), viewer->publicKey, secrets);
	QVERIFY(grant);
	QVERIFY(grant->find("mcam/") == std::string::npos);
	QVERIFY(grant->find("rtsps") == std::string::npos);
	QVERIFY(grant->find("s3cret") == std::string::npos);
	QVERIFY(grant->find(toBase64(secrets.key)) == std::string::npos);

	const nlohmann::json json = nlohmann::json::parse(*grant);
	QCOMPARE(json.size(), std::size_t(4));
	QVERIFY(json.contains("v") && json.contains("nonce") && json.contains("epk") && json.contains("box"));
}

void TestWebcamProtocol::grantWrongNonceOrViewer() {
	const std::optional< KeyPair > viewer = x25519KeyPair();
	const std::optional< KeyPair > other  = x25519KeyPair();
	QVERIFY(viewer && other);
	const std::string nonce                  = randomNonce();
	const std::optional< std::string > grant = encodeWatchGrant(nonce, viewer->publicKey, sampleSecrets());
	QVERIFY(grant);

	QVERIFY(!decodeWatchGrant(*grant, randomNonce(), *viewer));
	QVERIFY(!decodeWatchGrant(*grant, nonce, *other));
}

void TestWebcamProtocol::grantTamperedNonceInAad() {
	// A relay that rewrites the nonce field to steer a grant onto another request is caught,
	// because the nonce is part of the box's AAD.
	const std::optional< KeyPair > viewer = x25519KeyPair();
	QVERIFY(viewer);
	const std::string nonce                  = "0011223344556677";
	const std::optional< std::string > grant = encodeWatchGrant(nonce, viewer->publicKey, sampleSecrets());
	QVERIFY(grant);

	nlohmann::json json = nlohmann::json::parse(*grant);
	json["nonce"]       = "8899aabbccddeeff";
	QVERIFY(!decodeWatchGrant(json.dump(), "8899aabbccddeeff", *viewer));
}

void TestWebcamProtocol::grantFitsMurmurLimit() {
	const std::optional< KeyPair > viewer = x25519KeyPair();
	QVERIFY(viewer);
	const std::optional< std::string > grant = encodeWatchGrant(randomNonce(), viewer->publicKey, sampleSecrets());
	QVERIFY(grant);
	QVERIFY(grant->size() <= MAX_MESSAGE_SIZE);

	// Secrets too long for one message are refused rather than sent and dropped by Murmur.
	StreamSecrets huge = sampleSecrets();
	huge.urls.push_back(std::string(900, 'x'));
	QVERIFY(!encodeWatchGrant(randomNonce(), viewer->publicKey, huge));
}

void TestWebcamProtocol::rekeyKeyOnlyAndMove() {
	const std::optional< KeyPair > viewer = x25519KeyPair();
	QVERIFY(viewer);

	StreamSecrets keyOnly;
	keyOnly.kid                        = 4;
	keyOnly.key                        = randomBytes(KEY_SIZE);
	std::optional< std::string > rekey = encodeWatchRekey(viewer->publicKey, keyOnly);
	QVERIFY(rekey);
	std::optional< StreamSecrets > opened = decodeWatchRekey(*rekey, *viewer);
	QVERIFY(opened);
	QVERIFY(opened->urls.empty());
	QCOMPARE(opened->kid, std::uint8_t(4));
	QCOMPARE(opened->key, keyOnly.key);

	const StreamSecrets move = sampleSecrets();
	rekey                    = encodeWatchRekey(viewer->publicKey, move);
	QVERIFY(rekey);
	opened = decodeWatchRekey(*rekey, *viewer);
	QVERIFY(opened);
	QCOMPARE(opened->urls, move.urls);
	QCOMPARE(opened->user, move.user);
}

void TestWebcamProtocol::rekeyIsNotAGrant() {
	// The data id is in the AAD, so a rekey replayed as a grant (or the other way) is refused.
	const std::optional< KeyPair > viewer = x25519KeyPair();
	QVERIFY(viewer);
	const std::optional< std::string > rekey = encodeWatchRekey(viewer->publicKey, sampleSecrets());
	QVERIFY(rekey);
	nlohmann::json json = nlohmann::json::parse(*rekey);
	json["nonce"]       = "0011223344556677";
	QVERIFY(!decodeWatchGrant(json.dump(), "0011223344556677", *viewer));

	const std::optional< std::string > grant = encodeWatchGrant("0011223344556677", viewer->publicKey, sampleSecrets());
	QVERIFY(grant);
	QVERIFY(!decodeWatchRekey(*grant, *viewer));
}

void TestWebcamProtocol::pickUrl() {
	QCOMPARE(Mumble::Webcam::pickUrl({ "https://h/whep", "rtsps://h:1/mcam/x" }),
			 std::optional< std::string >("rtsps://h:1/mcam/x"));
	QVERIFY(!Mumble::Webcam::pickUrl({ "https://h/whep", "rtsp://h/plain" }));
	QVERIFY(!Mumble::Webcam::pickUrl({}));
}

void TestWebcamProtocol::relayAuthority() {
	using Mumble::Webcam::relayAuthority;
	QCOMPARE(relayAuthority("rtsps://Relay.Example.org:8322/mcam/x"),
			 std::optional< std::string >("relay.example.org:8322"));
	QCOMPARE(relayAuthority("rtsps://relay.example.org"), std::optional< std::string >("relay.example.org:322"));
	QCOMPARE(relayAuthority("rtsps://relay.example.org:/x"), std::optional< std::string >("relay.example.org:322"));
	QCOMPARE(relayAuthority("rtsps://u:p@relay.example.org:8322"),
			 std::optional< std::string >("relay.example.org:8322"));
	QCOMPARE(relayAuthority("rtsps://[::1]:18322/x"), std::optional< std::string >("[::1]:18322"));
	QCOMPARE(relayAuthority("rtsps://[::1]/x"), std::optional< std::string >("[::1]:322"));
	// A user part cannot pass off another host as the relay.
	QCOMPARE(relayAuthority("rtsps://relay.example.org:8322@evil.example:8322/x"),
			 std::optional< std::string >("evil.example:8322"));
	QVERIFY(!relayAuthority("rtsp://relay.example.org:8322/x"));
	QVERIFY(!relayAuthority("rtsps://"));
	QVERIFY(!relayAuthority("rtsps:///x"));
	QVERIFY(!relayAuthority("rtsps://:8322/x"));
	QVERIFY(!relayAuthority(""));
}

void TestWebcamProtocol::readLogin() {
	using Mumble::Webcam::Login;
	const Login own{ "viewer", "own-pass" };
	const std::string relay = "rtsps://relay.example.org:8322/";
	const std::string url   = "rtsps://relay.example.org:8322/mcam/0123";
	StreamSecrets secrets   = sampleSecrets();

	secrets.user = "sender-read";
	secrets.pass = "read-pass";
	QCOMPARE(Mumble::Webcam::readLogin(secrets, url, own, relay),
			 std::optional< Login >(Login{ "sender-read", "read-pass" }));

	secrets.user.clear();
	secrets.pass.clear();
	QCOMPARE(Mumble::Webcam::readLogin(secrets, url, own, relay), std::optional< Login >(own));
	QVERIFY(!Mumble::Webcam::readLogin(secrets, url, Login{}, relay));
	QVERIFY(!Mumble::Webcam::readLogin(secrets, url, Login{ "", "pass-without-user" }, relay));
	// The own login never goes to another relay, nor anywhere when no relay is set.
	QVERIFY(!Mumble::Webcam::readLogin(secrets, "rtsps://evil.example:8322/mcam/0123", own, relay));
	QVERIFY(!Mumble::Webcam::readLogin(secrets, "rtsps://relay.example.org:9322/mcam/0123", own, relay));
	QVERIFY(!Mumble::Webcam::readLogin(secrets, url, own, ""));
}

QTEST_MAIN(TestWebcamProtocol)
#include "TestWebcamProtocol.moc"
