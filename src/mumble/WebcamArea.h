// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_WEBCAMAREA_H_
#define MUMBLE_MUMBLE_WEBCAMAREA_H_

#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QPair>
#include <QtCore/QPointer>
#include <QtGui/QImage>
#include <QtWidgets/QWidget>

class QComboBox;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QToolButton;
class WebcamWindow;

/// One camera in the camera area: the picture, its owner's name on hover, and buttons to pop it
/// out into a window of its own and to stop it.
class WebcamTile : public QWidget {
private:
	Q_OBJECT
	Q_DISABLE_COPY(WebcamTile)

public:
	WebcamTile(const QString &title, const QString &stopLabel, QWidget *parent = nullptr);

	void setFrame(const QImage &image);
	void clearFrame();
	void setStatus(const QString &status);
	/// Draws an edge around the picture (for one shown over another).
	void setFramed(bool framed);
	const QImage &frame() const { return m_frame; }
	const QString &status() const { return m_status; }

signals:
	void popOutRequested();
	void stopRequested();

protected:
	void paintEvent(QPaintEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;
	void enterEvent(QEnterEvent *event) override;
	void leaveEvent(QEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

private:
	QString m_title;
	QString m_stopLabel;
	QString m_status;
	QImage m_frame;
	QToolButton *m_popOut = nullptr;
	QToolButton *m_stop   = nullptr;
	bool m_hovered        = false;
	bool m_framed         = false;
};

/// The camera area of the main window: our own share (start and stop, who watches it, each with
/// a stop) and every camera we watch, side by side. Any camera can be popped out into a floating
/// window and goes back into the area when that window closes; closing a window or the area never
/// ends a watch, only a tile's stop does (as in Mumla). How the cameras are arranged is a setting:
/// a grid, our own camera as picture in picture, or our own camera hidden.
class WebcamArea : public QWidget {
private:
	Q_OBJECT
	Q_DISABLE_COPY(WebcamArea)

public:
	explicit WebcamArea(QWidget *parent = nullptr);
	~WebcamArea() override;

	// -- our own camera ------------------------------------------------------------------------
	void setSharing(bool sharing);
	void setOwnFrame(const QImage &image);
	void setOwnStatus(const QString &status);
	void clearOwnFrame();
	/// Who watches our camera: session and name, in the order shown.
	void setViewers(const QList< QPair< unsigned int, QString > > &viewers);
	/// Who asks to watch it and waits for an answer: session and name.
	void setAsks(const QList< QPair< unsigned int, QString > > &asks);

	// -- cameras we watch ----------------------------------------------------------------------
	void addWatch(unsigned int session, const QString &name);
	void removeWatch(unsigned int session);
	void setWatchFrame(unsigned int session, const QImage &image);
	void setWatchStatus(unsigned int session, const QString &status);
	/// Raises the camera's own window when it is popped out. False when it is in the area (which
	/// the caller shows instead).
	bool raiseWatch(unsigned int session);
	bool isPoppedOut(unsigned int session) const;

	/// Applies the layout setting (it may have been changed in the settings dialog).
	void reloadLayout();

signals:
	void startSharingRequested();
	void stopSharingRequested();
	void revokeRequested(unsigned int session);
	void stopWatchRequested(unsigned int session);
	void askAnswered(unsigned int session, bool allow);

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	struct Entry {
		QString title;
		WebcamTile *tile = nullptr;
		QPointer< WebcamWindow > window;
	};

	Entry *watchEntry(unsigned int session);
	void popOut(Entry &entry, const QString &closeLabel);
	/// Back from its window into the area; the camera keeps playing.
	void dock(Entry &entry);
	void forwardFrame(Entry &entry, const QImage &image);
	void forwardStatus(Entry &entry, const QString &status);
	void relayout();
	void updateShareBar();

	QLabel *m_shareLabel       = nullptr;
	QPushButton *m_shareButton = nullptr;
	QComboBox *m_layout        = nullptr;
	QWidget *m_viewersBox      = nullptr;
	QHBoxLayout *m_viewersRow  = nullptr;
	QLabel *m_viewersLabel     = nullptr;
	QList< QToolButton * > m_viewerButtons;
	QWidget *m_asksBox     = nullptr;
	QHBoxLayout *m_asksRow = nullptr;
	QList< QWidget * > m_askWidgets;
	QWidget *m_stage = nullptr;
	QLabel *m_hint   = nullptr;

	bool m_sharing    = false;
	int m_viewerCount = 0;
	Entry m_own;
	QHash< unsigned int, Entry > m_watches;
	/// The order the watched cameras were opened in, which the grid keeps.
	QList< unsigned int > m_order;
};

#endif
