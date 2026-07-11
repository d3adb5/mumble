// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_WIDGETS_MULTISLIDER_H_
#define MUMBLE_MUMBLE_WIDGETS_MULTISLIDER_H_

#include <functional>

#include <QColor>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

/// A horizontal slider carrying several handles on a single groove. The handles
/// can never cross each other, so the values stay in non-decreasing order
/// (handy for the base <= adaptive <= maximum amplification levels). Each handle
/// draws a caption and a formatted value, and an optional live indicator can be
/// overlaid to show a current value against the configured handles.
class MultiSlider : public QWidget {
	Q_OBJECT
public:
	explicit MultiSlider(QWidget *parent = nullptr);

	void setRange(int minimum, int maximum);
	int minimum() const { return m_minimum; }
	int maximum() const { return m_maximum; }
	void setSingleStep(int step) { m_singleStep = step; }

	void setHandleCount(int count);
	int handleCount() const { return static_cast< int >(m_values.size()); }

	/// Captions drawn underneath each handle, e.g. "Base", "Adaptive", "Max".
	void setCaptions(const QStringList &captions);
	/// Formats a handle value for the label drawn above the handle.
	void setValueFormatter(std::function< QString(int) > formatter);

	int value(int index) const;
	void setValue(int index, int value);
	QVector< int > values() const { return m_values; }
	void setValues(const QVector< int > &values);

	/// Overlay a thin marker at \p value to show a live reading against the
	/// handles. Pass active = false to hide it.
	void setIndicator(int value, bool active, const QColor &color);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

signals:
	void valuesChanged();

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void keyPressEvent(QKeyEvent *) override;

private:
	int m_minimum    = 0;
	int m_maximum    = 100;
	int m_singleStep = 1;
	QVector< int > m_values;
	QStringList m_captions;
	std::function< QString(int) > m_formatter;
	int m_active = 0;

	/// Several handles can sit on the same spot (e.g. every level at "no
	/// amplification"), making a press ambiguous. The pick is then deferred
	/// to the first drag movement: dragging right grabs the last handle of
	/// the stack (the one free to move right), dragging left the first (the
	/// one free to move left).
	bool m_pendingPick = false;
	int m_pickLow      = 0;
	int m_pickHigh     = 0;
	int m_pressX       = 0;

	bool m_indicatorActive = false;
	int m_indicatorValue   = 0;
	QColor m_indicatorColor;

	void moveHandle(int index, int value);
	int clampHandle(int index, int value) const;
	int valueToX(int value) const;
	int valueFromX(int x) const;
	int grooveLeft() const;
	int grooveRight() const;
	int grooveY() const;
	// Handles (and the live indicator fill) are rectangles spanning 2 * HANDLE_RADIUS
	// in height; HANDLE_WIDTH is their (narrower) width.
	static constexpr int HANDLE_RADIUS = 6;
	static constexpr int HANDLE_WIDTH  = 8;
};

#endif
