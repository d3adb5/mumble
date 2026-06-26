// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore>
#include <QtTest>

#include "DeepFilterNet.h"

#include <limits>

using namespace Mumble::DeepFilter;

/// Unit tests for the pure DeepFilterNet helpers: the 16 bit PCM <-> normalized
/// float conversion and the clamping/scaling of the user-facing settings. These
/// need neither Qt audio nor the (Rust) DeepFilterNet library.
class TestDeepFilter : public QObject {
	Q_OBJECT
private slots:
	void int16Float_roundTrip();
	void floatToInt16_clamps();
	void attenLimit_clamps();
	void postFilterBeta_scalesAndClamps();
};

void TestDeepFilter::int16Float_roundTrip() {
	// Representative samples survive a float round trip exactly.
	for (short s : { static_cast< short >(0), static_cast< short >(1), static_cast< short >(-1),
					 static_cast< short >(12345), static_cast< short >(-12345),
					 std::numeric_limits< short >::min(), static_cast< short >(32767) }) {
		QCOMPARE(floatToInt16(int16ToFloat(s)), s);
	}

	// The normalization maps full scale into [-1, 1].
	QCOMPARE(int16ToFloat(0), 0.0f);
	QCOMPARE(int16ToFloat(std::numeric_limits< short >::min()), -1.0f);
	QVERIFY(int16ToFloat(32767) < 1.0f);
	QVERIFY(int16ToFloat(32767) > 0.999f);
}

void TestDeepFilter::floatToInt16_clamps() {
	// Out-of-range model output saturates instead of overflowing the cast.
	QCOMPARE(floatToInt16(2.0f), static_cast< short >(32767));
	QCOMPARE(floatToInt16(-2.0f), std::numeric_limits< short >::min());
	QCOMPARE(floatToInt16(0.0f), static_cast< short >(0));
}

void TestDeepFilter::attenLimit_clamps() {
	QCOMPARE(clampAttenLimitDb(ATTEN_LIMIT_DEFAULT_DB), 100.0f);
	QCOMPARE(clampAttenLimitDb(40), 40.0f);
	// Values outside the range are pulled back to the bounds.
	QCOMPARE(clampAttenLimitDb(-10), 0.0f);
	QCOMPARE(clampAttenLimitDb(250), 100.0f);
}

void TestDeepFilter::postFilterBeta_scalesAndClamps() {
	// Disabled by default.
	QCOMPARE(postFilterBeta(POST_FILTER_DEFAULT), 0.0f);
	// The integer setting is beta x 1000.
	QCOMPARE(postFilterBeta(20), 0.02f);
	QCOMPARE(postFilterBeta(POST_FILTER_MAX), 0.05f);
	// Clamped to the supported range.
	QCOMPARE(postFilterBeta(-5), 0.0f);
	QCOMPARE(postFilterBeta(100), 0.05f);
}

QTEST_MAIN(TestDeepFilter)
#include "TestDeepFilter.moc"
