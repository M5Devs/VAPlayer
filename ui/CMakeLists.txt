# VAPlayer Qt user interface
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

cmake_minimum_required(VERSION 3.16)

project(VAPlayer
    VERSION 0.1.0
    DESCRIPTION "Qt front-end for the VAPlayer x86_64 Android emulator"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# ---------------------------------------------------------------------------
# Qt discovery: prefer Qt6, fall back to Qt5.
# ---------------------------------------------------------------------------
find_package(Qt6 QUIET COMPONENTS Core Gui Widgets Network)
if(NOT Qt6_FOUND)
    find_package(Qt5 5.15 QUIET COMPONENTS Core Gui Widgets Network)
endif()

if(Qt6_FOUND)
    set(VAPLAYER_QT_VERSION 6)
    set(CMAKE_AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/ui")
elseif(Qt5_FOUND)
    set(VAPLAYER_QT_VERSION 5)
    set(CMAKE_AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/ui")
else()
    message(FATAL_ERROR
        "Neither Qt6 nor Qt5 (Core, Gui, Widgets, Network) was found. "
        "Install qt6-base-dev or qtbase5-dev.")
endif()

message(STATUS "VAPlayer UI: building against Qt${VAPLAYER_QT_VERSION}")

set(VAPLAYER_SOURCES
    core_bridge/QmpClient.cpp
    process/EmulatorRunner.cpp
    ui/MainWindow.cpp
    ui/SettingsDialog.cpp
    main.cpp
)

set(VAPLAYER_HEADERS
    core_bridge/QmpClient.h
    process/EmulatorRunner.h
    ui/MainWindow.h
    ui/SettingsDialog.h
)

set(VAPLAYER_FORMS
    ui/MainWindow.ui
    ui/SettingsDialog.ui
)

add_executable(vaplayer-ui
    ${VAPLAYER_SOURCES}
    ${VAPLAYER_HEADERS}
    ${VAPLAYER_FORMS}
    resources/resources.qrc
)

target_include_directories(vaplayer-ui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(WIN32)
    target_compile_definitions(vaplayer-ui PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

# ---------------------------------------------------------------------------
# Link against the selected Qt major version.
# ---------------------------------------------------------------------------
if(VAPLAYER_QT_VERSION EQUAL 6)
    target_link_libraries(vaplayer-ui PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)
else()
    target_link_libraries(vaplayer-ui PRIVATE
        Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network)
endif()

# ---------------------------------------------------------------------------
# Tests (optional; only built when Qt Test is available).
# ---------------------------------------------------------------------------
option(VAPLAYER_BUILD_TESTS "Build the VAPlayer UI unit tests" ON)

if(VAPLAYER_BUILD_TESTS)
    if(VAPLAYER_QT_VERSION EQUAL 6)
        find_package(Qt6 QUIET COMPONENTS Test)
        set(VAPLAYER_QT_TEST Qt6::Test)
    else()
        find_package(Qt5 QUIET COMPONENTS Test)
        set(VAPLAYER_QT_TEST Qt5::Test)
    endif()

    if(TARGET ${VAPLAYER_QT_TEST})
        enable_testing()
        add_subdirectory(tests)
    else()
        message(STATUS "Qt Test not found: skipping VAPlayer UI tests")
    endif()
endif()

install(TARGETS vaplayer-ui RUNTIME DESTINATION bin)
