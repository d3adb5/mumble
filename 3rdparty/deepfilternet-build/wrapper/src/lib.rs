// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

//! Minimal C ABI around the bundled DeepFilterNet (deep_filter) crate.
//!
//! The DeepFilterNet C API (libDF/src/capi.rs) loads its model from a file path;
//! this shim instead uses the Rust API with the embedded default DFN3 model, so
//! Mumble ships no separate model file. It mirrors what the upstream LADSPA
//! plugin does and exposes only what src/mumble/DeepFilterNetProcessing.cpp uses.

use df::tract::*;
use ndarray::prelude::*;

/// Opaque handle wrapping one mono DeepFilterNet runtime instance.
pub struct MumbleDfState {
    df: DfTract,
}

/// Create a DeepFilterNet processor using the embedded model.
///
/// Returns a null pointer if the runtime could not be initialized; the caller
/// (DeepFilterNetProcessor) treats that as "unavailable" and falls back.
#[no_mangle]
pub unsafe extern "C" fn mumble_df_create(atten_lim_db: f32, post_filter_beta: f32) -> *mut MumbleDfState {
    let params = DfParams::default();
    // One channel; thresholds left at DeepFilterNet's defaults (see libDF capi).
    let r_params = RuntimeParams::default_with_ch(1)
        .with_atten_lim(atten_lim_db)
        .with_thresholds(-15.0, 35.0, 35.0)
        .with_post_filter(post_filter_beta)
        .with_mask_reduce(ReduceMask::MAX);

    match DfTract::new(params, &r_params) {
        Ok(df) => Box::into_raw(Box::new(MumbleDfState { df })),
        Err(_) => std::ptr::null_mut(),
    }
}

/// Frame (hop) size in samples the processor expects per call.
#[no_mangle]
pub unsafe extern "C" fn mumble_df_frame_length(st: *const MumbleDfState) -> usize {
    match st.as_ref() {
        Some(s) => s.df.hop_size,
        None => 0,
    }
}

/// Update the live attenuation limit (dB).
#[no_mangle]
pub unsafe extern "C" fn mumble_df_set_atten_lim(st: *mut MumbleDfState, lim_db: f32) {
    if let Some(s) = st.as_mut() {
        s.df.set_atten_lim(lim_db);
    }
}

/// Update the live post-filter strength (0 disables it).
#[no_mangle]
pub unsafe extern "C" fn mumble_df_set_post_filter_beta(st: *mut MumbleDfState, beta: f32) {
    if let Some(s) = st.as_mut() {
        s.df.set_pf_beta(beta);
    }
}

/// Denoise one frame of `mumble_df_frame_length()` normalized float samples.
///
/// `input` and `output` must each point to that many samples. Returns the local
/// SNR estimate of the frame (currently unused by the caller).
#[no_mangle]
pub unsafe extern "C" fn mumble_df_process_frame(
    st: *mut MumbleDfState,
    input: *const f32,
    output: *mut f32,
) -> f32 {
    let s = match st.as_mut() {
        Some(s) => s,
        None => return 0.0,
    };
    let n = s.df.hop_size;
    let noisy = ArrayView2::from_shape_ptr((1, n), input);
    let enh = ArrayViewMut2::from_shape_ptr((1, n), output);
    s.df.process(noisy, enh).unwrap_or(0.0)
}

/// Free a processor created by `mumble_df_create`.
#[no_mangle]
pub unsafe extern "C" fn mumble_df_free(st: *mut MumbleDfState) {
    if !st.is_null() {
        drop(Box::from_raw(st));
    }
}
