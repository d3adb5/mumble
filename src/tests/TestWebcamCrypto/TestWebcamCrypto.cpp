// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore>
#include <QtTest>

#include "WebcamCrypto.h"

using namespace Mumble::Webcam;

namespace {
Bytes fromHex(const char *hex) {
	const QByteArray raw = QByteArray::fromHex(hex);
	return Bytes(raw.begin(), raw.end());
}

QByteArray hex(const Bytes &data) {
	return QByteArray(reinterpret_cast< const char * >(data.data()), static_cast< int >(data.size())).toHex();
}

Bytes lengthPrefixed(const std::vector< Bytes > &nals) {
	Bytes out;
	for (const Bytes &nal : nals) {
		const auto size = static_cast< std::uint32_t >(nal.size());
		out.push_back(static_cast< std::uint8_t >(size >> 24));
		out.push_back(static_cast< std::uint8_t >(size >> 16));
		out.push_back(static_cast< std::uint8_t >(size >> 8));
		out.push_back(static_cast< std::uint8_t >(size));
		out.insert(out.end(), nal.begin(), nal.end());
	}
	return out;
}

std::vector< Bytes > splitLengthPrefixed(const Bytes &au) {
	std::vector< Bytes > nals;
	std::size_t pos = 0;
	while (pos + 4 <= au.size()) {
		const std::size_t len = (std::size_t(au[pos]) << 24) | (std::size_t(au[pos + 1]) << 16)
								| (std::size_t(au[pos + 2]) << 8) | std::size_t(au[pos + 3]);
		pos += 4;
		nals.emplace_back(au.begin() + static_cast< long >(pos), au.begin() + static_cast< long >(pos + len));
		pos += len;
	}
	return nals;
}

/// Whether \p body holds 00 00 00, 00 00 01 or 00 00 02, which emulation prevention rules out.
bool hasStartCodePattern(const Bytes &body) {
	for (std::size_t i = 0; i + 2 < body.size(); ++i) {
		if (body[i] == 0 && body[i + 1] == 0 && body[i + 2] <= 0x02) {
			return true;
		}
	}
	return false;
}

// RFC 7748 section 6.1.
const char *ALICE_PRIVATE = "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a";
const char *ALICE_PUBLIC  = "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a";
const char *BOB_PRIVATE   = "5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb";
const char *BOB_PUBLIC    = "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f";
const char *SHARED        = "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742";

// The key wrap vector for the spec, computed independently (Python "cryptography").
const char *WRAP_VECTOR_KW  = "5ac0e7490cacc8dc40ab778794eafdbd4c34252fa8be58cb62e17752bf6c3107";
const char *WRAP_VECTOR_BOX = "000102030405060708090a0bd1d4b90f903430d61282c67c65c8fa9011846f9af0cc64133c22581a5646"
							  "071a86860b74961e2b5b1a23213a2ad5c4902b097e4c57a718e13d7c98a69d44f958e13d022db3ff9de6"
							  "81eb907306bb";
} // namespace

/// Unit tests for webcam sharing's payload encryption and key wrap (protocol v1).
class TestWebcamCrypto : public QObject {
	Q_OBJECT
private slots:
	void emulation_data();
	void emulation();
	void emulation_randomRoundTrip();
	void streamPath();
	void nalRoundTrip();
	void nalWithIvIsDeterministic();
	void nalTamperedOrWrongKid();
	void nalHeaderIsAuthenticated();
	void keyRingKeepsPrevious();
	void accessUnitDropsSeiKeepsParameterSets();
	void accessUnitFailsOnBadSlice();
	void accessUnitMalformed();
	void x25519Rfc7748();
	void x25519RejectsLowOrder();
	void hkdfRfc5869();
	void wrapRoundTrip();
	void wrapWrongAadOrViewer();
	void wrapVector();
};

void TestWebcamCrypto::emulation_data() {
	QTest::addColumn< QByteArray >("body");
	QTest::addColumn< QByteArray >("escaped");

	QTest::newRow("empty") << QByteArray("") << QByteArray("");
	QTest::newRow("plain") << QByteArray("0102") << QByteArray("0102");
	QTest::newRow("start code") << QByteArray("000001") << QByteArray("00000301");
	QTest::newRow("each low byte") << QByteArray("000000000002000003") << QByteArray("000003000003000200000303");
	QTest::newRow("00 00 04 untouched") << QByteArray("000004") << QByteArray("000004");
	QTest::newRow("trailing zeros") << QByteArray("010000") << QByteArray("010000");
	QTest::newRow("ends 00 03") << QByteArray("0100000380") << QByteArray("010000030380");
}

void TestWebcamCrypto::emulation() {
	QFETCH(QByteArray, body);
	QFETCH(QByteArray, escaped);

	const Bytes in  = fromHex(body.constData());
	const Bytes out = escapeEmulation(in);
	QCOMPARE(hex(out), escaped);
	QCOMPARE(unescapeEmulation(out.data(), out.size()), in);
}

void TestWebcamCrypto::emulation_randomRoundTrip() {
	// Zero-heavy random bodies: the escaped form never holds a start code and always comes back
	// unchanged.
	QRandomGenerator rng(42);
	for (int round = 0; round < 2000; ++round) {
		Bytes body(static_cast< std::size_t >(rng.bounded(1, 64)));
		for (std::uint8_t &byte : body) {
			byte = static_cast< std::uint8_t >(rng.bounded(4) == 0 ? rng.bounded(256) : rng.bounded(4));
		}
		const Bytes out = escapeEmulation(body);
		QVERIFY(!hasStartCodePattern(out));
		QCOMPARE(unescapeEmulation(out.data(), out.size()), body);
	}
}

void TestWebcamCrypto::streamPath() {
	const std::string a = randomStreamPath();
	const std::string b = randomStreamPath();
	QVERIFY(QRegularExpression("^mcam/[0-9a-f]{32}$").match(QString::fromStdString(a)).hasMatch());
	QVERIFY(a != b);
}

void TestWebcamCrypto::nalRoundTrip() {
	const Bytes key = randomBytes(KEY_SIZE);
	KeyRing keys;
	keys.set(7, key);

	// An IDR slice whose body is mostly zeros, so the ciphertext layout is exercised alongside.
	Bytes nal = { 0x65 };
	for (int i = 0; i < 3000; ++i) {
		nal.push_back(static_cast< std::uint8_t >(i % 5 == 0 ? i : 0));
	}
	const Bytes sealed = encryptNal(nal.data(), nal.size(), key, 7);
	QCOMPARE(sealed[0], std::uint8_t(0x65));
	QCOMPARE(sealed.size() >= nal.size() + 1 + IV_SIZE + TAG_SIZE, true);
	QVERIFY(!hasStartCodePattern(Bytes(sealed.begin() + 1, sealed.end())));
	QCOMPARE(sealed.back(), STOP_BYTE);

	const std::optional< Bytes > plain = decryptNal(sealed.data(), sealed.size(), keys);
	QVERIFY(plain);
	QCOMPARE(*plain, nal);

	// Two encryptions of the same NAL never share an IV.
	const Bytes again = encryptNal(nal.data(), nal.size(), key, 7);
	QVERIFY(again != sealed);
}

void TestWebcamCrypto::nalWithIvIsDeterministic() {
	const Bytes key = fromHex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
	const Bytes iv  = fromHex("a0a1a2a3a4a5a6a7a8a9aaab");
	const Bytes nal = fromHex("41e0000001");
	const Bytes a   = encryptNalWithIv(nal.data(), nal.size(), key, 3, iv);
	const Bytes b   = encryptNalWithIv(nal.data(), nal.size(), key, 3, iv);
	QCOMPARE(a, b);
	// The vector recorded in the spec, computed independently (Python "cryptography"): header in
	// the clear, kid, IV, ciphertext, tag, stop byte.
	QCOMPARE(hex(a), QByteArray("4103a0a1a2a3a4a5a6a7a8a9aaab06187c2c590b0895a04b1293b0e9c34d731fbee780"));
}

void TestWebcamCrypto::nalTamperedOrWrongKid() {
	const Bytes key = randomBytes(KEY_SIZE);
	const Bytes nal = fromHex("4101020304050607");
	Bytes sealed    = encryptNal(nal.data(), nal.size(), key, 1);

	KeyRing other;
	other.set(2, key);
	QVERIFY(!decryptNal(sealed.data(), sealed.size(), other));

	KeyRing wrongKey;
	wrongKey.set(1, randomBytes(KEY_SIZE));
	QVERIFY(!decryptNal(sealed.data(), sealed.size(), wrongKey));

	KeyRing keys;
	keys.set(1, key);
	sealed[sealed.size() - 5] ^= 0x01;
	QVERIFY(!decryptNal(sealed.data(), sealed.size(), keys));
}

void TestWebcamCrypto::nalHeaderIsAuthenticated() {
	const Bytes key = randomBytes(KEY_SIZE);
	const Bytes nal = fromHex("4101020304");
	Bytes sealed    = encryptNal(nal.data(), nal.size(), key, 1);
	KeyRing keys;
	keys.set(1, key);

	// A relay that rewrote the NAL type (here non-IDR to IDR) is caught.
	sealed[0] = 0x65;
	QVERIFY(!decryptNal(sealed.data(), sealed.size(), keys));
}

void TestWebcamCrypto::keyRingKeepsPrevious() {
	KeyRing keys;
	const Bytes a = randomBytes(KEY_SIZE);
	const Bytes b = randomBytes(KEY_SIZE);
	const Bytes c = randomBytes(KEY_SIZE);
	keys.set(1, a);
	keys.set(2, b);
	QVERIFY(keys.find(1) && *keys.find(1) == a);
	QVERIFY(keys.find(2) && *keys.find(2) == b);
	keys.set(3, c);
	QVERIFY(!keys.find(1));
	QVERIFY(keys.find(2) && keys.find(3));

	// Setting the current kid again replaces its key without dropping the previous one.
	keys.set(3, a);
	QVERIFY(keys.find(2) && *keys.find(3) == a);

	keys.clear();
	QVERIFY(!keys.find(2) && !keys.find(3));
}

void TestWebcamCrypto::accessUnitDropsSeiKeepsParameterSets() {
	const Bytes key = randomBytes(KEY_SIZE);
	const Bytes aud = fromHex("0910");
	const Bytes sps = fromHex("6742c01e8c8d40");
	const Bytes pps = fromHex("68ce3c80");
	const Bytes sei = fromHex("0605ff78323634");
	const Bytes idr = fromHex("6588840000000100ff");
	const Bytes au  = lengthPrefixed({ aud, sps, pps, sei, idr });

	const std::optional< Bytes > sealed = encryptAccessUnit(au.data(), au.size(), key, 9);
	QVERIFY(sealed);
	const std::vector< Bytes > nals = splitLengthPrefixed(*sealed);
	QCOMPARE(nals.size(), std::size_t(4));
	QCOMPARE(nals[0], aud);
	QCOMPARE(nals[1], sps);
	QCOMPARE(nals[2], pps);
	QCOMPARE(nals[3][0], std::uint8_t(0x65));
	QVERIFY(nals[3] != idr);
	QVERIFY(containsIdr(sealed->data(), sealed->size()));

	KeyRing keys;
	keys.set(9, key);
	const std::optional< Bytes > plain = decryptAccessUnit(sealed->data(), sealed->size(), keys);
	QVERIFY(plain);
	QCOMPARE(*plain, lengthPrefixed({ aud, sps, pps, idr }));
}

void TestWebcamCrypto::accessUnitFailsOnBadSlice() {
	const Bytes key                     = randomBytes(KEY_SIZE);
	const Bytes au                      = lengthPrefixed({ fromHex("6742c01e"), fromHex("41aabbcc") });
	const std::optional< Bytes > sealed = encryptAccessUnit(au.data(), au.size(), key, 1);
	QVERIFY(sealed);

	// A viewer without the key gets nothing, never the ciphertext.
	KeyRing none;
	QVERIFY(!decryptAccessUnit(sealed->data(), sealed->size(), none));
	QVERIFY(!containsIdr(sealed->data(), sealed->size()));
}

void TestWebcamCrypto::accessUnitMalformed() {
	const Bytes key = randomBytes(KEY_SIZE);
	// A length that runs past the end.
	const Bytes truncated = fromHex("0000000a4101");
	QVERIFY(!encryptAccessUnit(truncated.data(), truncated.size(), key, 1));
	KeyRing keys;
	keys.set(1, key);
	QVERIFY(!decryptAccessUnit(truncated.data(), truncated.size(), keys));
}

void TestWebcamCrypto::x25519Rfc7748() {
	const std::optional< KeyPair > alice = x25519KeyPair(fromHex(ALICE_PRIVATE));
	const std::optional< KeyPair > bob   = x25519KeyPair(fromHex(BOB_PRIVATE));
	QVERIFY(alice && bob);
	QCOMPARE(hex(alice->publicKey), QByteArray(ALICE_PUBLIC));
	QCOMPARE(hex(bob->publicKey), QByteArray(BOB_PUBLIC));

	const std::optional< Bytes > ab = x25519(alice->privateKey, bob->publicKey);
	const std::optional< Bytes > ba = x25519(bob->privateKey, alice->publicKey);
	QVERIFY(ab && ba);
	QCOMPARE(hex(*ab), QByteArray(SHARED));
	QCOMPARE(hex(*ba), QByteArray(SHARED));

	const std::optional< KeyPair > fresh = x25519KeyPair();
	QVERIFY(fresh);
	QCOMPARE(fresh->publicKey.size(), X25519_SIZE);
	QVERIFY(fresh->privateKey != alice->privateKey);
}

void TestWebcamCrypto::x25519RejectsLowOrder() {
	// The all-zero point (low order) makes the shared secret zero, which must be refused.
	const Bytes zero(X25519_SIZE, 0);
	QVERIFY(!x25519(fromHex(ALICE_PRIVATE), zero));
	QVERIFY(!sealToViewer(zero, "{}", "webcam/watch-grant"));
}

void TestWebcamCrypto::hkdfRfc5869() {
	// RFC 5869 test case 1.
	const Bytes ikm       = fromHex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
	const Bytes salt      = fromHex("000102030405060708090a0b0c");
	const QByteArray info = QByteArray::fromHex("f0f1f2f3f4f5f6f7f8f9");
	const Bytes okm       = hkdfSha256(ikm, salt, info.toStdString(), 42);
	QCOMPARE(hex(okm), QByteArray("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
								  "34007208d5b887185865"));
}

void TestWebcamCrypto::wrapRoundTrip() {
	const std::optional< KeyPair > viewer = x25519KeyPair();
	QVERIFY(viewer);
	const std::string secret = R"({"kid":1,"key":"...","urls":["rtsps://h:8322/mcam/00"]})";
	const std::string aad    = "webcam/watch-grant0123456789abcdef";

	const std::optional< WrappedBox > wrapped = sealToViewer(viewer->publicKey, secret, aad);
	QVERIFY(wrapped);
	QCOMPARE(wrapped->epk.size(), X25519_SIZE);
	QCOMPARE(wrapped->box.size(), IV_SIZE + secret.size() + TAG_SIZE);

	const std::optional< std::string > opened = openFromSender(*viewer, *wrapped, aad);
	QVERIFY(opened);
	QCOMPARE(*opened, secret);

	// Every grant uses a fresh ephemeral key.
	const std::optional< WrappedBox > second = sealToViewer(viewer->publicKey, secret, aad);
	QVERIFY(second && second->epk != wrapped->epk);
}

void TestWebcamCrypto::wrapWrongAadOrViewer() {
	const std::optional< KeyPair > viewer = x25519KeyPair();
	const std::optional< KeyPair > other  = x25519KeyPair();
	QVERIFY(viewer && other);
	const std::optional< WrappedBox > wrapped = sealToViewer(viewer->publicKey, "secret", "webcam/watch-rekey");
	QVERIFY(wrapped);

	QVERIFY(!openFromSender(*viewer, *wrapped, "webcam/watch-grant"));
	QVERIFY(!openFromSender(*other, *wrapped, "webcam/watch-rekey"));

	WrappedBox tampered = *wrapped;
	tampered.box[IV_SIZE] ^= 0x80;
	QVERIFY(!openFromSender(*viewer, tampered, "webcam/watch-rekey"));
}

void TestWebcamCrypto::wrapVector() {
	// The fixed-input vector recorded in the spec: Alice (RFC 7748 6.1) is the sender's
	// ephemeral key, Bob is the viewer, and the IV is fixed.
	const std::optional< KeyPair > alice = x25519KeyPair(fromHex(ALICE_PRIVATE));
	const std::optional< KeyPair > bob   = x25519KeyPair(fromHex(BOB_PRIVATE));
	QVERIFY(alice && bob);

	const std::optional< Bytes > kw = wrapKey(alice->privateKey, bob->publicKey, alice->publicKey, bob->publicKey);
	QVERIFY(kw);
	QCOMPARE(hex(*kw), QByteArray(WRAP_VECTOR_KW));

	const std::string plaintext = R"({"kid":0,"key":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="})";
	const std::string aad       = "webcam/watch-grant00112233aabbccdd";
	const std::optional< WrappedBox > wrapped =
		sealToViewer(bob->publicKey, plaintext, aad, alice, fromHex("000102030405060708090a0b"));
	QVERIFY(wrapped);
	QCOMPARE(hex(wrapped->epk), QByteArray(ALICE_PUBLIC));
	QCOMPARE(hex(wrapped->box), QByteArray(WRAP_VECTOR_BOX));

	const std::optional< std::string > opened = openFromSender(*bob, *wrapped, aad);
	QVERIFY(opened && *opened == plaintext);
}

QTEST_MAIN(TestWebcamCrypto)
#include "TestWebcamCrypto.moc"
