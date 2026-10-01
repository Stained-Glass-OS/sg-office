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

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Network PrintSupport WebEngineCore WebEngineWidgets)

add_executable(sg-office
    src/main.cpp
    src/paths.cpp
    src/fontcache.cpp
    src/formats.cpp
    src/document.cpp
    src/schemehandler.cpp
    src/titlebar.cpp
    src/window.cpp
    src/appicon.cpp
    src/office.cpp
    src/printing.cpp
    res/sg-office.qrc)
target_compile_options(sg-office PRIVATE -Wall -Wextra)
target_link_libraries(sg-office PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::PrintSupport
                      Qt6::WebEngineCore Qt6::WebEngineWidgets xcb)
# SG_MUTANTS="A;B": a build with those fixes reverted (#ifdef SG_MUTANT_A ...), for
# make test-mutation
if(SG_MUTANTS)
    foreach(m ${SG_MUTANTS})
        target_compile_definitions(sg-office PRIVATE SG_MUTANT_${m})
    endforeach()
endif()
install(TARGETS sg-office RUNTIME DESTINATION bin)
