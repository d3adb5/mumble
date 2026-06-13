// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QApplication>
#include <QSignalSpy>
#include <QtTest>

#include "MultiSlider.h"

/// Verifies the MultiSlider keeps its handles from crossing, so the base,
/// adaptive and maximum amplification values stay in non-decreasing order.
class TestMultiSlider : public QObject {
	Q_OBJECT
private slots:
	void initialValues();
	void handlesCannotCross();
	void setValuesEnforcesOrder();
	void rangeClamps();
	void emitsOnChange();
};

void TestMultiSlider::initialValues() {
	MultiSlider slider;
	slider.setRange(0, 100);
	slider.setHandleCount(3);
	slider.setValues({ 10, 50, 90 });
	QCOMPARE(slider.value(0), 10);
	QCOMPARE(slider.value(1), 50);
	QCOMPARE(slider.value(2), 90);
}

void TestMultiSlider::handlesCannotCross() {
	MultiSlider slider;
	slider.setRange(0, 100);
	slider.setHandleCount(3);
	slider.setValues({ 10, 50, 90 });

	// The base cannot be pushed past the adaptive handle.
	slider.setValue(0, 70);
	QCOMPARE(slider.value(0), 50);

	// The maximum cannot be pulled below the adaptive handle.
	slider.setValue(2, 30);
	QCOMPARE(slider.value(2), 50);

	// The adaptive handle is bounded by its neighbours.
	slider.setValue(1, 999);
	QCOMPARE(slider.value(1), slider.value(2));
}

void TestMultiSlider::setValuesEnforcesOrder() {
	MultiSlider slider;
	slider.setRange(0, 100);
	slider.setHandleCount(3);
	slider.setValues({ 80, 20, 90 });
	QVERIFY(slider.value(0) <= slider.value(1));
	QVERIFY(slider.value(1) <= slider.value(2));
}

void TestMultiSlider::rangeClamps() {
	MultiSlider slider;
	slider.setRange(0, 100);
	slider.setHandleCount(3);
	slider.setValues({ -50, 50, 200 });
	QCOMPARE(slider.value(0), 0);
	QCOMPARE(slider.value(2), 100);
}

void TestMultiSlider::emitsOnChange() {
	MultiSlider slider;
	slider.setRange(0, 100);
	slider.setHandleCount(3);
	slider.setValues({ 10, 50, 90 });

	QSignalSpy spy(&slider, &MultiSlider::valuesChanged);
	slider.setValue(1, 60);
	QCOMPARE(spy.count(), 1);

	// Setting the same value again is a no-op and does not emit.
	slider.setValue(1, 60);
	QCOMPARE(spy.count(), 1);

	// A move that gets clamped but still changes the value does emit.
	slider.setValue(0, 999);
	QCOMPARE(spy.count(), 2);
	QCOMPARE(slider.value(0), slider.value(1));
}

QTEST_MAIN(TestMultiSlider)
#include "TestMultiSlider.moc"
