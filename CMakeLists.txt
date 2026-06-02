cmake_minimum_required(VERSION 3.15)
project(ParryShell)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)


set_source_files_properties(window.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")

find_library(COCOA_LIBRARY Cocoa)
find_library(APPKIT_LIBRARY AppKit)


find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_executable(ParryShell main.cpp
        window.cpp
        window.h
        shell.cpp
        shell.h
        parser.cpp
        parser.h
        history.cpp
        colors.h
        history.h)

#I did experiment a lot with the GUI kit, so there could be multiple libraries here, not important.
target_link_libraries(ParryShell
        ${COCOA_LIBRARY}
        ${APPKIT_LIBRARY}
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)