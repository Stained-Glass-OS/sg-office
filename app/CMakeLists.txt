# SG Office -- the program: a window per document, hosting the editors in
# QtWebEngine (Debian's, built from source), with the engine beside it.
# Copyright (C) 2026 Stained Glass OS contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
cmake_minimum_required(VERSION 3.25)
project(sg-office LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets WebEngineCore WebEngineWidgets)

add_executable(sg-office
    src/main.cpp
    src/paths.cpp
    src/fontcache.cpp
    src/formats.cpp
    src/document.cpp
    src/schemehandler.cpp
    src/titlebar.cpp
    src/window.cpp
    res/sg-office.qrc)
target_compile_options(sg-office PRIVATE -Wall -Wextra)
target_link_libraries(sg-office PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::WebEngineCore Qt6::WebEngineWidgets)
install(TARGETS sg-office RUNTIME DESTINATION bin)
