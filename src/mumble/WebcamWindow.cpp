// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "WebcamWindow.h"

#include "WebcamMode.h"

#include <QtGui/QContextMenuEvent>
#include <QtGui/QGuiApplication>
#include <QtGui/QPainter>
#include <QtGui/QScreen>
#include <QtGui/QWindow>
#include <QtWidgets/QMenu>
#include <QtWidgets/QToolButton>

namespace {
/// How close to an edge a press resizes rather than moves.
constexpr int EDGE          = 8;
constexpr int DEFAULT_WIDTH = 480;
} // namespace

WebcamWindow::WebcamWindow(const QString &title, const QByteArray &geometry, QWidget *parent)
	: QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint), m_title(title) {
	setWindowTitle(title);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setMouseTracking(true);
	setMinimumSize(160, 90);

	m_close = new QToolButton(this);
	m_close->setText(QStringLiteral("✕"));
	m_close->setToolTip(tr("Close"));
	m_close->setAutoRaise(true);
	m_close->setCursor(Qt::ArrowCursor);
	m_close->hide();
	connect(m_close, &QToolButton::clicked, this, &WebcamWindow::closeByUser);

	// A restored geometry gives the place and the size; the picture still sets the shape.
	if (geometry.isEmpty() || !restoreGeometry(geometry)) {
		resize(DEFAULT_WIDTH, DEFAULT_WIDTH * 9 / 16);
		if (const QScreen *screen = QGuiApplication::primaryScreen()) {
			const QRect area = screen->availableGeometry();
			move(area.right() - width() - 32, area.bottom() - height() - 32);
		}
	}
}

void WebcamWindow::setStatus(const QString &status) {
	m_status = status;
	update();
}

void WebcamWindow::clearFrame() {
	m_frame = QImage();
	update();
}

void WebcamWindow::setFrame(const QImage &image) {
	if (image.isNull()) {
		return;
	}
	// The first picture (and any change of shape) sizes the window, until the user does.
	if (!m_userSized && (m_frame.isNull() || m_frame.size() != image.size())) {
		fitTo(image.size());
	}
	m_frame = image;
	m_status.clear();
	update();
}

void WebcamWindow::paintEvent(QPaintEvent *) {
	QPainter painter(this);
	painter.fillRect(rect(), Qt::black);

	if (!m_frame.isNull()) {
		const QSize scaled = m_frame.size().scaled(size(), Qt::KeepAspectRatio);
		const QRect target((width() - scaled.width()) / 2, (height() - scaled.height()) / 2, scaled.width(),
						   scaled.height());
		painter.setRenderHint(QPainter::SmoothPixmapTransform);
		painter.drawImage(target, m_frame);
	}

	painter.setPen(Qt::white);
	if (!m_status.isEmpty()) {
		painter.drawText(rect().adjusted(12, 12, -12, -12), Qt::AlignCenter | Qt::TextWordWrap, m_status);
	}
	if (m_hovered || m_frame.isNull()) {
		const QRect bar(0, 0, width(), fontMetrics().height() + 12);
		painter.fillRect(bar, QColor(0, 0, 0, 140));
		painter.drawText(bar.adjusted(8, 0, -40, 0), Qt::AlignVCenter | Qt::AlignLeft,
						 fontMetrics().elidedText(m_title, Qt::ElideRight, bar.width() - 48));
	}
}

void WebcamWindow::fitTo(const QSize &picture) {
	const QScreen *screen = this->screen() ? this->screen() : QGuiApplication::primaryScreen();
	const QRect area      = screen ? screen->availableGeometry() : QRect();
	const std::pair< int, int > fitted =
		Mumble::Webcam::windowSizeFor(width(), height(), picture.width(), picture.height(), minimumWidth(),
									  minimumHeight(), area.width(), area.height());

	// Around the same centre, then back onto the screen if that pushed it off.
	QRect target(QPoint(), QSize(fitted.first, fitted.second));
	target.moveCenter(geometry().center());
	if (area.isValid()) {
		target.moveLeft(std::max(area.left(), std::min(target.left(), area.right() - target.width() + 1)));
		target.moveTop(std::max(area.top(), std::min(target.top(), area.bottom() - target.height() + 1)));
	}
	setGeometry(target);
}

Qt::Edges WebcamWindow::edgesAt(const QPoint &pos) const {
	Qt::Edges edges;
	if (pos.x() < EDGE) {
		edges |= Qt::LeftEdge;
	} else if (pos.x() >= width() - EDGE) {
		edges |= Qt::RightEdge;
	}
	if (pos.y() < EDGE) {
		edges |= Qt::TopEdge;
	} else if (pos.y() >= height() - EDGE) {
		edges |= Qt::BottomEdge;
	}
	return edges;
}

void WebcamWindow::mousePressEvent(QMouseEvent *event) {
	if (event->button() != Qt::LeftButton || !windowHandle()) {
		QWidget::mousePressEvent(event);
		return;
	}
	// The window system moves and resizes, so it works the same on X11, Wayland and Windows.
	const Qt::Edges edges = edgesAt(event->position().toPoint());
	if (edges) {
		m_userSized = true;
		windowHandle()->startSystemResize(edges);
	} else {
		windowHandle()->startSystemMove();
	}
	event->accept();
}

void WebcamWindow::mouseMoveEvent(QMouseEvent *event) {
	const Qt::Edges edges = edgesAt(event->position().toPoint());
	if (edges == (Qt::LeftEdge | Qt::TopEdge) || edges == (Qt::RightEdge | Qt::BottomEdge)) {
		setCursor(Qt::SizeFDiagCursor);
	} else if (edges == (Qt::RightEdge | Qt::TopEdge) || edges == (Qt::LeftEdge | Qt::BottomEdge)) {
		setCursor(Qt::SizeBDiagCursor);
	} else if (edges & (Qt::LeftEdge | Qt::RightEdge)) {
		setCursor(Qt::SizeHorCursor);
	} else if (edges & (Qt::TopEdge | Qt::BottomEdge)) {
		setCursor(Qt::SizeVerCursor);
	} else {
		setCursor(Qt::SizeAllCursor);
	}
	QWidget::mouseMoveEvent(event);
}

void WebcamWindow::mouseDoubleClickEvent(QMouseEvent *event) {
	// Back to the picture's own shape at the current width.
	if (!m_frame.isNull()) {
		m_userSized = false;
		fitTo(m_frame.size());
	}
	event->accept();
}

void WebcamWindow::keyPressEvent(QKeyEvent *event) {
	if (event->key() == Qt::Key_Escape) {
		closeByUser();
		return;
	}
	QWidget::keyPressEvent(event);
}

void WebcamWindow::contextMenuEvent(QContextMenuEvent *event) {
	QMenu menu(this);
	QAction *fit = menu.addAction(tr("Fit to picture"));
	fit->setEnabled(!m_frame.isNull());
	menu.addSeparator();
	QAction *close  = menu.addAction(tr("Close"));
	QAction *chosen = menu.exec(event->globalPos());
	if (chosen == close) {
		closeByUser();
	} else if (chosen == fit) {
		m_userSized = false;
		fitTo(m_frame.size());
	}
}

void WebcamWindow::enterEvent(QEnterEvent *event) {
	m_hovered = true;
	m_close->show();
	update();
	QWidget::enterEvent(event);
}

void WebcamWindow::leaveEvent(QEvent *event) {
	m_hovered = false;
	m_close->hide();
	update();
	QWidget::leaveEvent(event);
}

void WebcamWindow::resizeEvent(QResizeEvent *event) {
	const int side = fontMetrics().height() + 8;
	m_close->setGeometry(width() - side - 4, 2, side, side);
	QWidget::resizeEvent(event);
}

void WebcamWindow::closeEvent(QCloseEvent *event) {
	// The owner disconnects before it closes the window itself, so this is the window manager.
	emit closedByUser();
	QWidget::closeEvent(event);
}

void WebcamWindow::closeByUser() {
	emit closedByUser();
}
