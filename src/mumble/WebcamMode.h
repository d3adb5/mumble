// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_WEBCAMMODE_H_
#define MUMBLE_MUMBLE_WEBCAMMODE_H_

#include <algorithm>
#include <cmath>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <tuple>
#include <vector>

// Choosing the mode a webcam is opened in, from the caps GStreamer reports for it. Ported from
// teevee-camera (desktop_share/camera.py), whose desktop camera role Mumble takes over. Kept
// free of Qt and of GStreamer, reading caps as the strings GStreamer prints, so it can be unit
// tested with no camera.
namespace Mumble {
namespace Webcam {

	/// One thing a camera can produce: a format, a size and a frame rate.
	struct Mode {
		bool jpeg  = false;
		int width  = 0;
		int height = 0;
		int fpsNum = 0;
		int fpsDen = 1;
		/// The raw format (e.g. YUY2), empty for MJPG.
		std::string format;

		double fps() const { return fpsDen > 0 ? static_cast< double >(fpsNum) / fpsDen : 0.0; }

		bool operator==(const Mode &other) const {
			return std::tie(jpeg, format, width, height, fpsNum, fpsDen)
				   == std::tie(other.jpeg, other.format, other.width, other.height, other.fpsNum, other.fpsDen);
		}
		bool operator<(const Mode &other) const {
			return std::tie(jpeg, format, width, height, fpsNum, fpsDen)
				   < std::tie(other.jpeg, other.format, other.width, other.height, other.fpsNum, other.fpsDen);
		}

		/// Caps that ask the camera for exactly this mode.
		std::string caps() const {
			std::string out = jpeg ? "image/jpeg" : "video/x-raw,format=" + format;
			out += ",width=" + std::to_string(width) + ",height=" + std::to_string(height)
				   + ",framerate=" + std::to_string(fpsNum) + "/" + std::to_string(fpsDen);
			return out;
		}
	};

	/// Raw formats videoconvert takes from a webcam. DMA_DRM is PipeWire's dmabuf caps for the
	/// same frames, which v4l2src does not produce.
	inline bool isUsableRawFormat(const std::string &format) {
		static const std::set< std::string > formats = { "YUY2", "YVYU", "UYVY", "NV12", "NV21", "I420", "YV12",
														 "RGB",  "BGR",  "RGBx", "BGRx", "xRGB", "xBGR", "GRAY8" };
		return formats.count(format) > 0;
	}

	/// The modes in one caps structure, as GStreamer prints it. A frame-rate range yields its
	/// top end only.
	inline std::vector< Mode > parseCapsStructure(const std::string &structure) {
		static const std::regex widthRe(R"(\bwidth=\(int\)(\d+))");
		static const std::regex heightRe(R"(\bheight=\(int\)(\d+))");
		static const std::regex formatRe(R"(\bformat=\(string\)([A-Za-z0-9_]+))");
		static const std::regex fpsRe(R"(\bframerate=\(fraction\)(\{[^}]*\}|\[[^\]]*\]|[0-9]+/[0-9]+))");
		static const std::regex fractionRe(R"((\d+)/(\d+))");

		const std::size_t start = structure.find_first_not_of(" \t\n");
		if (start == std::string::npos) {
			return {};
		}
		const std::string text = structure.substr(start);

		Mode base;
		std::smatch match;
		if (text.rfind("image/jpeg", 0) == 0) {
			base.jpeg = true;
		} else if (text.rfind("video/x-raw", 0) == 0) {
			const std::string mediaType = text.substr(0, text.find(','));
			if (mediaType.find("memory:") != std::string::npos || !std::regex_search(text, match, formatRe)
				|| !isUsableRawFormat(match[1].str())) {
				return {};
			}
			base.format = match[1].str();
		} else {
			return {};
		}

		std::smatch width, height, rates;
		if (!std::regex_search(text, width, widthRe) || !std::regex_search(text, height, heightRe)
			|| !std::regex_search(text, rates, fpsRe)) {
			return {};
		}
		base.width  = std::stoi(width[1].str());
		base.height = std::stoi(height[1].str());
		if (base.width <= 0 || base.height <= 0) {
			return {};
		}

		const std::string rateText = rates[1].str();
		std::vector< std::pair< int, int > > fractions;
		for (auto it = std::sregex_iterator(rateText.begin(), rateText.end(), fractionRe); it != std::sregex_iterator();
			 ++it) {
			const int num = std::stoi((*it)[1].str());
			const int den = std::stoi((*it)[2].str());
			if (num > 0 && den > 0) {
				fractions.emplace_back(num, den);
			}
		}
		if (rateText.front() == '[' && !fractions.empty()) {
			const auto top = std::max_element(fractions.begin(), fractions.end(), [](const auto &a, const auto &b) {
				return static_cast< double >(a.first) / a.second < static_cast< double >(b.first) / b.second;
			});
			fractions      = { *top };
		}

		std::vector< Mode > modes;
		for (const auto &fraction : fractions) {
			Mode mode   = base;
			mode.fpsNum = fraction.first;
			mode.fpsDen = fraction.second;
			modes.push_back(mode);
		}
		return modes;
	}

	/// Every distinct mode across a device's caps structures, in the order first seen.
	inline std::vector< Mode > parseCaps(const std::vector< std::string > &structures) {
		std::vector< Mode > modes;
		std::set< Mode > seen;
		for (const std::string &structure : structures) {
			for (const Mode &mode : parseCapsStructure(structure)) {
				if (seen.insert(mode).second) {
					modes.push_back(mode);
				}
			}
		}
		return modes;
	}

	/// The mode to open the camera in, for a wanted height (0: the most it can do) and frame
	/// rate. The smallest mode at least as tall as asked that keeps the frame rate, nearest 16:9
	/// among equals, raw before MJPG among those (no decode). Nothing upscales afterwards, so
	/// taller is fine: the scaler brings it down. When no mode keeps the rate at that height, the
	/// frame rate gives way first (a sharp 15 fps face is worth more than a soft 30), and when
	/// nothing is tall enough, the tallest mode there is. Nothing if there are no modes.
	inline std::optional< Mode > chooseMode(const std::vector< Mode > &modes, int height, int fps) {
		if (modes.empty()) {
			return std::nullopt;
		}

		// Nearness to 16:9, rounded so near-equal shapes tie, then raw before MJPG.
		auto shape = [](const Mode &mode) {
			const double wide = std::abs(static_cast< double >(mode.width) / mode.height - 16.0 / 9.0);
			return std::make_pair(std::round(wide * 100.0), mode.jpeg ? 1 : 0);
		};
		auto keepsRate = [fps](const Mode &mode) { return mode.fps() >= fps - 0.01; };
		auto pickMin   = [](const std::vector< Mode > &pool, auto key) {
			return *std::min_element(pool.begin(), pool.end(),
									 [&](const Mode &a, const Mode &b) { return key(a) < key(b); });
		};
		auto filter = [](const std::vector< Mode > &pool, auto predicate) {
			std::vector< Mode > out;
			std::copy_if(pool.begin(), pool.end(), std::back_inserter(out), predicate);
			return out;
		};
		auto tallestOf = [](const std::vector< Mode > &pool) {
			return std::max_element(pool.begin(), pool.end(),
									[](const Mode &a, const Mode &b) { return a.height < b.height; })
				->height;
		};

		if (height <= 0) {
			std::vector< Mode > pool = filter(modes, keepsRate);
			if (pool.empty()) {
				pool = modes;
			}
			const int tallest = tallestOf(pool);
			return pickMin(filter(pool, [tallest](const Mode &m) { return m.height == tallest; }),
						   [&](const Mode &m) { return std::make_tuple(shape(m), -m.fps()); });
		}

		const std::vector< Mode > tall    = filter(modes, [height](const Mode &m) { return m.height >= height; });
		const std::vector< Mode > keeping = filter(tall, keepsRate);
		if (!keeping.empty()) {
			return pickMin(keeping, [&](const Mode &m) { return std::make_tuple(m.height, shape(m), m.fps()); });
		}
		if (!tall.empty()) {
			double best = 0.0;
			for (const Mode &m : tall) {
				best = std::max(best, m.fps());
			}
			return pickMin(filter(tall, [best](const Mode &m) { return m.fps() == best; }),
						   [&](const Mode &m) { return std::make_tuple(m.height, shape(m)); });
		}
		const int tallest = tallestOf(modes);
		return pickMin(filter(modes, [tallest](const Mode &m) { return m.height == tallest; }),
					   [&](const Mode &m) { return std::make_tuple(-m.fps(), shape(m)); });
	}

	/// The /dev/videoN in a device's printed properties, whichever provider listed it: PipeWire's
	/// hides the v4l2 one and carries api.v4l2.path.
	inline std::optional< std::string > devicePath(const std::string &properties) {
		// Spelt out rather than raw strings: moc cannot read a raw string holding a quote.
		static const std::regex keyed[] = { std::regex("api\\.v4l2\\.path=\\(string\\)\"?([^\",;]+)"),
											std::regex("device\\.path=\\(string\\)\"?([^\",;]+)") };
		for (const std::regex &re : keyed) {
			std::smatch match;
			if (std::regex_search(properties, match, re)) {
				return std::regex_replace(match[1].str(), std::regex("\\\\ "), " ");
			}
		}
		static const std::regex objectPath("object\\.path=\\(string\\)v4l2:([^\",;]+)");
		std::smatch match;
		if (std::regex_search(properties, match, objectPath)) {
			return match[1].str();
		}
		return std::nullopt;
	}

	/// What the sender encodes to: a preset or custom values from the settings.
	struct Quality {
		int width;
		int height;
		int fps;
		int bitrateKbps;
	};

	/// The presets of protocol v1 (data saver to high), in the order the settings list them.
	inline const std::vector< Quality > &qualityPresets() {
		static const std::vector< Quality > presets = {
			{ 320, 240, 12, 150 }, { 480, 360, 15, 300 }, { 640, 480, 24, 600 }, { 1280, 720, 30, 1500 }
		};
		return presets;
	}

	/// The size to encode at for a camera mode and a wanted size: the wanted height (never more
	/// than the camera gives), with the camera's aspect, both even as H.264 needs.
	inline std::pair< int, int > outputSize(const Mode &mode, int wantedHeight) {
		const int height = std::max(2, std::min(wantedHeight > 0 ? wantedHeight : mode.height, mode.height)) & ~1;
		// The nearest even width, so the aspect drifts as little as it can.
		const double exact = static_cast< double >(mode.width) * height / mode.height;
		const int width    = std::max(2, 2 * static_cast< int >(std::lround(exact / 2.0)));
		return { width, height };
	}

} // namespace Webcam
} // namespace Mumble

#endif
