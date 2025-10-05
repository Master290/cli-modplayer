cmake_minimum_required(VERSION 3.18)
project(cli-modplayer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CheckIncludeFile)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
pkg_check_modules(OPENMPT REQUIRED libopenmpt)

pkg_check_modules(FLAC flac)
pkg_check_modules(LAME lame)

if(NOT LAME_FOUND)
    find_library(LAME_LIBRARIES NAMES mp3lame)
    find_path(LAME_INCLUDE_DIRS NAMES lame/lame.h)
    if(LAME_LIBRARIES AND LAME_INCLUDE_DIRS)
        set(LAME_FOUND TRUE)
        message(STATUS "Found LAME manually: ${LAME_LIBRARIES}")
    endif()
endif()

include(FetchContent)
FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui.git
    GIT_TAG v5.0.0
)
FetchContent_MakeAvailable(ftxui)

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

add_executable(cli-modplayer
    src/main.cpp
    src/player.cpp
    src/ui.cpp
    src/config.cpp
    src/audio_effects.cpp
    src/audio_exporter.cpp
    src/file_browser.cpp
    src/file_browser_ui.cpp
    src/simple_ui.cpp
)

target_link_libraries(cli-modplayer
    PRIVATE
        note_formatter
        ${PORTAUDIO_LIBRARIES}
        ${OPENMPT_LIBRARIES}
        ftxui::component
)

if(FLAC_FOUND)
    target_link_libraries(cli-modplayer PRIVATE ${FLAC_LIBRARIES})
    target_compile_definitions(cli-modplayer PRIVATE HAVE_FLAC)
    message(STATUS "FLAC support enabled")
else()
    message(STATUS "FLAC support disabled (libFLAC not found)")
endif()

if(LAME_FOUND)
    target_link_libraries(cli-modplayer PRIVATE ${LAME_LIBRARIES})
    target_compile_definitions(cli-modplayer PRIVATE HAVE_LAME)
    message(STATUS "MP3 support enabled")
else()
    message(STATUS "MP3 support disabled (LAME library not found)")
endif()

target_include_directories(cli-modplayer
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${PORTAUDIO_INCLUDE_DIRS}
        ${OPENMPT_INCLUDE_DIRS}
)

if(FLAC_FOUND)
    target_include_directories(cli-modplayer PRIVATE ${FLAC_INCLUDE_DIRS})
endif()

if(LAME_FOUND)
    target_include_directories(cli-modplayer PRIVATE ${LAME_INCLUDE_DIRS})
endif()

target_compile_options(cli-modplayer
    PRIVATE
        -Wall -Wextra -Wpedantic
        ${PORTAUDIO_CFLAGS_OTHER}
        ${OPENMPT_CFLAGS_OTHER}
)

target_link_directories(cli-modplayer
    PRIVATE
        ${PORTAUDIO_LIBRARY_DIRS}
        ${OPENMPT_LIBRARY_DIRS}
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
