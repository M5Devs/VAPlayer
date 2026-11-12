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
    ${CMAKE_SOURCE_DIR}/tools/ToolRunner.cpp
)

# Keymapper model, engine and overlay.  The overlay needs Qt Widgets.
set(VAPLAYER_KEYMAPPER_SUPPORT
    ${CMAKE_SOURCE_DIR}/core_bridge/QmpClient.cpp
    ${CMAKE_SOURCE_DIR}/keymapper/Keymap.cpp
    ${CMAKE_SOURCE_DIR}/keymapper/KeymapperEngine.cpp
    ${CMAKE_SOURCE_DIR}/keymapper/KeymapperOverlay.cpp
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

# ToolRunner plus the V-Flash / ROM manager dialogs.  The dialog tests need
# Qt Widgets, so this target links it even though the other suites do not.
add_executable(test_provisioning
    test_provisioning.cpp
    ${CMAKE_SOURCE_DIR}/tools/ToolRunner.cpp
    ${CMAKE_SOURCE_DIR}/ui/VFlashDialog.cpp
    ${CMAKE_SOURCE_DIR}/ui/RomManagerDialog.cpp
)

target_include_directories(test_provisioning PRIVATE
    ${CMAKE_SOURCE_DIR}
)

# Keymapper engine/overlay tests (Qt Widgets for the overlay).
add_executable(test_keymapper
    test_keymapper.cpp
    ${VAPLAYER_KEYMAPPER_SUPPORT}
)

target_include_directories(test_keymapper PRIVATE
    ${CMAKE_SOURCE_DIR}
)

# Drag & drop, ADB and dialog tests.
add_executable(test_task_e
    test_task_e.cpp
    ${CMAKE_SOURCE_DIR}/tools/ToolRunner.cpp
    ${CMAKE_SOURCE_DIR}/tools/AdbClient.cpp
    ${CMAKE_SOURCE_DIR}/ui/DropHelper.cpp
    ${CMAKE_SOURCE_DIR}/ui/ViewportWidget.cpp
    ${CMAKE_SOURCE_DIR}/ui/SharedFolderDialog.cpp
    ${CMAKE_SOURCE_DIR}/ui/KeymapperDialog.cpp
    ${CMAKE_SOURCE_DIR}/keymapper/Keymap.cpp
    ${CMAKE_SOURCE_DIR}/keymapper/KeymapperEngine.cpp
    ${CMAKE_SOURCE_DIR}/keymapper/KeymapperOverlay.cpp
    ${CMAKE_SOURCE_DIR}/core_bridge/QmpClient.cpp
)

target_include_directories(test_task_e PRIVATE
    ${CMAKE_SOURCE_DIR}
)

if(VAPLAYER_QT_VERSION EQUAL 6)
    target_link_libraries(test_emulator_runner PRIVATE
        Qt6::Core Qt6::Network Qt6::Test)
    target_link_libraries(test_qmp_client PRIVATE
        Qt6::Core Qt6::Network Qt6::Test)
    target_link_libraries(test_core_integration PRIVATE
        Qt6::Core Qt6::Network Qt6::Test)
    target_link_libraries(test_provisioning PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
    target_link_libraries(test_keymapper PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test)
    target_link_libraries(test_task_e PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test)
else()
    target_link_libraries(test_emulator_runner PRIVATE
        Qt5::Core Qt5::Network Qt5::Test)
    target_link_libraries(test_qmp_client PRIVATE
        Qt5::Core Qt5::Network Qt5::Test)
    target_link_libraries(test_core_integration PRIVATE
        Qt5::Core Qt5::Network Qt5::Test)
    target_link_libraries(test_provisioning PRIVATE
        Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Test)
    target_link_libraries(test_keymapper PRIVATE
        Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network Qt5::Test)
    target_link_libraries(test_task_e PRIVATE
        Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network Qt5::Test)
endif()

add_test(NAME emulator_runner COMMAND test_emulator_runner)
add_test(NAME qmp_client COMMAND test_qmp_client)

# Keymapper and Task E suites need a QApplication, so run them offscreen.
add_test(NAME keymapper COMMAND test_keymapper)
set_tests_properties(keymapper PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 120
)

add_test(NAME task_e COMMAND test_task_e)
set_tests_properties(task_e PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 120
)

# The integration test needs a built vaplayer-core.  It reads VAPLAYER_CORE
# from the environment so CI can point it at a staged core, and falls back to
# this compiled-in path for an in-tree build.  Setting the variable as a test
# property would shadow the caller's environment, so it is a compile definition
# instead.  The test skips only when no core was requested at all.
add_test(NAME core_integration COMMAND test_core_integration)
set_tests_properties(core_integration PROPERTIES TIMEOUT 120)
target_compile_definitions(test_core_integration PRIVATE
    VAPLAYER_DEFAULT_CORE="${CMAKE_SOURCE_DIR}/../core/build/vaplayer-core"
)

# Runs under a virtual display when the platform plugin requires one.
add_test(NAME provisioning COMMAND test_provisioning)
set_tests_properties(provisioning PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 120
)
