// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore>
#include <QtTest>

#include "WebcamLayout.h"

using namespace Mumble::Webcam;

namespace {
Rect rect(int x, int y, int width, int height) {
	Rect r;
	r.x      = x;
	r.y      = y;
	r.width  = width;
	r.height = height;
	return r;
}

bool overlaps(const Rect &a, const Rect &b) {
	return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}
} // namespace

class TestWebcamLayout : public QObject {
	Q_OBJECT
private slots:
	void layoutFromSettings();
	void ownPlacementPerLayout();
	void columnsFollowTheAreaShape();
	void singleTileFillsTheArea();
	void lastRowIsCentred();
	void tilesStayInsideAndApart();
	void degenerateAreas();
	void overlayInTheCorner();
	void overlayKeepsPortraitShape();
};

void TestWebcamLayout::layoutFromSettings() {
	QCOMPARE(layoutFrom(0), Layout::Grid);
	QCOMPARE(layoutFrom(1), Layout::PictureInPicture);
	QCOMPARE(layoutFrom(2), Layout::HideOwn);
	// Anything unknown (an older or newer settings file) falls back to the grid.
	QCOMPARE(layoutFrom(-1), Layout::Grid);
	QCOMPARE(layoutFrom(7), Layout::Grid);
}

void TestWebcamLayout::ownPlacementPerLayout() {
	QCOMPARE(ownPlacement(Layout::Grid, true, 0), OwnPlacement::Tile);
	QCOMPARE(ownPlacement(Layout::Grid, true, 3), OwnPlacement::Tile);
	QCOMPARE(ownPlacement(Layout::PictureInPicture, true, 2), OwnPlacement::Overlay);
	// Alone, picture in picture has nothing to sit on: an ordinary tile.
	QCOMPARE(ownPlacement(Layout::PictureInPicture, true, 0), OwnPlacement::Tile);
	QCOMPARE(ownPlacement(Layout::HideOwn, true, 0), OwnPlacement::None);
	QCOMPARE(ownPlacement(Layout::HideOwn, true, 2), OwnPlacement::None);
	// Not sharing, or popped out of the area: nothing in it.
	QCOMPARE(ownPlacement(Layout::Grid, false, 2), OwnPlacement::None);
	QCOMPARE(ownPlacement(Layout::PictureInPicture, false, 2), OwnPlacement::None);
}

void TestWebcamLayout::columnsFollowTheAreaShape() {
	// Two cameras side by side in a wide area, stacked in a tall one.
	QCOMPARE(gridColumns(2, 1600, 450, 0), 2);
	QCOMPARE(gridColumns(2, 400, 900, 0), 1);
	// Four in a 16:9 area: two by two.
	QCOMPARE(gridColumns(4, 1600, 900, 0), 2);
	// Three in a very wide strip: one row.
	QCOMPARE(gridColumns(3, 1800, 300, 0), 3);
	QCOMPARE(gridColumns(0, 800, 600, 0), 0);
}

void TestWebcamLayout::singleTileFillsTheArea() {
	// The area's own shape: the tile is all of it.
	const std::vector< Rect > exact = gridRects(1, 1600, 900, 8);
	QCOMPARE(exact.size(), std::size_t(1));
	QVERIFY(exact[0] == rect(0, 0, 1600, 900));
	// Wider than 16:9: as tall as the area, centred across.
	const std::vector< Rect > wide = gridRects(1, 2000, 900, 8);
	QVERIFY(wide[0] == rect(200, 0, 1600, 900));
}

void TestWebcamLayout::lastRowIsCentred() {
	// Three in a 16:9 area: two above, the third centred below.
	const std::vector< Rect > rects = gridRects(3, 1600, 900, 0);
	QCOMPARE(rects.size(), std::size_t(3));
	QVERIFY(rects[0] == rect(0, 0, 800, 450));
	QVERIFY(rects[1] == rect(800, 0, 800, 450));
	QVERIFY(rects[2] == rect(400, 450, 800, 450));
}

void TestWebcamLayout::tilesStayInsideAndApart() {
	for (int count = 1; count <= 9; ++count) {
		for (const auto &area : { std::make_pair(640, 360), std::make_pair(300, 800), std::make_pair(1920, 400) }) {
			const std::vector< Rect > rects = gridRects(count, area.first, area.second, 6);
			QCOMPARE(rects.size(), std::size_t(count));
			for (std::size_t i = 0; i < rects.size(); ++i) {
				QVERIFY(rects[i].x >= 0 && rects[i].y >= 0);
				QVERIFY(rects[i].x + rects[i].width <= area.first);
				QVERIFY(rects[i].y + rects[i].height <= area.second);
				QCOMPARE(rects[i].width, rects[0].width);
				for (std::size_t j = i + 1; j < rects.size(); ++j) {
					QVERIFY(!overlaps(rects[i], rects[j]));
				}
			}
		}
	}
}

void TestWebcamLayout::degenerateAreas() {
	QVERIFY(gridRects(2, 0, 100, 4).empty());
	QVERIFY(gridRects(0, 100, 100, 4).empty());
	// Too small for the spacing: still one pixel per tile, never negative.
	const std::vector< Rect > tiny = gridRects(4, 3, 3, 8);
	QCOMPARE(tiny.size(), std::size_t(4));
	for (const Rect &r : tiny) {
		QVERIFY(r.width >= 1 && r.height >= 1);
	}
}

void TestWebcamLayout::overlayInTheCorner() {
	const Rect r = overlayRect(1600, 900, 864, 480);
	// A quarter of the width, in the picture's shape, 8 px from the bottom right.
	QCOMPARE(r.width, 400);
	QCOMPARE(r.height, 222);
	QCOMPARE(r.x, 1600 - 8 - 400);
	QCOMPARE(r.y, 900 - 8 - 222);
	// A small area still gets a usable overlay, never more than half of it.
	const Rect small = overlayRect(200, 150, 16, 9);
	QVERIFY(small.width <= 100);
	QVERIFY(small.height <= 75);
	QVERIFY(small.x + small.width <= 200 && small.y + small.height <= 150);
}

void TestWebcamLayout::overlayKeepsPortraitShape() {
	// A phone held upright: limited by half the height, still 9:16.
	const Rect r = overlayRect(1600, 400, 480, 640);
	QCOMPARE(r.height, 200);
	QCOMPARE(r.width, 150);
	QVERIFY(r.x + r.width <= 1600 && r.y + r.height <= 400);
}

QTEST_MAIN(TestWebcamLayout)
#include "TestWebcamLayout.moc"
