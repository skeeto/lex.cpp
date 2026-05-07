cmake_minimum_required(VERSION 3.20)
project(lexcpp
    VERSION 0.1.0
    DESCRIPTION "Portable C++20 flex-compatible lexer generator"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(GNUInstallDirs)

# Centralised output layout: bin/ for executables, lib/ for archives.
# Multi-config generators (MSVC, Xcode) suffix per config automatically.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

option(LEXCPP_BUILD_TESTS "Build the CTest test suite" ON)
option(LEXCPP_BUILD_FUZZ  "Build libFuzzer harnesses (clang only)" OFF)
option(LEXCPP_COVERAGE    "Enable coverage instrumentation" OFF)
option(LEXCPP_SANITIZE    "Enable ASan+UBSan" OFF)
option(LEXCPP_WERROR      "Treat warnings as errors" OFF)

include(Warnings)
include(Sanitizers)
include(Coverage)

add_subdirectory(src)
add_subdirectory(lib)

if(LEXCPP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(LEXCPP_BUILD_FUZZ)
    add_subdirectory(fuzz)
endif()
