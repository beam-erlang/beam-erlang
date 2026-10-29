cmake_minimum_required(VERSION 3.25)
project(beam_returning_sdk_client LANGUAGES C CXX)
find_package(BEAM CONFIG REQUIRED)
if(NOT EXPECTED_VERSION)
    message(FATAL_ERROR "EXPECTED_VERSION is required")
endif()
beam_check_runtime_sdk(VERSION "${EXPECTED_VERSION}")
set(_module "${CMAKE_CURRENT_BINARY_DIR}/sdk_returning.beam")
add_custom_command(OUTPUT "${_module}"
    COMMAND $<TARGET_FILE:BEAM::CLI> compile
        --depfile "${_module}.d" -o "${CMAKE_CURRENT_BINARY_DIR}" --
        "${CMAKE_CURRENT_SOURCE_DIR}/sdk_returning.erl"
    DEPFILE "${_module}.d"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/sdk_returning.erl" BEAM::CLI
    VERBATIM)
add_custom_target(sdk_returning_module ALL DEPENDS "${_module}")
include("${TEST_SUPPORT_DIR}/HostObservation.cmake")
foreach(_language c cxx)
    if(_language STREQUAL c)
        set(_source main.c)
    else()
        set(_source main.cpp)
    endif()
    add_executable(returning_${_language}_host "${_source}")
    beam_test_observe(returning_${_language}_host)
    target_compile_definitions(returning_${_language}_host PRIVATE EXPECTED_BEAM_VERSION="${EXPECTED_VERSION}")
    target_link_libraries(returning_${_language}_host PRIVATE BEAM::Host BEAM::Runtime ${CMAKE_DL_LIBS})
    add_dependencies(returning_${_language}_host sdk_returning_module)
    target_include_directories(returning_${_language}_host PRIVATE "${TEST_SUPPORT_DIR}")
    beam_target_bytecode_verifier(returning_${_language}_host INVENTORIES base boot services)
    beam_target_bytecode_verifier(returning_${_language}_host INVENTORIES base)
endforeach()

# Each documented public header owns its dependencies and C++ linkage.
foreach(_header IN ITEMS beam.h runtime.h boot.h library.h native_image.h native.h sdk/bytecode.h sdk/inventories.h)
    string(REPLACE "/" "_" _name "${_header}")
    string(REPLACE "." "_" _name "${_name}")
    foreach(_language IN ITEMS c cpp)
        set(_source "${CMAKE_CURRENT_BINARY_DIR}/${_name}.${_language}")
        file(WRITE "${_source}" "#include <beam/${_header}>\n")
        add_library(header_${_name}_${_language} OBJECT "${_source}")
        target_link_libraries(header_${_name}_${_language} PRIVATE BEAM::Runtime)
    endforeach()
endforeach()
