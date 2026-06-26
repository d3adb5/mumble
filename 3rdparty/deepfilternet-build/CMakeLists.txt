# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

# Builds DeepFilterNet3 (the deep_filter Rust crate, with its embedded DFN3
# model) plus the small C-ABI shim under "wrapper/", and exposes them as a single
# static library target ("mumble_deepfilter") that the Mumble client links. The
# Rust crates are compiled via Corrosion (a CMake<->Cargo bridge), so a Rust
# toolchain (cargo/rustc) must be available when this option is enabled.

set(DFN_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../deepfilternet-src")
set(CORROSION_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../corrosion")

if(NOT EXISTS "${DFN_SRC_DIR}/libDF/Cargo.toml")
	message(FATAL_ERROR
		"${DFN_SRC_DIR} was not found.\n"
		"Please checkout the submodule:\n"
		"git submodule update --init --recursive"
	)
endif()

if(NOT EXISTS "${CORROSION_DIR}/CMakeLists.txt")
	message(FATAL_ERROR
		"${CORROSION_DIR} was not found.\n"
		"Please checkout the submodule:\n"
		"git submodule update --init --recursive"
	)
endif()

# Corrosion locates cargo/rustc itself and fails with a clear message if no Rust
# toolchain is installed.
add_subdirectory("${CORROSION_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/corrosion")

# Import the C-ABI shim crate. Its (path) dependency on the bundled deep_filter
# crate is resolved by Cargo, so the whole DeepFilterNet stack is built from the
# pinned submodule. Pinning the Rust standard library's release optimizations is
# left to Cargo's default release profile.
corrosion_import_crate(
	MANIFEST_PATH "${CMAKE_CURRENT_SOURCE_DIR}/wrapper/Cargo.toml"
	PROFILE release
	CRATES mumble-deepfilter
)

# The resulting static library links into the (potentially shared) Mumble client,
# so make sure it is position independent.
corrosion_add_target_local_rustflags(mumble_deepfilter "-Crelocation-model=pic")
