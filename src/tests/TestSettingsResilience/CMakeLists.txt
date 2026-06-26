# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

find_pkg(Qt6 COMPONENTS Widgets REQUIRED)

add_executable(TestSettingsResilience TestSettingsResilience.cpp)

set_target_properties(TestSettingsResilience PROPERTIES AUTOMOC ON)

target_link_libraries(TestSettingsResilience PRIVATE mumble_client_object_lib Qt6::Test)

add_test(
	NAME TestSettingsResilience
	COMMAND $<TARGET_FILE:TestSettingsResilience>
	# Specifying the working directory is necessary to make sure the dependent DLLs are found (on Windows)
	WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# Use the offscreen platform so the QApplication started by QTEST_MAIN does not need a
# display - otherwise the test hangs on headless Windows CI.
set_tests_properties(TestSettingsResilience PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
