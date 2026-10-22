cmake_minimum_required(VERSION 3.25)
project(beam_native_coexistence LANGUAGES C)
find_package(BEAM CONFIG REQUIRED)
if(NOT EXPECTED_VERSION)
    message(FATAL_ERROR "EXPECTED_VERSION is required")
endif()
beam_check_runtime_sdk(VERSION "${EXPECTED_VERSION}")
find_package(PkgConfig REQUIRED)
pkg_check_modules(PCRE2 REQUIRED IMPORTED_TARGET libpcre2-8)
find_package(ZLIB REQUIRED)

# Put libbeam first to expose accidental interposition of its private libraries.
add_executable(native_coexistence main.c)
target_link_libraries(native_coexistence PRIVATE BEAM::Runtime BEAM::NIF PkgConfig::PCRE2 ZLIB::ZLIB ${CMAKE_DL_LIBS})
