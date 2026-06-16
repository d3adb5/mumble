// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "WebRTCAudioProcessing.h"

#include "api/audio/audio_processing.h"
#include "api/scoped_refptr.h"
#include "modules/audio_processing/include/audio_processing.h"

namespace {
webrtc::AudioProcessing::Config::NoiseSuppression::Level toWebRTCLevel(WebRTCAudioProcessor::NoiseLevel level) {
	using Level = webrtc::AudioProcessing::Config::NoiseSuppression::Level;
	switch (level) {
		case WebRTCAudioProcessor::NoiseLevel::Low:
			return Level::kLow;
		case WebRTCAudioProcessor::NoiseLevel::Moderate:
			return Level::kModerate;
		case WebRTCAudioProcessor::NoiseLevel::High:
			return Level::kHigh;
		case WebRTCAudioProcessor::NoiseLevel::VeryHigh:
			return Level::kVeryHigh;
	}
	return Level::kHigh;
}
} // namespace

struct WebRTCAudioProcessor::Impl {
	rtc::scoped_refptr< webrtc::AudioProcessing > apm;
	webrtc::StreamConfig streamConfig;
	bool echoCancel       = false;
	bool noiseSuppression = false;

	Impl(int sampleRate, int channels) : streamConfig(sampleRate, static_cast< size_t >(channels)) {}
};

WebRTCAudioProcessor::WebRTCAudioProcessor(int sampleRate, int channels, bool echoCancel, bool noiseSuppression,
										   NoiseLevel nsLevel, bool gainControl)
	: m_impl(new Impl(sampleRate, channels)) {
	m_impl->echoCancel       = echoCancel;
	m_impl->noiseSuppression = noiseSuppression;

	webrtc::AudioProcessing::Config config;

	config.echo_canceller.enabled     = echoCancel;
	config.echo_canceller.mobile_mode = false;

	config.noise_suppression.enabled = noiseSuppression;
	config.noise_suppression.level   = toWebRTCLevel(nsLevel);

	// The AEC3 high pass filter helps the echo canceller; only force it on when
	// echo cancellation is active so the signal is otherwise left untouched.
	config.high_pass_filter.enabled = echoCancel;

	// Optional post-AEC/-NS adaptive digital gain. Off by default so it does not
	// fight Mumble's own (Speex) automatic gain control.
	config.gain_controller2.enabled                  = gainControl;
	config.gain_controller2.adaptive_digital.enabled = gainControl;

	m_impl->apm = webrtc::AudioProcessingBuilder().SetConfig(config).Create();
}

WebRTCAudioProcessor::~WebRTCAudioProcessor() = default;

bool WebRTCAudioProcessor::isValid() const {
	return m_impl->apm != nullptr;
}

bool WebRTCAudioProcessor::echoCancelEnabled() const {
	return m_impl->apm != nullptr && m_impl->echoCancel;
}

bool WebRTCAudioProcessor::noiseSuppressionEnabled() const {
	return m_impl->apm != nullptr && m_impl->noiseSuppression;
}

void WebRTCAudioProcessor::analyzeReverseStream(const short *speaker) {
	if (!m_impl->apm) {
		return;
	}

	m_impl->apm->ProcessReverseStream(speaker, m_impl->streamConfig, m_impl->streamConfig,
									  const_cast< short * >(speaker));
}

void WebRTCAudioProcessor::processCapture(short *mic, int streamDelayMs) {
	if (!m_impl->apm) {
		return;
	}

	if (m_impl->echoCancel) {
		m_impl->apm->set_stream_delay_ms(streamDelayMs);
	}

	m_impl->apm->ProcessStream(mic, m_impl->streamConfig, m_impl->streamConfig, mic);
}
