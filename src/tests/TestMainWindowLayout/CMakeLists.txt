# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

find_pkg(Qt6 COMPONENTS Widgets REQUIRED)

# The helper is header-only and only depends on Qt, so there is nothing to
# compile besides the test itself.
add_executable(TestMainWindowLayout
	TestMainWindowLayout.cpp
)

set_target_properties(TestMainWindowLayout PROPERTIES AUTOMOC ON)

target_include_directories(TestMainWindowLayout PRIVATE "${CMAKE_SOURCE_DIR}/src/mumble")

target_link_libraries(TestMainWindowLayout PRIVATE Qt6::Widgets Qt6::Test)

add_test(
	NAME TestMainWindowLayout
	COMMAND $<TARGET_FILE:TestMainWindowLayout>
)

# Run headless so the test does not require a display.
set_tests_properties(TestMainWindowLayout PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
