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

int MultiSlider::handleNear(const QPoint &pos) const {
	int best         = 0;
	int bestDistance = std::numeric_limits< int >::max();
	for (int i = 0; i < m_values.size(); ++i) {
		const int distance = std::abs(valueToX(m_values[i]) - pos.x());
		if (distance < bestDistance) {
			bestDistance = distance;
			best         = i;
		}
	}
	return best;
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

	for (int i = 0; i < m_values.size(); ++i) {
		const int x = valueToX(m_values[i]);

		// Handle.
		QColor handleColor = pal.color(i == m_active ? QPalette::Highlight : QPalette::Button);
		if (!enabled) {
			handleColor.setAlphaF(0.5f);
		}
		p.setPen(QPen(pal.color(QPalette::Dark), 1));
		p.setBrush(handleColor);
		p.drawRoundedRect(QRect(x - HANDLE_WIDTH / 2, y - HANDLE_RADIUS, HANDLE_WIDTH, 2 * HANDLE_RADIUS), 2, 2);

		// Value above, caption below, both centred on the handle and kept inside
		// the widget.
		// Keep the labels inside the widget so the edge handles are not clipped.
		const int textW       = 70;
		const int textX       = std::clamp(x - textW / 2, 0, std::max(0, width() - textW));
		const QRect valueRect(textX, labelTop, textW, labelH);
		const QRect captionRect(textX, labelBase, textW, labelH);
		p.setPen(enabled ? pal.color(QPalette::WindowText) : pal.color(QPalette::Disabled, QPalette::WindowText));
		if (m_formatter) {
			p.drawText(valueRect, Qt::AlignHCenter | Qt::AlignVCenter, m_formatter(m_values[i]));
		}
		if (i < m_captions.size()) {
			p.drawText(captionRect, Qt::AlignHCenter | Qt::AlignVCenter, m_captions[i]);
		}
	}
}

void MultiSlider::mousePressEvent(QMouseEvent *event) {
	if (event->button() != Qt::LeftButton) {
		return;
	}
	const int index = handleNear(event->position().toPoint());
	moveHandle(index, valueFromX(static_cast< int >(event->position().x())));
}

void MultiSlider::mouseMoveEvent(QMouseEvent *event) {
	if (!(event->buttons() & Qt::LeftButton)) {
		return;
	}
	moveHandle(m_active, valueFromX(static_cast< int >(event->position().x())));
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
