# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

find_pkg(Qt6 COMPONENTS Widgets REQUIRED)

# The widget only depends on Qt, so compile it straight into the test.
add_executable(TestMultiSlider
	TestMultiSlider.cpp
	"${CMAKE_SOURCE_DIR}/src/mumble/widgets/MultiSlider.cpp"
)

set_target_properties(TestMultiSlider PROPERTIES AUTOMOC ON)

target_include_directories(TestMultiSlider PRIVATE "${CMAKE_SOURCE_DIR}/src/mumble/widgets")

target_link_libraries(TestMultiSlider PRIVATE Qt6::Widgets Qt6::Test)

add_test(
	NAME TestMultiSlider
	COMMAND $<TARGET_FILE:TestMultiSlider>
)

# Run headless so the test does not require a display.
set_tests_properties(TestMultiSlider PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
