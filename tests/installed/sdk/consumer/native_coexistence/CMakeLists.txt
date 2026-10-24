cmake_minimum_required(VERSION 3.25)
project(beam_native_coexistence LANGUAGES C)
find_package(BEAM CONFIG REQUIRED)
if(NOT EXPECTED_VERSION)
    message(FATAL_ERROR "EXPECTED_VERSION is required")
endif()
beam_check_runtime_sdk(VERSION "${EXPECTED_VERSION}")
find_library(PCRE2_LIBRARY NAMES pcre2-8 REQUIRED)
find_path(PCRE2_INCLUDE_DIR pcre2.h REQUIRED)
find_package(ZLIB REQUIRED)

# Put libbeam first to expose accidental interposition of its private libraries.
add_executable(native_coexistence main.c)
target_link_libraries(native_coexistence PRIVATE BEAM::Runtime BEAM::NIF "${PCRE2_LIBRARY}" ZLIB::ZLIB ${CMAKE_DL_LIBS})

target_include_directories(native_coexistence PRIVATE "${PCRE2_INCLUDE_DIR}")
