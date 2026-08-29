# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

add_executable(TestTrayMenuModel TestTrayMenuModel.cpp)

set_target_properties(TestTrayMenuModel PROPERTIES AUTOMOC ON)

# The unit under test is the header-only helper in the client sources.
target_include_directories(TestTrayMenuModel PRIVATE "${CMAKE_SOURCE_DIR}/src/mumble")

target_link_libraries(TestTrayMenuModel PRIVATE shared Qt6::Test)

add_test(NAME TestTrayMenuModel COMMAND $<TARGET_FILE:TestTrayMenuModel>)
