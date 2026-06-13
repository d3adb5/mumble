# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

add_executable(TestAudioAmplification TestAudioAmplification.cpp)

set_target_properties(TestAudioAmplification PROPERTIES AUTOMOC ON)

# The unit under test is the header-only helper in the client sources.
target_include_directories(TestAudioAmplification PRIVATE "${CMAKE_SOURCE_DIR}/src/mumble")

target_link_libraries(TestAudioAmplification PRIVATE shared Qt6::Test)

add_test(NAME TestAudioAmplification COMMAND $<TARGET_FILE:TestAudioAmplification>)
