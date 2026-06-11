# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

find_pkg(Qt6 COMPONENTS Gui REQUIRED)

set(MUMBLE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/mumble")

set(TESTMARKDOWN_SOURCES
	TestMarkdown.cpp

	"${MUMBLE_SOURCE_DIR}/Markdown.cpp"
	"${MUMBLE_SOURCE_DIR}/Markdown.h"
)

add_executable(TestMarkdown ${TESTMARKDOWN_SOURCES})

set_target_properties(TestMarkdown PROPERTIES AUTOMOC ON)

target_include_directories(TestMarkdown PRIVATE ${MUMBLE_SOURCE_DIR})

target_link_libraries(TestMarkdown PRIVATE shared Qt6::Test Qt6::Gui)

add_test(NAME TestMarkdown COMMAND $<TARGET_FILE:TestMarkdown>)
