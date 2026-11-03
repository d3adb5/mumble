// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "WebcamArea.h"

#include "WebcamLayout.h"
#include "WebcamWindow.h"
#include "Global.h"

#include <QtGui/QContextMenuEvent>
#include <QtGui/QPainter>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMenu>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>

using namespace Mumble::Webcam;

namespace {
/// Room between the tiles of the grid.
constexpr int SPACING = 6;

QToolButton *overlayButton(const QString &text, const QString &toolTip, QWidget *parent) {
	auto *button = new QToolButton(parent);
	button->setText(text);
	button->setToolTip(toolTip);
	button->setAutoRaise(true);
	button->setCursor(Qt::ArrowCursor);
	button->hide();
	return button;
}

/// Hides \p widget now and deletes it once control is back in the event loop: a tile is removed
/// from its own stop button and context menu, which still run when it is.
void discard(QWidget *widget) {
	if (widget) {
		widget->hide();
		widget->disconnect();
		widget->deleteLater();
	}
}
} // namespace

// -- WebcamTile ---------------------------------------------------------------------------------

WebcamTile::WebcamTile(const QString &title, const QString &stopLabel, QWidget *parent)
	: QWidget(parent), m_title(title), m_stopLabel(stopLabel) {
	setAttribute(Qt::WA_OpaquePaintEvent);
	setMouseTracking(true);

	m_popOut = overlayButton(QStringLiteral("⧉"), tr("Pop out into its own window"), this);
	m_stop   = overlayButton(QStringLiteral("✕"), stopLabel, this);
	connect(m_popOut, &QToolButton::clicked, this, &WebcamTile::popOutRequested);
	connect(m_stop, &QToolButton::clicked, this, &WebcamTile::stopRequested);
}

void WebcamTile::setFrame(const QImage &image) {
	if (image.isNull()) {
		return;
	}
	m_frame = image;
	m_status.clear();
	if (isVisible()) {
		update();
	}
}

void WebcamTile::clearFrame() {
	m_frame = QImage();
	update();
}

void WebcamTile::setStatus(const QString &status) {
	m_status = status;
	update();
}

void WebcamTile::setFramed(bool framed) {
	if (framed != m_framed) {
		m_framed = framed;
		update();
	}
}

void WebcamTile::paintEvent(QPaintEvent *) {
	QPainter painter(this);
	// Small over another camera, the title would cover the status: it shows on hover only.
	paintWebcamPicture(painter, rect(), m_frame, m_status, m_framed && !m_hovered ? QString() : m_title, m_hovered);
	if (m_framed) {
		// Over another camera, the own picture needs an edge to stand apart from it.
		painter.setPen(QPen(palette().color(QPalette::Highlight), 2));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(rect().adjusted(1, 1, -1, -1));
	}
}

void WebcamTile::mouseDoubleClickEvent(QMouseEvent *event) {
	emit popOutRequested();
	event->accept();
}

void WebcamTile::contextMenuEvent(QContextMenuEvent *event) {
	QMenu menu(this);
	QAction *popOut = menu.addAction(tr("Pop Out"));
	menu.addSeparator();
	QAction *stop   = menu.addAction(m_stopLabel);
	QAction *chosen = menu.exec(event->globalPos());
	if (chosen == popOut) {
		emit popOutRequested();
	} else if (chosen == stop) {
		emit stopRequested();
	}
}

void WebcamTile::enterEvent(QEnterEvent *event) {
	m_hovered = true;
	m_popOut->show();
	m_stop->show();
	update();
	QWidget::enterEvent(event);
}

void WebcamTile::leaveEvent(QEvent *event) {
	m_hovered = false;
	m_popOut->hide();
	m_stop->hide();
	update();
	QWidget::leaveEvent(event);
}

void WebcamTile::resizeEvent(QResizeEvent *event) {
	const int side = fontMetrics().height() + 8;
	m_stop->setGeometry(width() - side - 4, 2, side, side);
	m_popOut->setGeometry(width() - 2 * side - 8, 2, side, side);
	QWidget::resizeEvent(event);
}

// -- WebcamArea ---------------------------------------------------------------------------------

WebcamArea::WebcamArea(QWidget *parent) : QWidget(parent) {
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(4);

	auto *shareRow = new QHBoxLayout();
	m_shareLabel   = new QLabel(this);
	m_shareLabel->setWordWrap(true);
	m_shareButton = new QPushButton(this);
	connect(m_shareButton, &QPushButton::clicked, this, [this] {
		if (m_sharing) {
			emit stopSharingRequested();
		} else {
			emit startSharingRequested();
		}
	});
	m_layout = new QComboBox(this);
	m_layout->addItem(tr("Grid"));
	m_layout->addItem(tr("My camera as picture in picture"));
	m_layout->addItem(tr("Hide my camera"));
	m_layout->setToolTip(tr("How the cameras are arranged here"));
	// The area fits beside the user list: the choices may be cut short, never the area widened.
	m_layout->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_layout->setMinimumContentsLength(8);
	m_layout->setCurrentIndex(static_cast< int >(layoutFrom(Global::get().s.iWebcamLayout)));
	connect(m_layout, &QComboBox::currentIndexChanged, this, [this](int index) {
		Global::get().s.iWebcamLayout = index;
		relayout();
	});
	shareRow->addWidget(m_shareLabel, 1);
	shareRow->addWidget(m_layout);
	shareRow->addWidget(m_shareButton);
	layout->addLayout(shareRow);

	// Who watches our camera, each with a button that stops them.
	m_viewersBox = new QWidget(this);
	m_viewersRow = new QHBoxLayout(m_viewersBox);
	m_viewersRow->setContentsMargins(0, 0, 0, 0);
	m_viewersLabel = new QLabel(m_viewersBox);
	m_viewersRow->addWidget(m_viewersLabel);
	m_viewersRow->addStretch(1);
	layout->addWidget(m_viewersBox);

	// Requests to watch that wait for an answer ("Later" in the prompt leaves them here).
	m_asksBox = new QWidget(this);
	m_asksRow = new QHBoxLayout(m_asksBox);
	m_asksRow->setContentsMargins(0, 0, 0, 0);
	m_asksRow->addWidget(new QLabel(tr("Wants to watch:"), m_asksBox));
	m_asksRow->addStretch(1);
	m_asksBox->hide();
	layout->addWidget(m_asksBox);

	m_stage = new QWidget(this);
	m_stage->setMinimumSize(160, 90);
	m_stage->installEventFilter(this);
	m_hint = new QLabel(tr("Cameras you watch show up here. To watch one, right-click someone who shares their "
						   "camera and choose Watch Camera."),
						m_stage);
	m_hint->setWordWrap(true);
	m_hint->setAlignment(Qt::AlignCenter);
	layout->addWidget(m_stage, 1);

	updateShareBar();
	relayout();
}

WebcamArea::~WebcamArea() {
	// The pop-out windows have no parent: they go with the area.
	for (Entry &entry : m_watches) {
		delete entry.window;
	}
	delete m_own.window;
}

void WebcamArea::reloadLayout() {
	const int index = static_cast< int >(layoutFrom(Global::get().s.iWebcamLayout));
	if (m_layout->currentIndex() != index) {
		m_layout->setCurrentIndex(index);
	}
	relayout();
}

void WebcamArea::updateShareBar() {
	if (m_sharing) {
		m_shareLabel->setText(tr("You are sharing your camera."));
		m_shareButton->setText(tr("Stop Sharing"));
		m_shareButton->setToolTip(tr("Stop sharing your camera: everyone watching it is cut off"));
	} else {
		m_shareLabel->setText(tr("You are not sharing your camera."));
		m_shareButton->setText(tr("Share Camera"));
		m_shareButton->setToolTip(tr("Let others ask to watch your camera"));
	}
	m_viewersBox->setVisible(m_sharing);
	m_viewersLabel->setText(m_viewerCount > 0 ? tr("Watching you:") : tr("Nobody is watching your camera."));
}

// -- our own camera ------------------------------------------------------------------------------

void WebcamArea::setSharing(bool sharing) {
	if (sharing == m_sharing) {
		return;
	}
	m_sharing = sharing;
	if (sharing) {
		m_own.title = tr("Your camera (only you see this)");
		m_own.tile  = new WebcamTile(m_own.title, tr("Stop Sharing Your Camera"), m_stage);
		connect(m_own.tile, &WebcamTile::popOutRequested, this,
				[this] { popOut(m_own, tr("Back into the camera area")); });
		connect(m_own.tile, &WebcamTile::stopRequested, this, &WebcamArea::stopSharingRequested);
	} else {
		discard(m_own.window);
		discard(m_own.tile);
		m_own = Entry();
		setViewers({});
		setAsks({});
	}
	updateShareBar();
	relayout();
}

void WebcamArea::setOwnFrame(const QImage &image) {
	if (m_own.tile) {
		forwardFrame(m_own, image);
	}
}

void WebcamArea::setOwnStatus(const QString &status) {
	if (m_own.tile) {
		forwardStatus(m_own, status);
	}
}

void WebcamArea::clearOwnFrame() {
	if (m_own.tile) {
		m_own.tile->clearFrame();
		if (m_own.window) {
			m_own.window->clearFrame();
		}
	}
}

void WebcamArea::setViewers(const QList< QPair< unsigned int, QString > > &viewers) {
	// The button clicked to revoke someone is among them.
	for (QToolButton *button : m_viewerButtons) {
		discard(button);
	}
	m_viewerButtons.clear();
	m_viewerCount = static_cast< int >(viewers.size());
	int position  = 1;
	for (const QPair< unsigned int, QString > &viewer : viewers) {
		auto *button = new QToolButton(m_viewersBox);
		button->setText(viewer.second + QStringLiteral("  ✕"));
		button->setToolTip(tr("Stop letting %1 watch your camera").arg(viewer.second));
		const unsigned int session = viewer.first;
		connect(button, &QToolButton::clicked, this, [this, session] { emit revokeRequested(session); });
		m_viewersRow->insertWidget(position++, button);
		m_viewerButtons << button;
	}
	updateShareBar();
}

void WebcamArea::setAsks(const QList< QPair< unsigned int, QString > > &asks) {
	// The button clicked to answer is among them.
	for (QWidget *widget : m_askWidgets) {
		discard(widget);
	}
	m_askWidgets.clear();
	int position = 1;
	for (const QPair< unsigned int, QString > &ask : asks) {
		const unsigned int session = ask.first;
		auto *name                 = new QLabel(ask.second, m_asksBox);
		auto *allow                = new QToolButton(m_asksBox);
		allow->setText(tr("Allow"));
		allow->setToolTip(tr("Let %1 watch your camera").arg(ask.second));
		auto *deny = new QToolButton(m_asksBox);
		deny->setText(tr("Deny"));
		deny->setToolTip(tr("Do not let %1 watch your camera").arg(ask.second));
		connect(allow, &QToolButton::clicked, this, [this, session] { emit askAnswered(session, true); });
		connect(deny, &QToolButton::clicked, this, [this, session] { emit askAnswered(session, false); });
		for (QWidget *widget :
			 { static_cast< QWidget * >(name), static_cast< QWidget * >(allow), static_cast< QWidget * >(deny) }) {
			m_asksRow->insertWidget(position++, widget);
			m_askWidgets << widget;
		}
	}
	m_asksBox->setVisible(m_sharing && !asks.isEmpty());
}

// -- cameras we watch ----------------------------------------------------------------------------

WebcamArea::Entry *WebcamArea::watchEntry(unsigned int session) {
	auto it = m_watches.find(session);
	return it == m_watches.end() ? nullptr : &it.value();
}

void WebcamArea::addWatch(unsigned int session, const QString &name) {
	if (m_watches.contains(session)) {
		return;
	}
	Entry entry;
	entry.title = tr("%1's camera").arg(name);
	entry.tile  = new WebcamTile(entry.title, tr("Stop Watching"), m_stage);
	connect(entry.tile, &WebcamTile::popOutRequested, this, [this, session] {
		if (Entry *e = watchEntry(session)) {
			popOut(*e, tr("Back into the camera area"));
		}
	});
	connect(entry.tile, &WebcamTile::stopRequested, this, [this, session] { emit stopWatchRequested(session); });
	m_watches.insert(session, entry);
	m_order << session;
	relayout();
}

void WebcamArea::removeWatch(unsigned int session) {
	Entry *entry = watchEntry(session);
	if (!entry) {
		return;
	}
	if (entry->window) {
		Global::get().s.qbaWebcamWindowGeometry = entry->window->saveGeometry();
		discard(entry->window);
	}
	discard(entry->tile);
	m_watches.remove(session);
	m_order.removeAll(session);
	relayout();
}

void WebcamArea::setWatchFrame(unsigned int session, const QImage &image) {
	if (Entry *entry = watchEntry(session)) {
		forwardFrame(*entry, image);
	}
}

void WebcamArea::setWatchStatus(unsigned int session, const QString &status) {
	if (Entry *entry = watchEntry(session)) {
		forwardStatus(*entry, status);
	}
}

bool WebcamArea::raiseWatch(unsigned int session) {
	Entry *entry = watchEntry(session);
	if (!entry || !entry->window) {
		return false;
	}
	entry->window->show();
	entry->window->raise();
	entry->window->activateWindow();
	return true;
}

bool WebcamArea::isPoppedOut(unsigned int session) const {
	const auto it = m_watches.constFind(session);
	return it != m_watches.constEnd() && it->window;
}

// -- pop-out windows -----------------------------------------------------------------------------

void WebcamArea::popOut(Entry &entry, const QString &closeLabel) {
	if (!entry.window) {
		entry.window = new WebcamWindow(entry.title, Global::get().s.qbaWebcamWindowGeometry, nullptr);
		entry.window->setCloseLabel(closeLabel);
		// Each window remembers the same place: step a further one aside instead of covering the
		// others exactly.
		int open = m_own.window ? 1 : 0;
		for (const Entry &other : m_watches) {
			open += other.window ? 1 : 0;
		}
		entry.window->move(entry.window->pos() - QPoint(32, 32) * (open - 1));
		if (!entry.tile->frame().isNull()) {
			entry.window->setFrame(entry.tile->frame());
		}
		entry.window->setStatus(entry.tile->status());
		WebcamTile *tile = entry.tile;
		connect(entry.window, &WebcamWindow::closedByUser, this, [this, tile] {
			if (m_own.tile == tile) {
				dock(m_own);
				return;
			}
			for (Entry &e : m_watches) {
				if (e.tile == tile) {
					dock(e);
					return;
				}
			}
		});
		relayout();
	}
	entry.window->show();
	entry.window->raise();
	entry.window->activateWindow();
}

void WebcamArea::dock(Entry &entry) {
	if (WebcamWindow *window = entry.window) {
		Global::get().s.qbaWebcamWindowGeometry = window->saveGeometry();
		entry.window                            = nullptr;
		discard(window);
	}
	relayout();
}

void WebcamArea::forwardFrame(Entry &entry, const QImage &image) {
	entry.tile->setFrame(image);
	if (entry.window) {
		entry.window->setFrame(image);
	}
}

void WebcamArea::forwardStatus(Entry &entry, const QString &status) {
	entry.tile->setStatus(status);
	if (entry.window) {
		entry.window->setStatus(status);
	}
}

// -- arranging -----------------------------------------------------------------------------------

bool WebcamArea::eventFilter(QObject *watched, QEvent *event) {
	if (watched == m_stage && event->type() == QEvent::Resize) {
		relayout();
	}
	return QWidget::eventFilter(watched, event);
}

void WebcamArea::relayout() {
	std::vector< WebcamTile * > grid;
	for (unsigned int session : m_order) {
		const Entry &entry = m_watches[session];
		entry.tile->setVisible(!entry.window);
		if (!entry.window) {
			grid.push_back(entry.tile);
		}
	}

	const bool ownHere = m_own.tile && !m_own.window;
	const OwnPlacement own =
		ownPlacement(layoutFrom(m_layout->currentIndex()), ownHere, static_cast< int >(grid.size()));
	if (own == OwnPlacement::Tile) {
		// Our own camera leads the grid, as in a call.
		m_own.tile->setFramed(false);
		grid.insert(grid.begin(), m_own.tile);
	}

	const std::vector< Rect > rects =
		gridRects(static_cast< int >(grid.size()), m_stage->width(), m_stage->height(), SPACING);
	for (std::size_t i = 0; i < grid.size() && i < rects.size(); ++i) {
		grid[i]->setGeometry(rects[i].x, rects[i].y, rects[i].width, rects[i].height);
		grid[i]->show();
	}

	if (m_own.tile) {
		if (own == OwnPlacement::Overlay) {
			const QSize picture = m_own.tile->frame().isNull() ? QSize(16, 9) : m_own.tile->frame().size();
			const Rect r        = overlayRect(m_stage->width(), m_stage->height(), picture.width(), picture.height());
			m_own.tile->setGeometry(r.x, r.y, r.width, r.height);
			m_own.tile->setFramed(true);
			m_own.tile->show();
			m_own.tile->raise();
		} else if (own == OwnPlacement::None) {
			m_own.tile->hide();
		}
	}

	m_hint->setGeometry(m_stage->rect().adjusted(16, 16, -16, -16));
	m_hint->setVisible(grid.empty() && own != OwnPlacement::Overlay);
}
