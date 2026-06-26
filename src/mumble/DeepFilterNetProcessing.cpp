// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "DeepFilterNetProcessing.h"

#include "DeepFilterNet.h"

#include <array>
#include <cstddef>

// C ABI exported by the bundled DeepFilterNet wrapper crate
// (3rdparty/deepfilternet-build/wrapper). Declared here so the heavy Rust
// dependency is reached only from this translation unit. The model is embedded
// in the library, so no external model file is needed at runtime.
extern "C" {
struct MumbleDfState;
MumbleDfState *mumble_df_create(float atten_lim_db, float post_filter_beta);
std::size_t mumble_df_frame_length(const MumbleDfState *st);
void mumble_df_set_atten_lim(MumbleDfState *st, float lim_db);
void mumble_df_set_post_filter_beta(MumbleDfState *st, float beta);
float mumble_df_process_frame(MumbleDfState *st, const float *input, float *output);
void mumble_df_free(MumbleDfState *st);
}

struct DeepFilterNetProcessor::Impl {
	MumbleDfState *state = nullptr;
	// Scratch buffers for the normalized float frame the model works on.
	std::array< float, Mumble::DeepFilter::FRAME_SIZE > in{};
	std::array< float, Mumble::DeepFilter::FRAME_SIZE > out{};
};

DeepFilterNetProcessor::DeepFilterNetProcessor(float attenLimitDb, float postFilterBeta) : m_impl(new Impl) {
	m_impl->state = mumble_df_create(attenLimitDb, postFilterBeta);

	// The pipeline only ever feeds 480-sample frames; bail out if the model
	// expects a different hop so processFrame() never reads/writes out of bounds.
	if (m_impl->state && mumble_df_frame_length(m_impl->state) != Mumble::DeepFilter::FRAME_SIZE) {
		mumble_df_free(m_impl->state);
		m_impl->state = nullptr;
	}
}

DeepFilterNetProcessor::~DeepFilterNetProcessor() {
	if (m_impl->state) {
		mumble_df_free(m_impl->state);
	}
}

bool DeepFilterNetProcessor::isValid() const {
	return m_impl->state != nullptr;
}

void DeepFilterNetProcessor::setAttenLimit(float attenLimitDb) {
	if (m_impl->state) {
		mumble_df_set_atten_lim(m_impl->state, attenLimitDb);
	}
}

void DeepFilterNetProcessor::setPostFilterBeta(float postFilterBeta) {
	if (m_impl->state) {
		mumble_df_set_post_filter_beta(m_impl->state, postFilterBeta);
	}
}

void DeepFilterNetProcessor::processFrame(short *frame) {
	if (!m_impl->state) {
		return;
	}

	for (unsigned int i = 0; i < Mumble::DeepFilter::FRAME_SIZE; ++i) {
		m_impl->in[i] = Mumble::DeepFilter::int16ToFloat(frame[i]);
	}

	mumble_df_process_frame(m_impl->state, m_impl->in.data(), m_impl->out.data());

	for (unsigned int i = 0; i < Mumble::DeepFilter::FRAME_SIZE; ++i) {
		frame[i] = Mumble::DeepFilter::floatToInt16(m_impl->out[i]);
	}
}
