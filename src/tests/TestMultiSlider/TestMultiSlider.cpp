// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QApplication>
#include <QSignalSpy>
#include <QtTest>

#include "MultiSlider.h"

/// Verifies the MultiSlider keeps its handles from crossing, so the base,
/// adaptive and maximum amplification values stay in non-decreasing order,
/// and that stacked (coincident) handles can still be grabbed by dragging.
class TestMultiSlider : public QObject {
	Q_OBJECT
private slots:
	void initialValues();
	void handlesCannotCross();
	void setValuesEnforcesOrder();
	void rangeClamps();
	void emitsOnChange();
	void stackedHandlesDragRightGrabsLast();
	void stackedHandlesDragLeftGrabsFirst();
	void stackedHandlesPlainClickKeepsValues();
};

// Drives the slider with synthesized mouse events, as a user dragging from
// \p fromX to \p toX would.
static void drag(MultiSlider &slider, int fromX, int toX) {
	const QPointF from(fromX, 25);
	const QPointF to(toX, 25);
	QMouseEvent press(QEvent::MouseButtonPress, from, from, from, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent(&slider, &press);
	QMouseEvent move(QEvent::MouseMove, to, to, to, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent(&slider, &move);
	QMouseEvent release(QEvent::MouseButtonRelease, to, to, to, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
	QApplication::sendEvent(&slider, &release);
}

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

void TestMultiSlider::stackedHandlesDragRightGrabsLast() {
	MultiSlider slider;
	slider.resize(300, 60);
	slider.setRange(0, 100);
	slider.setHandleCount(3);
	// All handles stacked at the minimum, like three levels at "off": the
	// first handle is wedged against its neighbours, so a rightwards drag has
	// to grab the last one for anything to move at all.
	slider.setValues({ 0, 0, 0 });

	drag(slider, 150, 250);

	QCOMPARE(slider.value(0), 0);
	QCOMPARE(slider.value(1), 0);
	QVERIFY(slider.value(2) > 0);
}

void TestMultiSlider::stackedHandlesDragLeftGrabsFirst() {
	MultiSlider slider;
	slider.resize(300, 60);
	slider.setRange(0, 100);
	slider.setHandleCount(3);
	slider.setValues({ 100, 100, 100 });

	drag(slider, 150, 50);

	QVERIFY(slider.value(0) < 100);
	QCOMPARE(slider.value(1), 100);
	QCOMPARE(slider.value(2), 100);
}

void TestMultiSlider::stackedHandlesPlainClickKeepsValues() {
	MultiSlider slider;
	slider.resize(300, 60);
	slider.setRange(0, 100);
	slider.setHandleCount(3);
	slider.setValues({ 50, 50, 50 });

	// A click without a drag must not teleport any handle of the stack.
	drag(slider, 150, 150);

	QCOMPARE(slider.value(0), 50);
	QCOMPARE(slider.value(1), 50);
	QCOMPARE(slider.value(2), 50);
}

QTEST_MAIN(TestMultiSlider)
#include "TestMultiSlider.moc"
