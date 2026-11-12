# VAPlayer UI tests
#
# Copyright (C) 2026 VAPlayer contributors
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# this program.  If not, see <https://www.gnu.org/licenses/>.

# The core bridge and process manager are compiled directly into the test
# binaries so they can be exercised without linking the whole GUI.
set(VAPLAYER_TEST_SUPPORT
    ${CMAKE_SOURCE_DIR}/core_bridge/QmpClient.cpp
    ${CMAKE_SOURCE_DIR}/process/EmulatorRunner.cpp
)

add_executable(test_emulator_runner
    test_emulator_runner.cpp
    ${VAPLAYER_TEST_SUPPORT}
)

target_include_directories(test_emulator_runner PRIVATE
    ${CMAKE_SOURCE_DIR}
)

add_executable(test_qmp_client
    test_qmp_client.cpp
    ${VAPLAYER_TEST_SUPPORT}
)

target_include_directories(test_qmp_client PRIVATE
    ${CMAKE_SOURCE_DIR}
)

add_executable(test_core_integration
    test_core_integration.cpp
    ${VAPLAYER_TEST_SUPPORT}
)

target_include_directories(test_core_integration PRIVATE
    ${CMAKE_SOURCE_DIR}
)

if(VAPLAYER_QT_VERSION EQUAL 6)
    target_link_libraries(test_emulator_runner PRIVATE
        Qt6::Core Qt6::Network Qt6::Test)
    target_link_libraries(test_qmp_client PRIVATE
        Qt6::Core Qt6::Network Qt6::Test)
    target_link_libraries(test_core_integration PRIVATE
        Qt6::Core Qt6::Network Qt6::Test)
else()
    target_link_libraries(test_emulator_runner PRIVATE
        Qt5::Core Qt5::Network Qt5::Test)
    target_link_libraries(test_qmp_client PRIVATE
        Qt5::Core Qt5::Network Qt5::Test)
    target_link_libraries(test_core_integration PRIVATE
        Qt5::Core Qt5::Network Qt5::Test)
endif()

add_test(NAME emulator_runner COMMAND test_emulator_runner)
add_test(NAME qmp_client COMMAND test_qmp_client)

# The integration test needs a built vaplayer-core; it skips cleanly if absent.
add_test(NAME core_integration COMMAND test_core_integration)
set_tests_properties(core_integration PROPERTIES
    ENVIRONMENT "VAPLAYER_CORE=${CMAKE_SOURCE_DIR}/../core/build/vaplayer-core"
    TIMEOUT 120
)
