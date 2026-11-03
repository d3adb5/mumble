// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_WEBCAMLAYOUT_H_
#define MUMBLE_MUMBLE_WEBCAMLAYOUT_H_

#include <algorithm>
#include <cmath>
#include <vector>

/// Where the cameras go in the camera area: a grid of equal tiles, the own camera as a small
/// picture over the others, or the own camera left out. Pure geometry, without Qt.
namespace Mumble {
namespace Webcam {
	/// The layouts the camera area offers (stored in the settings as these numbers).
	enum class Layout { Grid = 0, PictureInPicture = 1, HideOwn = 2 };

	inline Layout layoutFrom(int value) {
		switch (value) {
			case 1:
				return Layout::PictureInPicture;
			case 2:
				return Layout::HideOwn;
			default:
				return Layout::Grid;
		}
	}

	/// Where the own camera is shown.
	enum class OwnPlacement { None, Tile, Overlay };

	/// \p hasOwn: we share and our camera is not popped out of the area. \p others: the watched
	/// cameras still in the area. Picture in picture needs something to sit on: alone, the own
	/// camera is an ordinary tile.
	inline OwnPlacement ownPlacement(Layout layout, bool hasOwn, int others) {
		if (!hasOwn || layout == Layout::HideOwn) {
			return OwnPlacement::None;
		}
		if (layout == Layout::PictureInPicture && others > 0) {
			return OwnPlacement::Overlay;
		}
		return OwnPlacement::Tile;
	}

	struct Rect {
		int x      = 0;
		int y      = 0;
		int width  = 0;
		int height = 0;

		bool operator==(const Rect &other) const {
			return x == other.x && y == other.y && width == other.width && height == other.height;
		}
	};

	/// The number of columns that gives \p count tiles of shape \p aspect the largest size in
	/// \p width x \p height with \p spacing between them; the fewer columns on a tie.
	inline int gridColumns(int count, int width, int height, int spacing, double aspect = 16.0 / 9.0) {
		if (count <= 0) {
			return 0;
		}
		int best         = 1;
		double bestWidth = -1.0;
		for (int columns = 1; columns <= count; ++columns) {
			const int rows        = (count + columns - 1) / columns;
			const double byWidth  = static_cast< double >(width - (columns - 1) * spacing) / columns;
			const double byHeight = static_cast< double >(height - (rows - 1) * spacing) / rows * aspect;
			const double tile     = std::min(byWidth, byHeight);
			if (tile > bestWidth + 0.5) {
				bestWidth = tile;
				best      = columns;
			}
		}
		return best;
	}

	/// \p count equal tiles of shape \p aspect, as large as fit in \p width x \p height, in rows
	/// filled left to right; the grid is centred, and so is a last row that is not full (as video
	/// calls lay out their participants).
	inline std::vector< Rect > gridRects(int count, int width, int height, int spacing, double aspect = 16.0 / 9.0) {
		std::vector< Rect > out;
		if (count <= 0 || width <= 0 || height <= 0) {
			return out;
		}
		const int columns     = gridColumns(count, width, height, spacing, aspect);
		const int rows        = (count + columns - 1) / columns;
		const double byWidth  = static_cast< double >(width - (columns - 1) * spacing) / columns;
		const double byHeight = static_cast< double >(height - (rows - 1) * spacing) / rows * aspect;
		const int tileWidth   = std::max(1, static_cast< int >(std::floor(std::min(byWidth, byHeight))));
		const int tileHeight  = std::max(1, static_cast< int >(std::floor(tileWidth / aspect)));

		const int gridHeight = rows * tileHeight + (rows - 1) * spacing;
		const int top        = std::max(0, (height - gridHeight) / 2);
		for (int i = 0; i < count; ++i) {
			const int row      = i / columns;
			const int inRow    = std::min(columns, count - row * columns);
			const int rowWidth = inRow * tileWidth + (inRow - 1) * spacing;
			const int left     = std::max(0, (width - rowWidth) / 2);
			Rect rect;
			rect.x      = left + (i % columns) * (tileWidth + spacing);
			rect.y      = top + row * (tileHeight + spacing);
			rect.width  = tileWidth;
			rect.height = tileHeight;
			out.push_back(rect);
		}
		return out;
	}

	/// The own camera over the others: in the bottom right corner, \p fraction of the area's width
	/// (at least \p minWidth, never more than half the area), in the picture's shape.
	inline Rect overlayRect(int width, int height, int pictureWidth, int pictureHeight, double fraction = 0.25,
							int margin = 8, int minWidth = 120) {
		const double aspect = static_cast< double >(std::max(1, pictureWidth)) / std::max(1, pictureHeight);
		const int widest    = std::max(1, std::min(width / 2, width - 2 * margin));
		const int tallest   = std::max(1, std::min(height / 2, height - 2 * margin));
		double w = std::min(static_cast< double >(widest), std::max(static_cast< double >(minWidth), width * fraction));
		double h = w / aspect;
		if (h > tallest) {
			h = tallest;
			w = h * aspect;
		}
		Rect rect;
		rect.width  = std::max(1, static_cast< int >(std::lround(w)));
		rect.height = std::max(1, static_cast< int >(std::lround(h)));
		rect.x      = std::max(0, width - margin - rect.width);
		rect.y      = std::max(0, height - margin - rect.height);
		return rect;
	}
} // namespace Webcam
} // namespace Mumble

#endif
