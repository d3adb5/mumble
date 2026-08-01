// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore>
#include <QtTest>

#include "DeepFilterNet.h"
#include "DeepFilterNetProcessing.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using namespace Mumble::DeepFilter;

/// Tests that exercise the actual DeepFilterNet3 library rather than the pure
/// helpers (which TestDeepFilter covers). They are the guarantee that a build
/// configured with -Ddeepfilternet=ON really carries both the mechanism and the
/// embedded DFN3 model: the model is compiled into the Rust library through the
/// crate's "default-model" feature, so a processor that reports itself valid can
/// only have gotten there by finding that model.
class TestDeepFilterModel : public QObject {
	Q_OBJECT
private slots:
	void embeddedModelLoads();
	void silenceStaysSilent();
	void attenuatesStationaryNoise();

private:
	/// Fill a frame with reproducible white noise scaled to roughly a quarter of
	/// full scale, i.e. a signal the suppressor should recognize as pure noise.
	static void fillWithNoise(std::vector< short > &frame, std::mt19937 &rng);

	/// Root mean square of a frame, in 16 bit PCM units.
	static double rms(const std::vector< short > &frame);
};

void TestDeepFilterModel::fillWithNoise(std::vector< short > &frame, std::mt19937 &rng) {
	std::uniform_int_distribution< int > dist(-8192, 8192);
	for (short &sample : frame) {
		sample = static_cast< short >(dist(rng));
	}
}

double TestDeepFilterModel::rms(const std::vector< short > &frame) {
	double sum = 0.0;
	for (short sample : frame) {
		sum += static_cast< double >(sample) * static_cast< double >(sample);
	}
	return std::sqrt(sum / static_cast< double >(frame.size()));
}

void TestDeepFilterModel::embeddedModelLoads() {
	DeepFilterNetProcessor processor(clampAttenLimitDb(ATTEN_LIMIT_DEFAULT_DB), postFilterBeta(POST_FILTER_DEFAULT));

	// isValid() is only true once the library has instantiated the model *and*
	// reported the 480-sample hop the client feeds it.
	QVERIFY(processor.isValid());
}

void TestDeepFilterModel::silenceStaysSilent() {
	DeepFilterNetProcessor processor(clampAttenLimitDb(ATTEN_LIMIT_DEFAULT_DB), postFilterBeta(POST_FILTER_DEFAULT));
	QVERIFY(processor.isValid());

	std::vector< short > frame(FRAME_SIZE, 0);
	for (int i = 0; i < 10; ++i) {
		std::fill(frame.begin(), frame.end(), static_cast< short >(0));
		processor.processFrame(frame.data());

		// Denoising digital silence cannot invent signal. This also catches a
		// processor that writes uninitialized scratch back into the frame.
		QCOMPARE(rms(frame), 0.0);
	}
}

void TestDeepFilterModel::attenuatesStationaryNoise() {
	DeepFilterNetProcessor processor(clampAttenLimitDb(ATTEN_LIMIT_DEFAULT_DB), postFilterBeta(POST_FILTER_DEFAULT));
	QVERIFY(processor.isValid());

	std::mt19937 rng(42);
	std::vector< short > frame(FRAME_SIZE, 0);

	// The model needs a few frames to build up its noise estimate, so feed it
	// some noise before looking at what comes out.
	for (int i = 0; i < 20; ++i) {
		fillWithNoise(frame, rng);
		processor.processFrame(frame.data());
	}

	fillWithNoise(frame, rng);
	const double inputRms = rms(frame);
	processor.processFrame(frame.data());
	const double outputRms = rms(frame);

	// Speech-less white noise is what the suppressor exists to remove; anything
	// that merely passed the frame through would come out at the input level.
	QVERIFY2(outputRms < inputRms / 2.0,
			 qPrintable(QStringLiteral("Expected noise to be attenuated, got %1 -> %2")
							.arg(inputRms)
							.arg(outputRms)));
}

QTEST_MAIN(TestDeepFilterModel)
#include "TestDeepFilterModel.moc"
