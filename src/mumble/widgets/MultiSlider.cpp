// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "MultiSlider.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QStyle>

MultiSlider::MultiSlider(QWidget *parent) : QWidget(parent) {
	setFocusPolicy(Qt::StrongFocus);
	setHandleCount(3);
}

void MultiSlider::setRange(int minimum, int maximum) {
	m_minimum = minimum;
	m_maximum = std::max(maximum, minimum + 1);
	setValues(m_values);
}

void MultiSlider::setHandleCount(int count) {
	count = std::max(count, 1);
	m_values.resize(count);
	// Spread any freshly added handles across the range.
	for (int i = 0; i < count; ++i) {
		m_values[i] = m_minimum + (m_maximum - m_minimum) * i / count;
	}
	setValues(m_values);
}

void MultiSlider::setCaptions(const QStringList &captions) {
	m_captions = captions;
	update();
}

void MultiSlider::setValueFormatter(std::function< QString(int) > formatter) {
	m_formatter = std::move(formatter);
	update();
}

int MultiSlider::value(int index) const {
	return (index >= 0 && index < m_values.size()) ? m_values[index] : 0;
}

void MultiSlider::setValue(int index, int value) {
	if (index < 0 || index >= m_values.size()) {
		return;
	}
	const int clamped = clampHandle(index, value);
	if (clamped != m_values[index]) {
		m_values[index] = clamped;
		update();
		emit valuesChanged();
	}
}

void MultiSlider::setValues(const QVector< int > &values) {
	for (int i = 0; i < m_values.size() && i < values.size(); ++i) {
		m_values[i] = values[i];
	}
	// Re-establish the non-decreasing invariant and clamp to the range.
	for (int i = 0; i < m_values.size(); ++i) {
		m_values[i] = clampHandle(i, m_values[i]);
	}
	update();
}

void MultiSlider::setIndicator(int value, bool active, const QColor &color) {
	m_indicatorValue  = std::clamp(value, m_minimum, m_maximum);
	m_indicatorActive = active;
	m_indicatorColor  = color;
	update();
}

void MultiSlider::moveHandle(int index, int value) {
	setValue(index, value);
	m_active = index;
}

int MultiSlider::clampHandle(int index, int value) const {
	const int lo = (index > 0) ? m_values[index - 1] : m_minimum;
	const int hi = (index < m_values.size() - 1) ? m_values[index + 1] : m_maximum;
	return std::clamp(value, lo, hi);
}

int MultiSlider::grooveLeft() const {
	return HANDLE_RADIUS + 2;
}

int MultiSlider::grooveRight() const {
	return width() - HANDLE_RADIUS - 2;
}

int MultiSlider::grooveY() const {
	// Leave room for the value labels above and the captions below the groove.
	return fontMetrics().height() + 4 + HANDLE_RADIUS + 2;
}

int MultiSlider::valueToX(int value) const {
	const int span = grooveRight() - grooveLeft();
	return grooveLeft() + (value - m_minimum) * span / (m_maximum - m_minimum);
}

int MultiSlider::valueFromX(int x) const {
	const int span = grooveRight() - grooveLeft();
	if (span <= 0) {
		return m_minimum;
	}
	const int value = m_minimum + (x - grooveLeft()) * (m_maximum - m_minimum) / span;
	return std::clamp(value, m_minimum, m_maximum);
}


void MultiSlider::paintEvent(QPaintEvent *) {
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);

	const QPalette &pal = palette();
	const int left      = grooveLeft();
	const int right     = grooveRight();
	const int y         = grooveY();
	const bool enabled  = isEnabled();

	// Live indicator (e.g. the amplification currently applied), drawn first as a
	// translucent rectangular fill running from the left up to the current value,
	// so it reads as the bar being filled rather than a single line. It sits below
	// the groove and the handles in the Z-order.
	if (m_indicatorActive) {
		const int ix = valueToX(m_indicatorValue);
		if (ix > left) {
			QColor fill = m_indicatorColor;
			fill.setAlphaF(enabled ? 0.30f : 0.18f);
			p.setPen(Qt::NoPen);
			p.setBrush(fill);
			p.drawRoundedRect(QRect(left, y - HANDLE_RADIUS, ix - left, 2 * HANDLE_RADIUS), 2, 2);
		}
	}

	// Groove: a faint full-width track with the configured span (up to the last
	// handle) filled in the highlight colour.
	const QRect groove(left, y - 2, right - left, 4);
	p.setPen(Qt::NoPen);
	p.setBrush(pal.color(QPalette::Mid));
	p.drawRoundedRect(groove, 2, 2);

	if (!m_values.isEmpty()) {
		QColor fill = pal.color(QPalette::Highlight);
		if (!enabled) {
			fill.setAlphaF(0.5f);
		}
		const QRect filled(left, y - 2, valueToX(m_values.last()) - left, 4);
		p.setBrush(fill);
		p.drawRoundedRect(filled, 2, 2);
	}

	const int labelTop  = 0;
	const int labelBase = y + HANDLE_RADIUS + 2;
	const int labelH    = fontMetrics().height();

	// Handles that sit on the same spot are treated as one group: they are
	// fanned out slightly around their shared position so every handle stays
	// visible, and they share a combined caption/value label instead of
	// painting several labels on top of each other.
	for (int i = 0; i < m_values.size();) {
		const int x = valueToX(m_values[i]);
		int last    = i;
		while (last + 1 < m_values.size() && valueToX(m_values[last + 1]) == x) {
			++last;
		}
		const int groupSize = last - i + 1;

		for (int k = i; k <= last; ++k) {
			const int fanX = x + ((k - i) * 2 - (groupSize - 1)) * (HANDLE_WIDTH + 1) / 2;

			QColor handleColor = pal.color(k == m_active ? QPalette::Highlight : QPalette::Button);
			if (!enabled) {
				handleColor.setAlphaF(0.5f);
			}
			p.setPen(QPen(pal.color(QPalette::Dark), 1));
			p.setBrush(handleColor);
			p.drawRoundedRect(QRect(fanX - HANDLE_WIDTH / 2, y - HANDLE_RADIUS, HANDLE_WIDTH, 2 * HANDLE_RADIUS), 2,
							  2);
		}

		// One value text (distinct values landing on the same pixel are joined)
		// and one caption for the whole group, centred on the shared position
		// and kept inside the widget so the edge handles are not clipped.
		QString valueText;
		if (m_formatter) {
			QStringList valueTexts;
			for (int k = i; k <= last; ++k) {
				const QString text = m_formatter(m_values[k]);
				if (!valueTexts.contains(text)) {
					valueTexts << text;
				}
			}
			valueText = valueTexts.join(QLatin1Char('/'));
		}
		QStringList captionTexts;
		for (int k = i; k <= last && k < m_captions.size(); ++k) {
			captionTexts << m_captions[k];
		}
		const QString captionText = captionTexts.join(QLatin1Char('/'));

		const int textW = std::max({ 70, fontMetrics().horizontalAdvance(valueText) + 8,
									 fontMetrics().horizontalAdvance(captionText) + 8 });
		const int textX = std::clamp(x - textW / 2, 0, std::max(0, width() - textW));
		const QRect valueRect(textX, labelTop, textW, labelH);
		const QRect captionRect(textX, labelBase, textW, labelH);
		p.setPen(enabled ? pal.color(QPalette::WindowText) : pal.color(QPalette::Disabled, QPalette::WindowText));
		if (!valueText.isEmpty()) {
			p.drawText(valueRect, Qt::AlignHCenter | Qt::AlignVCenter, valueText);
		}
		if (!captionText.isEmpty()) {
			p.drawText(captionRect, Qt::AlignHCenter | Qt::AlignVCenter, captionText);
		}

		i = last + 1;
	}
}

void MultiSlider::mousePressEvent(QMouseEvent *event) {
	if (event->button() != Qt::LeftButton) {
		return;
	}
	const int x = static_cast< int >(event->position().x());

	// Find the nearest handle. Several handles may tie for that distance
	// (typically because they sit on the very same value); grabbing the first
	// one would wedge it against its neighbour, so for a tie the choice is
	// deferred until the drag direction is known.
	int bestDistance = std::numeric_limits< int >::max();
	for (int i = 0; i < m_values.size(); ++i) {
		bestDistance = std::min(bestDistance, std::abs(valueToX(m_values[i]) - x));
	}
	int low  = -1;
	int high = -1;
	for (int i = 0; i < m_values.size(); ++i) {
		if (std::abs(valueToX(m_values[i]) - x) == bestDistance) {
			if (low < 0) {
				low = i;
			}
			high = i;
		}
	}

	if (low == high) {
		m_pendingPick = false;
		moveHandle(low, valueFromX(x));
	} else {
		m_pendingPick = true;
		m_pickLow     = low;
		m_pickHigh    = high;
		m_pressX      = x;
		m_active      = high;
		update();
	}
}

void MultiSlider::mouseMoveEvent(QMouseEvent *event) {
	if (!(event->buttons() & Qt::LeftButton)) {
		return;
	}
	const int x = static_cast< int >(event->position().x());
	if (m_pendingPick) {
		// Wait for a clear direction before committing to a handle.
		if (std::abs(x - m_pressX) < 3) {
			return;
		}
		m_active      = (x > m_pressX) ? m_pickHigh : m_pickLow;
		m_pendingPick = false;
	}
	moveHandle(m_active, valueFromX(x));
}

void MultiSlider::mouseReleaseEvent(QMouseEvent *event) {
	if (event->button() != Qt::LeftButton) {
		return;
	}
	// A plain click on a stacked group without a drag: leave the values alone,
	// the group's last handle stays active for keyboard adjustment.
	m_pendingPick = false;
}

void MultiSlider::keyPressEvent(QKeyEvent *event) {
	switch (event->key()) {
		case Qt::Key_Left:
		case Qt::Key_Down:
			setValue(m_active, m_values[m_active] - m_singleStep);
			break;
		case Qt::Key_Right:
		case Qt::Key_Up:
			setValue(m_active, m_values[m_active] + m_singleStep);
			break;
		case Qt::Key_Home:
			setValue(m_active, m_minimum);
			break;
		case Qt::Key_End:
			setValue(m_active, m_maximum);
			break;
		case Qt::Key_Tab:
			m_active = (m_active + 1) % static_cast< int >(m_values.size());
			update();
			break;
		default:
			QWidget::keyPressEvent(event);
			return;
	}
	event->accept();
}

QSize MultiSlider::minimumSizeHint() const {
	return QSize(240, fontMetrics().height() * 2 + 4 + 2 * HANDLE_RADIUS + 6);
}

QSize MultiSlider::sizeHint() const {
	return minimumSizeHint();
}
