// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore>
#include <QtTest>

#include "WebcamMode.h"

using namespace Mumble::Webcam;

namespace {
// A Logitech C920 through PipeWire's provider, as teevee-camera recorded it (2026-10-16). The
// choices matter because a wrong one is a working share that is merely bad: raw YUYV at
// 1280x720 is 10 fps on that camera, MJPG is 30.
const std::string RATES               = "{ 30/1, 24/1, 20/1, 15/1, 10/1, 15/2, 5/1 }";
const std::vector< std::string > C920 = {
	"video/x-raw, drm-format=(string)YUYV, format=(string)DMA_DRM, interlace-mode=(string)progressive, "
	"width=(int)640, height=(int)480, framerate=(fraction)"
		+ RATES + ", colorimetry=(string)2:4:5:1;",
	"video/x-raw, format=(string)YUY2, interlace-mode=(string)progressive, width=(int)640, height=(int)360, "
	"framerate=(fraction)"
		+ RATES + ";",
	"video/x-raw, format=(string)YUY2, interlace-mode=(string)progressive, width=(int)640, height=(int)480, "
	"framerate=(fraction)"
		+ RATES + ";",
	"video/x-raw, format=(string)YUY2, interlace-mode=(string)progressive, width=(int)864, height=(int)480, "
	"framerate=(fraction){ 20/1, 15/1, 10/1 };",
	"video/x-raw, format=(string)YUY2, interlace-mode=(string)progressive, width=(int)1280, height=(int)720, "
	"framerate=(fraction){ 10/1, 15/2, 5/1 };",
	"video/x-raw, format=(string)YUY2, interlace-mode=(string)progressive, width=(int)1920, height=(int)1080, "
	"framerate=(fraction)5/1;",
	"image/jpeg, width=(int)640, height=(int)480, framerate=(fraction)" + RATES + ";",
	"image/jpeg, width=(int)864, height=(int)480, framerate=(fraction)" + RATES + ";",
	"image/jpeg, width=(int)960, height=(int)720, framerate=(fraction)" + RATES + ";",
	"image/jpeg, width=(int)1280, height=(int)720, framerate=(fraction)" + RATES + ";",
	"image/jpeg, width=(int)1920, height=(int)1080, framerate=(fraction)" + RATES + ";",
};

std::vector< Mode > rawOnly(const std::vector< Mode > &modes) {
	std::vector< Mode > out;
	for (const Mode &mode : modes) {
		if (!mode.jpeg) {
			out.push_back(mode);
		}
	}
	return out;
}
} // namespace

/// Unit tests for choosing a webcam's mode (ported from teevee-camera's tests/test_camera.py).
class TestWebcamMode : public QObject {
	Q_OBJECT
private slots:
	void readsAFrameRateList();
	void skipsPipewiresDmabufCaps();
	void readsASingleRateAndMjpg();
	void aRangeGivesItsTop();
	void capsAskForExactlyTheMode();
	void duplicatesCollapse();
	void takesMjpgWhenRawCannotKeepTheRate();
	void wideBeatsSquareAtTheSameHeight();
	void rawIsPreferredWhenItKeepsUp();
	void p480at15IsRawAndWide();
	void sourceIsTheTallestThatKeepsTheRate();
	void theRateGivesWayBeforeTheSize();
	void tooTallARequestTakesTheTallest();
	void noModesIsNothing();
	void devicePathFromPipewireAndV4l2();
	void outputSizeKeepsAspectAndIsEven();
	void windowKeepsItsAreaInThePicturesShape();
	void windowStaysOnTheScreenAndAboveTheMinimum();
};

void TestWebcamMode::readsAFrameRateList() {
	const std::vector< Mode > modes = parseCapsStructure(C920[1]);
	std::set< double > rates;
	for (const Mode &mode : modes) {
		rates.insert(mode.fps());
		QVERIFY(!mode.jpeg);
		QCOMPARE(mode.format, std::string("YUY2"));
	}
	QCOMPARE(rates, (std::set< double >{ 30, 24, 20, 15, 10, 7.5, 5 }));
}

void TestWebcamMode::skipsPipewiresDmabufCaps() {
	// v4l2src never produces DMA_DRM; asking for it fails at negotiation.
	QVERIFY(parseCapsStructure(C920[0]).empty());
}

void TestWebcamMode::readsASingleRateAndMjpg() {
	const std::vector< Mode > modes = parseCapsStructure(C920[5]);
	QCOMPARE(modes.size(), std::size_t(1));
	QCOMPARE(modes[0].width, 1920);
	QCOMPARE(modes[0].height, 1080);
	QCOMPARE(modes[0].fps(), 5.0);
	QVERIFY(parseCapsStructure(C920.back())[0].jpeg);
}

void TestWebcamMode::aRangeGivesItsTop() {
	const std::vector< Mode > modes = parseCapsStructure(
		"video/x-raw, format=(string)NV12, width=(int)1280, height=(int)720, framerate=(fraction)[ 1/1, 60/1 ]");
	QCOMPARE(modes.size(), std::size_t(1));
	QCOMPARE(modes[0].fps(), 60.0);
}

void TestWebcamMode::capsAskForExactlyTheMode() {
	const std::vector< Mode > modes  = parseCaps(C920);
	const std::optional< Mode > mode = chooseMode(modes, 480, 15);
	QVERIFY(mode);
	QCOMPARE(mode->caps(), std::string("video/x-raw,format=YUY2,width=864,height=480,framerate=15/1"));

	Mode jpeg;
	jpeg.jpeg   = true;
	jpeg.width  = 1280;
	jpeg.height = 720;
	jpeg.fpsNum = 30;
	QCOMPARE(jpeg.caps(), std::string("image/jpeg,width=1280,height=720,framerate=30/1"));
}

void TestWebcamMode::duplicatesCollapse() {
	const std::vector< Mode > modes = parseCaps({ C920[1], C920[1] });
	QCOMPARE(modes.size(), parseCapsStructure(C920[1]).size());
}

void TestWebcamMode::takesMjpgWhenRawCannotKeepTheRate() {
	const std::optional< Mode > mode = chooseMode(parseCaps(C920), 720, 30);
	QVERIFY(mode);
	QVERIFY(mode->jpeg);
	QCOMPARE(mode->width, 1280);
	QCOMPARE(mode->height, 720);
	QCOMPARE(mode->fps(), 30.0);
}

void TestWebcamMode::wideBeatsSquareAtTheSameHeight() {
	// 960x720 is MJPG at 30 too; a 4:3 picture is not what was asked for.
	QCOMPARE(chooseMode(parseCaps(C920), 720, 30)->width, 1280);
}

void TestWebcamMode::rawIsPreferredWhenItKeepsUp() {
	const std::optional< Mode > mode = chooseMode(parseCaps(C920), 360, 30);
	QVERIFY(mode);
	QVERIFY(!mode->jpeg);
	QCOMPARE(mode->width, 640);
	QCOMPARE(mode->height, 360);
}

void TestWebcamMode::p480at15IsRawAndWide() {
	const std::optional< Mode > mode = chooseMode(parseCaps(C920), 480, 15);
	QVERIFY(mode);
	QVERIFY(!mode->jpeg);
	QCOMPARE(mode->width, 864);
	QCOMPARE(mode->height, 480);
}

void TestWebcamMode::sourceIsTheTallestThatKeepsTheRate() {
	const std::optional< Mode > mode = chooseMode(parseCaps(C920), 0, 30);
	QVERIFY(mode);
	QVERIFY(mode->jpeg);
	QCOMPARE(mode->height, 1080);
	QCOMPARE(mode->fps(), 30.0);
}

void TestWebcamMode::theRateGivesWayBeforeTheSize() {
	const std::optional< Mode > mode = chooseMode(rawOnly(parseCaps(C920)), 720, 30);
	QVERIFY(mode);
	QCOMPARE(mode->height, 720);
	QCOMPARE(mode->fps(), 10.0);
}

void TestWebcamMode::tooTallARequestTakesTheTallest() {
	QCOMPARE(chooseMode(parseCaps(C920), 1440, 30)->height, 1080);
}

void TestWebcamMode::noModesIsNothing() {
	QVERIFY(!chooseMode({}, 720, 30));
}

void TestWebcamMode::devicePathFromPipewireAndV4l2() {
	QCOMPARE(devicePath("api.v4l2.path=(string)/dev/video2, device.api=(string)v4l2"),
			 std::optional< std::string >("/dev/video2"));
	QCOMPARE(devicePath("udev-probed=(boolean)true, device.path=(string)/dev/video0;"),
			 std::optional< std::string >("/dev/video0"));
	QCOMPARE(devicePath("object.path=(string)v4l2:/dev/video4, node.name=x"),
			 std::optional< std::string >("/dev/video4"));
	QVERIFY(!devicePath("device.api=(string)libcamera"));
}

void TestWebcamMode::outputSizeKeepsAspectAndIsEven() {
	Mode mode;
	mode.width  = 1280;
	mode.height = 720;
	QCOMPARE(outputSize(mode, 480), std::make_pair(854, 480));
	QCOMPARE(outputSize(mode, 240), std::make_pair(426, 240));
	// Never more than the camera gives.
	QCOMPARE(outputSize(mode, 1080), std::make_pair(1280, 720));
	mode.width  = 640;
	mode.height = 480;
	QCOMPARE(outputSize(mode, 360), std::make_pair(480, 360));
	QCOMPARE(outputSize(mode, 0), std::make_pair(640, 480));
}

void TestWebcamMode::windowKeepsItsAreaInThePicturesShape() {
	// The same shape keeps the size; a nearly equal one keeps the area.
	QCOMPARE(windowSizeFor(480, 270, 864, 480, 160, 90, 1920, 1080), std::make_pair(483, 268));
	QCOMPARE(windowSizeFor(480, 270, 1280, 720, 160, 90, 1920, 1080), std::make_pair(480, 270));
	// A portrait share gets about as many pixels, not a strip of the landscape window.
	QCOMPARE(windowSizeFor(480, 270, 480, 640, 160, 90, 1920, 1080), std::make_pair(312, 416));
	// And back.
	QCOMPARE(windowSizeFor(312, 416, 1280, 720, 160, 90, 1920, 1080), std::make_pair(480, 270));
}

void TestWebcamMode::windowStaysOnTheScreenAndAboveTheMinimum() {
	// Too tall for the screen: shrunk with its shape.
	QCOMPARE(windowSizeFor(1600, 900, 480, 640, 160, 90, 1920, 1080), std::make_pair(810, 1080));
	// No screen known: no upper bound.
	QCOMPARE(windowSizeFor(1600, 900, 480, 640, 160, 90, 0, 0), std::make_pair(1039, 1386));
	// Never below the minimum, and a broken picture size does not divide by zero.
	QCOMPARE(windowSizeFor(160, 90, 100, 1000, 160, 90, 1920, 1080), std::make_pair(160, 379));
	QCOMPARE(windowSizeFor(480, 270, 0, 0, 160, 90, 1920, 1080), std::make_pair(360, 360));
}

QTEST_MAIN(TestWebcamMode)
#include "TestWebcamMode.moc"
