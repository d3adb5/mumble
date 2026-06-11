// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "AvatarCropDialog.h"

#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <cmath>

/// Side length (in widget pixels) of the crop viewport
static constexpr int VIEWPORT_SIZE = 320;
/// Maximum zoom-in factor relative to the covering scale
static constexpr int MAX_ZOOM_PERCENT = 400;

AvatarCropWidget::AvatarCropWidget(const QImage &image, QWidget *parent)
	: QWidget(parent), m_image(image.convertToFormat(QImage::Format_ARGB32)) {
	setFixedSize(VIEWPORT_SIZE, VIEWPORT_SIZE);
	setCursor(Qt::OpenHandCursor);

	m_scale = coverScale();
	// Center the image in the viewport
	m_offset = QPointF((width() - m_image.width() * m_scale) / 2.0, (height() - m_image.height() * m_scale) / 2.0);
}

qreal AvatarCropWidget::coverScale() const {
	const int minSide = std::min(m_image.width(), m_image.height());
	return minSide > 0 ? static_cast< qreal >(VIEWPORT_SIZE) / minSide : 1.0;
}

void AvatarCropWidget::clampOffset() {
	const qreal scaledWidth  = m_image.width() * m_scale;
	const qreal scaledHeight = m_image.height() * m_scale;

	m_offset.setX(std::clamp(m_offset.x(), width() - scaledWidth, 0.0));
	m_offset.setY(std::clamp(m_offset.y(), height() - scaledHeight, 0.0));
}

void AvatarCropWidget::setZoomPercent(int percent) {
	percent = std::clamp(percent, 100, MAX_ZOOM_PERCENT);

	const qreal newScale = coverScale() * percent / 100.0;
	if (qFuzzyCompare(newScale, m_scale)) {
		return;
	}

	// Keep the point at the viewport center fixed while zooming
	const QPointF center      = QPointF(width(), height()) / 2.0;
	const QPointF imagePoint  = (center - m_offset) / m_scale;
	m_scale                   = newScale;
	m_offset                  = center - imagePoint * m_scale;

	clampOffset();
	update();

	emit zoomPercentChanged(percent);
}

void AvatarCropWidget::paintEvent(QPaintEvent *) {
	QPainter painter(this);
	painter.setRenderHint(QPainter::SmoothPixmapTransform);

	painter.save();
	painter.translate(m_offset);
	painter.scale(m_scale, m_scale);
	painter.drawImage(0, 0, m_image);
	painter.restore();

	painter.setPen(palette().color(QPalette::Mid));
	painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

void AvatarCropWidget::mousePressEvent(QMouseEvent *event) {
	if (event->button() == Qt::LeftButton) {
		m_dragging        = true;
		m_dragStart       = event->pos();
		m_dragStartOffset = m_offset;
		setCursor(Qt::ClosedHandCursor);
	}
}

void AvatarCropWidget::mouseMoveEvent(QMouseEvent *event) {
	if (m_dragging) {
		m_offset = m_dragStartOffset + (event->pos() - m_dragStart);
		clampOffset();
		update();
	}
}

void AvatarCropWidget::mouseReleaseEvent(QMouseEvent *event) {
	if (event->button() == Qt::LeftButton) {
		m_dragging = false;
		setCursor(Qt::OpenHandCursor);
	}
}

void AvatarCropWidget::wheelEvent(QWheelEvent *event) {
	const int currentPercent = static_cast< int >(std::round(m_scale / coverScale() * 100.0));
	setZoomPercent(currentPercent + event->angleDelta().y() / 8);
}

QImage AvatarCropWidget::croppedAvatar() const {
	// The part of the image (in image coordinates) that is visible in the viewport
	const QRectF sourceRect(-m_offset.x() / m_scale, -m_offset.y() / m_scale, width() / m_scale, height() / m_scale);

	// Never upscale beyond the image's native resolution
	const int targetSize = std::min< int >(AVATAR_SIZE, static_cast< int >(std::round(sourceRect.width())));
	if (targetSize <= 0) {
		return QImage();
	}

	QImage avatar(targetSize, targetSize, QImage::Format_ARGB32);
	avatar.fill(Qt::transparent);

	QPainter painter(&avatar);
	painter.setRenderHint(QPainter::SmoothPixmapTransform);
	painter.drawImage(QRectF(0, 0, targetSize, targetSize), m_image, sourceRect);
	painter.end();

	return avatar;
}

AvatarCropDialog::AvatarCropDialog(const QImage &image, QWidget *parent) : QDialog(parent) {
	setWindowTitle(tr("Crop Avatar"));

	QVBoxLayout *layout = new QVBoxLayout(this);

	QLabel *help = new QLabel(tr("Drag the image to position it and zoom with the slider or mouse wheel. "
								 "The square area will be used as your avatar."),
							  this);
	help->setWordWrap(true);
	layout->addWidget(help);

	m_cropWidget = new AvatarCropWidget(image, this);
	layout->addWidget(m_cropWidget, 0, Qt::AlignHCenter);

	m_zoomSlider = new QSlider(Qt::Horizontal, this);
	m_zoomSlider->setRange(100, MAX_ZOOM_PERCENT);
	m_zoomSlider->setValue(100);
	m_zoomSlider->setToolTip(tr("Zoom"));
	m_zoomSlider->setAccessibleName(tr("Avatar zoom level"));
	layout->addWidget(m_zoomSlider);

	connect(m_zoomSlider, &QSlider::valueChanged, m_cropWidget, &AvatarCropWidget::setZoomPercent);
	connect(m_cropWidget, &AvatarCropWidget::zoomPercentChanged, m_zoomSlider, &QSlider::setValue);

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
}

QImage AvatarCropDialog::croppedAvatar() const {
	return m_cropWidget->croppedAvatar();
}
