// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_DEEPFILTERNETPROCESSING_H_
#define MUMBLE_MUMBLE_DEEPFILTERNETPROCESSING_H_

#include <memory>

/// Thin wrapper around DeepFilterNet3, the deep-learning speech enhancer used as
/// one of Mumble's noise-suppression options.
///
/// The actual model and runtime live in the bundled (Rust) DeepFilterNet library,
/// reached through a tiny C ABI. That dependency is hidden behind a PIMPL so the
/// rest of the client never has to know about it, mirroring the WebRTC wrapper.
///
/// DeepFilterNet's model is mono only and runs on a fixed 480-sample (10 ms,
/// 48 kHz) frame - the same frame Mumble's input pipeline produces. Stereo
/// transmission therefore uses one processor instance per channel.
class DeepFilterNetProcessor {
public:
	/// Construct and load a DeepFilterNet processor with the embedded model.
	///
	/// \param attenLimitDb   Maximum noise attenuation in dB (see Mumble::DeepFilter).
	/// \param postFilterBeta Post-filter strength (0 disables it).
	DeepFilterNetProcessor(float attenLimitDb, float postFilterBeta);
	~DeepFilterNetProcessor();

	DeepFilterNetProcessor(const DeepFilterNetProcessor &)            = delete;
	DeepFilterNetProcessor &operator=(const DeepFilterNetProcessor &) = delete;

	/// \return whether the model was created successfully and operates on the
	/// expected 480-sample frame.
	bool isValid() const;

	/// Update the live attenuation limit (dB).
	void setAttenLimit(float attenLimitDb);

	/// Update the live post-filter strength (0 disables it).
	void setPostFilterBeta(float postFilterBeta);

	/// Denoise one 480-sample frame of mono 16 bit PCM in place.
	///
	/// \param frame Exactly Mumble::DeepFilter::FRAME_SIZE samples; modified in place.
	void processFrame(short *frame);

private:
	struct Impl;
	std::unique_ptr< Impl > m_impl;
};

#endif // MUMBLE_MUMBLE_DEEPFILTERNETPROCESSING_H_
