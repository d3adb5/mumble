// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_WIDGETS_AVATARCROPDIALOG_H_
#define MUMBLE_MUMBLE_WIDGETS_AVATARCROPDIALOG_H_

#include <QtWidgets/QDialog>
#include <QtWidgets/QSlider>

/// The square viewport in which the avatar image can be panned (by dragging)
/// and zoomed. Whatever is visible inside the viewport becomes the avatar.
class AvatarCropWidget : public QWidget {
	Q_OBJECT
	Q_DISABLE_COPY(AvatarCropWidget)

public:
	/// Side length of the resulting avatar image (it is always square)
	static constexpr int AVATAR_SIZE = 512;

	explicit AvatarCropWidget(const QImage &image, QWidget *parent = nullptr);

	/// Renders the currently visible part of the image into a square avatar
	/// image of at most AVATAR_SIZE pixels per side. The image is never
	/// upscaled beyond its native resolution.
	QImage croppedAvatar() const;

	/// Sets the zoom level in percent, where 100 means the image just covers
	/// the viewport and larger values zoom in.
	void setZoomPercent(int percent);

signals:
	void zoomPercentChanged(int percent);

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;

private:
	/// The source image to crop from
	QImage m_image;
	/// Current scale from image pixels to widget pixels
	qreal m_scale = 1.0;
	/// Position of the scaled image's top-left corner in widget coordinates
	QPointF m_offset;
	bool m_dragging = false;
	QPoint m_dragStart;
	QPointF m_dragStartOffset;

	/// The scale at which the image exactly covers the (square) viewport
	qreal coverScale() const;
	/// Keeps the image covering the whole viewport
	void clampOffset();
};

/// Dialog for interactively cropping an image to the square avatar format:
/// drag the image to position it, zoom with the slider or mouse wheel.
class AvatarCropDialog : public QDialog {
	Q_OBJECT
	Q_DISABLE_COPY(AvatarCropDialog)

public:
	explicit AvatarCropDialog(const QImage &image, QWidget *parent = nullptr);

	/// @returns the cropped, square avatar image (at most 512x512 pixels)
	QImage croppedAvatar() const;

private:
	AvatarCropWidget *m_cropWidget;
	QSlider *m_zoomSlider;
};

#endif // MUMBLE_MUMBLE_WIDGETS_AVATARCROPDIALOG_H_
