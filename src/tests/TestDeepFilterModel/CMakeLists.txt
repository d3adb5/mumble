# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

# Unlike the other client tests this one links the real (Rust) DeepFilterNet
# library, so that every platform we ship a client for is checked to actually
# carry the DFN3 mechanism and its embedded model. The PIMPL wrapper is compiled
# straight in: it pulls in nothing but the C ABI and the header-only helpers.
add_executable(TestDeepFilterModel
	TestDeepFilterModel.cpp
	"${CMAKE_SOURCE_DIR}/src/mumble/DeepFilterNetProcessing.cpp"
)

set_target_properties(TestDeepFilterModel PROPERTIES AUTOMOC ON)

target_include_directories(TestDeepFilterModel PRIVATE "${CMAKE_SOURCE_DIR}/src/mumble")

target_link_libraries(TestDeepFilterModel PRIVATE Qt6::Test mumble_deepfilter)

add_test(NAME TestDeepFilterModel COMMAND $<TARGET_FILE:TestDeepFilterModel>)
