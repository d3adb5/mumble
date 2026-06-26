# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

find_pkg(Qt6 COMPONENTS Widgets REQUIRED)

add_executable(TestSettingsResilience TestSettingsResilience.cpp)

set_target_properties(TestSettingsResilience PROPERTIES AUTOMOC ON)

target_link_libraries(TestSettingsResilience PRIVATE mumble_client_object_lib)
target_link_libraries(TestSettingsResilience PRIVATE Qt6::Test)

add_test(
	NAME TestSettingsResilience
	COMMAND $<TARGET_FILE:TestSettingsResilience>
	WORKING_DIRECTORY $<TARGET_FILE_DIR:TestSettingsResilience>
)
