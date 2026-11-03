// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_WEBCAMWINDOW_H_
#define MUMBLE_MUMBLE_WEBCAMWINDOW_H_

#include <QtGui/QImage>
#include <QtWidgets/QWidget>

class QPainter;
class QToolButton;

/// Paints a camera picture the way every camera view shows it: black around the picture (kept
/// in shape and centred), \p status over it, and \p title in a bar at the top when \p showTitle
/// or when there is no picture yet (never when \p title is empty).
void paintWebcamPicture(QPainter &painter, const QRect &rect, const QImage &frame, const QString &status,
						const QString &title, bool showTitle);

/// The floating window a watched camera plays in: frameless and always on top, moved by
/// dragging anywhere and resized from its edges. Its owner closes it when the share ends.
class WebcamWindow : public QWidget {
private:
	Q_OBJECT
	Q_DISABLE_COPY(WebcamWindow)

public:
	WebcamWindow(const QString &title, const QByteArray &geometry, QWidget *parent = nullptr);

	void setStatus(const QString &status);
	/// What closing the window does, for its close button and context menu ("Close" by default).
	void setCloseLabel(const QString &label);
	/// Drops the picture, back to the status alone.
	void clearFrame();

public slots:
	void setFrame(const QImage &image);

signals:
	/// The user closed the window (the close button, the context menu, Escape or the window
	/// manager).
	void closedByUser();

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;
	void enterEvent(QEnterEvent *event) override;
	void leaveEvent(QEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void closeEvent(QCloseEvent *event) override;

private:
	Qt::Edges edgesAt(const QPoint &pos) const;
	/// Gives the window the picture's shape at about its current area, kept on the screen.
	void fitTo(const QSize &picture);
	void closeByUser();

	QString m_title;
	QString m_status;
	QImage m_frame;
	QString m_closeLabel;
	QToolButton *m_close = nullptr;
	bool m_hovered       = false;
	/// Whether the user sized the window, after which the picture no longer sets its shape.
	bool m_userSized = false;
};

#endif
