# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

add_executable(TestWebcamProtocol TestWebcamProtocol.cpp)

set_target_properties(TestWebcamProtocol PROPERTIES AUTOMOC ON)

# The unit under test is the header-only helper in the client sources.
target_include_directories(TestWebcamProtocol PRIVATE "${CMAKE_SOURCE_DIR}/src/mumble")

target_link_libraries(TestWebcamProtocol PRIVATE Qt6::Test OpenSSL::Crypto nlohmann_json::nlohmann_json)

add_test(NAME TestWebcamProtocol COMMAND $<TARGET_FILE:TestWebcamProtocol>)
