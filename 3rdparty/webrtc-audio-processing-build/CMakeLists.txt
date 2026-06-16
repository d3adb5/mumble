# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

# CMake build for the freedesktop "webrtc-audio-processing" library (a curated
# snapshot of WebRTC's audio processing module, providing the AEC3 echo
# canceller and the WebRTC noise suppressor). Upstream only ships a Meson build,
# so the source lists below are translated from the upstream meson.build files
# (pinned submodule tag v2.1). The companion abseil-cpp submodule provides the
# required Abseil targets.

set(WAP_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../webrtc-audio-processing-src")
set(W "${WAP_SRC_DIR}/webrtc")
set(ABSEIL_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../abseil-cpp")

if(NOT EXISTS "${W}/modules/audio_processing/include/audio_processing.h")
	message(FATAL_ERROR
		"${WAP_SRC_DIR} was not found.\n"
		"Please checkout the submodule:\n"
		"git submodule update --init --recursive"
	)
endif()

# Everything here links into the (potentially shared) Mumble client, so it must
# be position independent.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# WebRTC (and Abseil) define many identically named symbols in anonymous
# namespaces across different source files. They are therefore not safe to
# compile as a unity/jumbo build, so disable it for everything defined here and
# in the Abseil subdirectory (Mumble enables it globally).
set(CMAKE_UNITY_BUILD OFF)

# ----------------------------------------------------------------------------
# Abseil
# ----------------------------------------------------------------------------
# WebRTC 2.1 requires the Abseil version it was released against; a system Abseil
# may be too new (e.g. it may have removed APIs WebRTC still uses). At the same
# time the consuming build (Mumble) usually already links a *different* Abseil
# that protobuf pulls in. To keep the two completely independent we do NOT use
# Abseil's own CMake here (it would register "absl::" targets that clash with the
# other Abseil) - instead we compile the bundled Abseil sources directly into a
# privately named static library. Abseil's LTS inline namespace keeps its symbols
# distinct from any other Abseil linked into the same binary.
if(NOT EXISTS "${ABSEIL_SRC_DIR}/absl/base/options.h")
	message(FATAL_ERROR
		"${ABSEIL_SRC_DIR} was not found.\n"
		"Please checkout the submodule:\n"
		"git submodule update --init --recursive"
	)
endif()

file(GLOB_RECURSE WAP_ABSL_SOURCES CONFIGURE_DEPENDS "${ABSEIL_SRC_DIR}/absl/*.cc")
# Drop tests, benchmarks and mocks.
list(FILTER WAP_ABSL_SOURCES EXCLUDE REGEX
	"_test\\.cc$|_test_|test_util|_testing|/testing/|mock_|_benchmark\\.cc$|/benchmark|nonprod")
# The random and crc sub-libraries contain sources that need special instruction
# set flags (AES / SSE4.2 / PCLMUL); WebRTC does not use them, so drop them.
list(FILTER WAP_ABSL_SOURCES EXCLUDE REGEX "/random/|/crc/")

add_library(webrtc-absl STATIC ${WAP_ABSL_SOURCES})
target_include_directories(webrtc-absl SYSTEM PUBLIC "${ABSEIL_SRC_DIR}")
if(NOT WIN32)
	find_package(Threads REQUIRED)
	target_link_libraries(webrtc-absl PUBLIC Threads::Threads)
endif()

# ----------------------------------------------------------------------------
# Architecture detection (mirrors the upstream meson logic)
# ----------------------------------------------------------------------------
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _wap_proc)
set(WAP_X86 FALSE)
set(WAP_ARM64 FALSE)
if(_wap_proc MATCHES "x86_64|amd64|x64|i[3-6]86|x86")
	set(WAP_X86 TRUE)
elseif(_wap_proc MATCHES "aarch64|arm64")
	set(WAP_ARM64 TRUE)
endif()

if(MSVC)
	set(WAP_AVX_FLAGS "/arch:AVX2")
	set(WAP_SSE2_FLAGS "")
else()
	set(WAP_AVX_FLAGS "-mavx2;-mfma")
	set(WAP_SSE2_FLAGS "-msse2")
endif()

# ----------------------------------------------------------------------------
# Source lists (translated from upstream meson.build files)
# ----------------------------------------------------------------------------
set(WAP_RTC_BASE_SOURCES
	rtc_base/checks.cc
	rtc_base/containers/flat_tree.cc
	rtc_base/event.cc
	rtc_base/event_tracer.cc
	rtc_base/experiments/field_trial_parser.cc
	rtc_base/logging.cc
	rtc_base/memory/aligned_malloc.cc
	rtc_base/platform_thread.cc
	rtc_base/platform_thread_types.cc
	rtc_base/race_checker.cc
	rtc_base/random.cc
	rtc_base/string_encode.cc
	rtc_base/string_to_number.cc
	rtc_base/string_utils.cc
	rtc_base/strings/string_builder.cc
	rtc_base/synchronization/sequence_checker_internal.cc
	rtc_base/synchronization/yield_policy.cc
	rtc_base/system/file_wrapper.cc
	rtc_base/system_time.cc
	rtc_base/time_utils.cc
	rtc_base/zero_memory.cc
)

set(WAP_API_SOURCES
	api/audio/audio_frame.cc
	api/audio/audio_processing.cc
	api/audio/audio_processing_statistics.cc
	api/audio/channel_layout.cc
	api/audio/echo_canceller3_config.cc
	api/rtp_packet_info.cc
	api/task_queue/task_queue_base.cc
	api/units/frequency.cc
	api/units/time_delta.cc
	api/units/timestamp.cc
	api/video/color_space.cc
	api/video/hdr_metadata.cc
	api/video/video_content_type.cc
	api/video/video_timing.cc
)

set(WAP_SYSTEM_WRAPPERS_SOURCES
	system_wrappers/source/cpu_features.cc
	system_wrappers/source/denormal_disabler.cc
	system_wrappers/source/field_trial.cc
	system_wrappers/source/metrics.cc
	system_wrappers/source/sleep.cc
)

set(WAP_COMMON_AUDIO_SOURCES
	common_audio/audio_converter.cc
	common_audio/audio_util.cc
	common_audio/channel_buffer.cc
	common_audio/fir_filter_c.cc
	common_audio/fir_filter_factory.cc
	common_audio/resampler/push_resampler.cc
	common_audio/resampler/push_sinc_resampler.cc
	common_audio/resampler/resampler.cc
	common_audio/resampler/sinc_resampler.cc
	common_audio/resampler/sinusoidal_linear_chirp_source.cc
	common_audio/ring_buffer.c
	common_audio/signal_processing/auto_correlation.c
	common_audio/signal_processing/auto_corr_to_refl_coef.c
	common_audio/signal_processing/complex_bit_reverse.c
	common_audio/signal_processing/complex_fft.c
	common_audio/signal_processing/copy_set_operations.c
	common_audio/signal_processing/cross_correlation.c
	common_audio/signal_processing/division_operations.c
	common_audio/signal_processing/dot_product_with_scale.cc
	common_audio/signal_processing/downsample_fast.c
	common_audio/signal_processing/filter_ar.c
	common_audio/signal_processing/filter_ar_fast_q12.c
	common_audio/signal_processing/energy.c
	common_audio/signal_processing/filter_ma_fast_q12.c
	common_audio/signal_processing/get_hanning_window.c
	common_audio/signal_processing/get_scaling_square.c
	common_audio/signal_processing/ilbc_specific_functions.c
	common_audio/signal_processing/levinson_durbin.c
	common_audio/signal_processing/lpc_to_refl_coef.c
	common_audio/signal_processing/min_max_operations.c
	common_audio/signal_processing/randomization_functions.c
	common_audio/signal_processing/real_fft.c
	common_audio/signal_processing/refl_coef_to_lpc.c
	common_audio/signal_processing/resample_48khz.c
	common_audio/signal_processing/resample_by_2.c
	common_audio/signal_processing/resample_by_2_internal.c
	common_audio/signal_processing/resample.c
	common_audio/signal_processing/resample_fractional.c
	common_audio/signal_processing/spl_init.c
	common_audio/signal_processing/spl_inl.c
	common_audio/signal_processing/splitting_filter.c
	common_audio/signal_processing/spl_sqrt.c
	common_audio/signal_processing/sqrt_of_one_minus_x_squared.c
	common_audio/signal_processing/vector_scaling_operations.c
	common_audio/smoothing_filter.cc
	common_audio/third_party/ooura/fft_size_128/ooura_fft.cc
	common_audio/third_party/ooura/fft_size_256/fft4g.cc
	common_audio/third_party/spl_sqrt_floor/spl_sqrt_floor.c
	common_audio/vad/vad.cc
	common_audio/vad/vad_core.c
	common_audio/vad/vad_filterbank.c
	common_audio/vad/vad_gmm.c
	common_audio/vad/vad_sp.c
	common_audio/vad/webrtc_vad.c
)

# iSAC VAD helpers that the APM links against (from audio_coding).
set(WAP_ISAC_VAD_SOURCES
	modules/audio_coding/codecs/isac/main/source/filter_functions.c
	modules/audio_coding/codecs/isac/main/source/isac_vad.c
	modules/audio_coding/codecs/isac/main/source/pitch_estimator.c
	modules/audio_coding/codecs/isac/main/source/pitch_filter.c
)

set(WAP_APM_SOURCES
	modules/audio_processing/aec_dump/null_aec_dump_factory.cc
	modules/audio_processing/aec3/adaptive_fir_filter.cc
	modules/audio_processing/aec3/adaptive_fir_filter_erl.cc
	modules/audio_processing/aec3/aec3_common.cc
	modules/audio_processing/aec3/aec3_fft.cc
	modules/audio_processing/aec3/aec_state.cc
	modules/audio_processing/aec3/alignment_mixer.cc
	modules/audio_processing/aec3/api_call_jitter_metrics.cc
	modules/audio_processing/aec3/block_buffer.cc
	modules/audio_processing/aec3/block_delay_buffer.cc
	modules/audio_processing/aec3/block_framer.cc
	modules/audio_processing/aec3/block_processor.cc
	modules/audio_processing/aec3/block_processor_metrics.cc
	modules/audio_processing/aec3/clockdrift_detector.cc
	modules/audio_processing/aec3/coarse_filter_update_gain.cc
	modules/audio_processing/aec3/comfort_noise_generator.cc
	modules/audio_processing/aec3/config_selector.cc
	modules/audio_processing/aec3/decimator.cc
	modules/audio_processing/aec3/dominant_nearend_detector.cc
	modules/audio_processing/aec3/downsampled_render_buffer.cc
	modules/audio_processing/aec3/echo_audibility.cc
	modules/audio_processing/aec3/echo_canceller3.cc
	modules/audio_processing/aec3/echo_path_delay_estimator.cc
	modules/audio_processing/aec3/echo_path_variability.cc
	modules/audio_processing/aec3/echo_remover.cc
	modules/audio_processing/aec3/echo_remover_metrics.cc
	modules/audio_processing/aec3/erle_estimator.cc
	modules/audio_processing/aec3/erl_estimator.cc
	modules/audio_processing/aec3/fft_buffer.cc
	modules/audio_processing/aec3/filter_analyzer.cc
	modules/audio_processing/aec3/frame_blocker.cc
	modules/audio_processing/aec3/fullband_erle_estimator.cc
	modules/audio_processing/aec3/matched_filter.cc
	modules/audio_processing/aec3/matched_filter_lag_aggregator.cc
	modules/audio_processing/aec3/moving_average.cc
	modules/audio_processing/aec3/multi_channel_content_detector.cc
	modules/audio_processing/aec3/refined_filter_update_gain.cc
	modules/audio_processing/aec3/render_buffer.cc
	modules/audio_processing/aec3/render_delay_buffer.cc
	modules/audio_processing/aec3/render_delay_controller.cc
	modules/audio_processing/aec3/render_delay_controller_metrics.cc
	modules/audio_processing/aec3/render_signal_analyzer.cc
	modules/audio_processing/aec3/residual_echo_estimator.cc
	modules/audio_processing/aec3/reverb_decay_estimator.cc
	modules/audio_processing/aec3/reverb_frequency_response.cc
	modules/audio_processing/aec3/reverb_model.cc
	modules/audio_processing/aec3/reverb_model_estimator.cc
	modules/audio_processing/aec3/signal_dependent_erle_estimator.cc
	modules/audio_processing/aec3/spectrum_buffer.cc
	modules/audio_processing/aec3/stationarity_estimator.cc
	modules/audio_processing/aec3/subband_erle_estimator.cc
	modules/audio_processing/aec3/subband_nearend_detector.cc
	modules/audio_processing/aec3/subtractor.cc
	modules/audio_processing/aec3/subtractor_output_analyzer.cc
	modules/audio_processing/aec3/subtractor_output.cc
	modules/audio_processing/aec3/suppression_filter.cc
	modules/audio_processing/aec3/suppression_gain.cc
	modules/audio_processing/aec3/transparent_mode.cc
	modules/audio_processing/aecm/aecm_core.cc
	modules/audio_processing/aecm/echo_control_mobile.cc
	modules/audio_processing/agc/agc.cc
	modules/audio_processing/agc/agc_manager_direct.cc
	modules/audio_processing/agc/legacy/analog_agc.cc
	modules/audio_processing/agc/legacy/digital_agc.cc
	modules/audio_processing/agc/loudness_histogram.cc
	modules/audio_processing/agc/utility.cc
	modules/audio_processing/agc2/adaptive_digital_gain_controller.cc
	modules/audio_processing/agc2/agc2_testing_common.cc
	modules/audio_processing/agc2/biquad_filter.cc
	modules/audio_processing/agc2/clipping_predictor.cc
	modules/audio_processing/agc2/clipping_predictor_level_buffer.cc
	modules/audio_processing/agc2/compute_interpolated_gain_curve.cc
	modules/audio_processing/agc2/cpu_features.cc
	modules/audio_processing/agc2/fixed_digital_level_estimator.cc
	modules/audio_processing/agc2/gain_applier.cc
	modules/audio_processing/agc2/input_volume_controller.cc
	modules/audio_processing/agc2/input_volume_stats_reporter.cc
	modules/audio_processing/agc2/interpolated_gain_curve.cc
	modules/audio_processing/agc2/limiter.cc
	modules/audio_processing/agc2/limiter_db_gain_curve.cc
	modules/audio_processing/agc2/noise_level_estimator.cc
	modules/audio_processing/agc2/rnn_vad/auto_correlation.cc
	modules/audio_processing/agc2/rnn_vad/features_extraction.cc
	modules/audio_processing/agc2/rnn_vad/lp_residual.cc
	modules/audio_processing/agc2/rnn_vad/pitch_search.cc
	modules/audio_processing/agc2/rnn_vad/pitch_search_internal.cc
	modules/audio_processing/agc2/rnn_vad/rnn.cc
	modules/audio_processing/agc2/rnn_vad/rnn_fc.cc
	modules/audio_processing/agc2/rnn_vad/rnn_gru.cc
	modules/audio_processing/agc2/rnn_vad/spectral_features.cc
	modules/audio_processing/agc2/rnn_vad/spectral_features_internal.cc
	modules/audio_processing/agc2/saturation_protector.cc
	modules/audio_processing/agc2/saturation_protector_buffer.cc
	modules/audio_processing/agc2/speech_level_estimator.cc
	modules/audio_processing/agc2/speech_probability_buffer.cc
	modules/audio_processing/agc2/vad_wrapper.cc
	modules/audio_processing/agc2/vector_float_frame.cc
	modules/audio_processing/audio_buffer.cc
	modules/audio_processing/audio_processing_builder_impl.cc
	modules/audio_processing/audio_processing_impl.cc
	modules/audio_processing/capture_levels_adjuster/audio_samples_scaler.cc
	modules/audio_processing/capture_levels_adjuster/capture_levels_adjuster.cc
	modules/audio_processing/echo_control_mobile_impl.cc
	modules/audio_processing/echo_detector/circular_buffer.cc
	modules/audio_processing/echo_detector/mean_variance_estimator.cc
	modules/audio_processing/echo_detector/moving_max.cc
	modules/audio_processing/echo_detector/normalized_covariance_estimator.cc
	modules/audio_processing/gain_control_impl.cc
	modules/audio_processing/gain_controller2.cc
	modules/audio_processing/high_pass_filter.cc
	modules/audio_processing/include/aec_dump.cc
	modules/audio_processing/include/audio_frame_proxies.cc
	modules/audio_processing/logging/apm_data_dumper.cc
	modules/audio_processing/ns/fast_math.cc
	modules/audio_processing/ns/histograms.cc
	modules/audio_processing/ns/noise_estimator.cc
	modules/audio_processing/ns/noise_suppressor.cc
	modules/audio_processing/ns/ns_fft.cc
	modules/audio_processing/ns/prior_signal_model.cc
	modules/audio_processing/ns/prior_signal_model_estimator.cc
	modules/audio_processing/ns/quantile_noise_estimator.cc
	modules/audio_processing/ns/signal_model.cc
	modules/audio_processing/ns/signal_model_estimator.cc
	modules/audio_processing/ns/speech_probability_estimator.cc
	modules/audio_processing/ns/suppression_params.cc
	modules/audio_processing/ns/wiener_filter.cc
	modules/audio_processing/residual_echo_detector.cc
	modules/audio_processing/rms_level.cc
	modules/audio_processing/splitting_filter.cc
	modules/audio_processing/three_band_filter_bank.cc
	modules/audio_processing/utility/cascaded_biquad_filter.cc
	modules/audio_processing/utility/delay_estimator.cc
	modules/audio_processing/utility/delay_estimator_wrapper.cc
	modules/audio_processing/utility/pffft_wrapper.cc
	modules/audio_processing/vad/gmm.cc
	modules/audio_processing/vad/pitch_based_vad.cc
	modules/audio_processing/vad/pitch_internal.cc
	modules/audio_processing/vad/pole_zero_filter.cc
	modules/audio_processing/vad/standalone_vad.cc
	modules/audio_processing/vad/vad_audio_proc.cc
	modules/audio_processing/vad/vad_circular_buffer.cc
	modules/audio_processing/vad/voice_activity_detector.cc
)

# AECM core: scalar C version on non-NEON, NEON variant added below.
list(APPEND WAP_APM_SOURCES modules/audio_processing/aecm/aecm_core_c.cc)

set(WAP_PFFFT_SOURCES third_party/pffft/src/pffft.c)
set(WAP_RNNOISE_SOURCES third_party/rnnoise/src/rnn_vad_weights.cc)

# Architecture specific SIMD sources.
set(WAP_SSE2_SOURCES "")
set(WAP_AVX2_SOURCES "")
set(WAP_NEON_SOURCES "")
if(WAP_X86)
	set(WAP_SSE2_SOURCES
		common_audio/fir_filter_sse.cc
		common_audio/resampler/sinc_resampler_sse.cc
		common_audio/third_party/ooura/fft_size_128/ooura_fft_sse2.cc
	)
	set(WAP_AVX2_SOURCES
		common_audio/fir_filter_avx2.cc
		common_audio/resampler/sinc_resampler_avx2.cc
		modules/audio_processing/aec3/adaptive_fir_filter_avx2.cc
		modules/audio_processing/aec3/adaptive_fir_filter_erl_avx2.cc
		modules/audio_processing/aec3/fft_data_avx2.cc
		modules/audio_processing/aec3/matched_filter_avx2.cc
		modules/audio_processing/aec3/vector_math_avx2.cc
		modules/audio_processing/agc2/rnn_vad/vector_math_avx2.cc
	)
elseif(WAP_ARM64)
	set(WAP_NEON_SOURCES
		common_audio/fir_filter_neon.cc
		common_audio/resampler/sinc_resampler_neon.cc
		common_audio/signal_processing/cross_correlation_neon.c
		common_audio/signal_processing/downsample_fast_neon.c
		common_audio/signal_processing/min_max_operations_neon.c
		common_audio/third_party/ooura/fft_size_128/ooura_fft_neon.cc
		modules/audio_processing/aecm/aecm_core_neon.cc
	)
endif()

# Combine, and turn every entry into an absolute path under the webrtc/ root.
set(WAP_ALL_SOURCES
	${WAP_RTC_BASE_SOURCES}
	${WAP_API_SOURCES}
	${WAP_SYSTEM_WRAPPERS_SOURCES}
	${WAP_COMMON_AUDIO_SOURCES}
	${WAP_ISAC_VAD_SOURCES}
	${WAP_APM_SOURCES}
	${WAP_PFFFT_SOURCES}
	${WAP_RNNOISE_SOURCES}
	${WAP_SSE2_SOURCES}
	${WAP_AVX2_SOURCES}
	${WAP_NEON_SOURCES}
)
list(TRANSFORM WAP_ALL_SOURCES PREPEND "${W}/")

add_library(webrtc-apm STATIC ${WAP_ALL_SOURCES})

# ----------------------------------------------------------------------------
# Per source-file compilation flags for the SIMD variants.
# ----------------------------------------------------------------------------
foreach(_src IN LISTS WAP_SSE2_SOURCES)
	if(WAP_SSE2_FLAGS)
		set_source_files_properties("${W}/${_src}" PROPERTIES COMPILE_OPTIONS "${WAP_SSE2_FLAGS}")
	endif()
endforeach()
foreach(_src IN LISTS WAP_AVX2_SOURCES)
	set_source_files_properties("${W}/${_src}" PROPERTIES COMPILE_OPTIONS "${WAP_AVX_FLAGS}")
endforeach()

# pffft wants _GNU_SOURCE (for sincosf and friends); harmless elsewhere.
set_source_files_properties("${W}/third_party/pffft/src/pffft.c"
	PROPERTIES COMPILE_DEFINITIONS "_GNU_SOURCE")

# ----------------------------------------------------------------------------
# Defines and include directories.
# ----------------------------------------------------------------------------
target_compile_definitions(webrtc-apm
	PRIVATE
		"WEBRTC_LIBRARY_IMPL"
		"WEBRTC_APM_DEBUG_DUMP=0"
		"NDEBUG"
)

# Some WebRTC headers (e.g. rtc_base/trace_event.h) use fixed width integer
# types without including <cstdint>, which recent libstdc++/libc++ versions no
# longer pull in transitively. Force-include it for every C++ translation unit
# instead of patching the pinned submodule.
if(MSVC)
	target_compile_options(webrtc-apm PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:SHELL:/FI cstdint>")
else()
	target_compile_options(webrtc-apm PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include cstdint>")
endif()

if(WIN32)
	target_compile_definitions(webrtc-apm
		PUBLIC
			"WEBRTC_WIN"
		PRIVATE
			"_WIN32"
			"NOMINMAX"
			"_USE_MATH_DEFINES"
			"_WINSOCKAPI_"
			"__STDC_FORMAT_MACROS=1"
	)
	target_link_libraries(webrtc-apm PRIVATE winmm)
elseif(APPLE)
	target_compile_definitions(webrtc-apm PUBLIC "WEBRTC_MAC" "WEBRTC_POSIX")
else()
	target_compile_definitions(webrtc-apm PUBLIC "WEBRTC_LINUX" "WEBRTC_POSIX" "WEBRTC_THREAD_RR")
endif()

if(WAP_X86)
	target_compile_definitions(webrtc-apm PRIVATE "WEBRTC_ENABLE_AVX2")
elseif(WAP_ARM64)
	target_compile_definitions(webrtc-apm PRIVATE "WEBRTC_ARCH_ARM64" "WEBRTC_HAS_NEON")
else()
	# No usable SIMD: let pffft fall back to scalar code.
	set_source_files_properties("${W}/third_party/pffft/src/pffft.c"
		PROPERTIES COMPILE_DEFINITIONS "_GNU_SOURCE;PFFFT_SIMD_DISABLE")
endif()

# The headers are included by the wrapper (which builds with -Werror), so expose
# the WebRTC include root as SYSTEM to keep third-party warnings out of our
# build. The bundled Abseil include root is propagated through webrtc-absl.
target_include_directories(webrtc-apm SYSTEM PUBLIC "${W}")

target_link_libraries(webrtc-apm PUBLIC webrtc-absl)

if(NOT WIN32)
	find_package(Threads REQUIRED)
	target_link_libraries(webrtc-apm PRIVATE Threads::Threads)
endif()
