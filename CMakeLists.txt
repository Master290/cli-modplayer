cmake_minimum_required(VERSION 3.18)
project(modtracker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CheckIncludeFile)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
pkg_check_modules(OPENMPT REQUIRED libopenmpt)
pkg_check_modules(NCURSES REQUIRED ncursesw)

add_library(note_formatter STATIC
    src/note_formatter.cpp
)
target_include_directories(note_formatter
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(note_formatter
    PRIVATE
        -Wall -Wextra -Wpedantic
)

add_executable(modtracker
    src/main.cpp
    src/player.cpp
    src/ui.cpp
)

target_link_libraries(modtracker
    PRIVATE
        note_formatter
        ${PORTAUDIO_LIBRARIES}
        ${OPENMPT_LIBRARIES}
        ${NCURSES_LIBRARIES}
)

target_include_directories(modtracker
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${PORTAUDIO_INCLUDE_DIRS}
        ${OPENMPT_INCLUDE_DIRS}
        ${NCURSES_INCLUDE_DIRS}
)

target_compile_options(modtracker
    PRIVATE
        -Wall -Wextra -Wpedantic
        ${PORTAUDIO_CFLAGS_OTHER}
        ${OPENMPT_CFLAGS_OTHER}
        ${NCURSES_CFLAGS_OTHER}
)

target_link_directories(modtracker
    PRIVATE
        ${PORTAUDIO_LIBRARY_DIRS}
        ${OPENMPT_LIBRARY_DIRS}
        ${NCURSES_LIBRARY_DIRS}
)

add_executable(note_formatter_tests
    tests/test_note_formatter.cpp
)

target_link_libraries(note_formatter_tests
    PRIVATE
        note_formatter
)

target_compile_options(note_formatter_tests
    PRIVATE
        -Wall -Wextra -Wpedantic
)

enable_testing()
add_test(NAME note_formatter_tests COMMAND note_formatter_tests)
