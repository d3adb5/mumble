# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

find_pkg(Qt6 COMPONENTS Widgets REQUIRED)

add_executable(TestSettingsProfiles TestSettingsProfiles.cpp)

set_target_properties(TestSettingsProfiles PROPERTIES AUTOMOC ON)

target_link_libraries(TestSettingsProfiles PRIVATE mumble_client_object_lib Qt6::Test)

add_test(
	NAME TestSettingsProfiles
	COMMAND $<TARGET_FILE:TestSettingsProfiles>
	# Specifying the working directory is necessary to make sure the dependent DLLs are found (on Windows)
	WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)

set_tests_properties(TestSettingsProfiles PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
