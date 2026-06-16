cmake_minimum_required(VERSION 3.25)
project(beam_returning_sdk_client LANGUAGES C CXX)
find_package(BEAM 29.0 EXACT CONFIG REQUIRED)
set(_module "${CMAKE_CURRENT_BINARY_DIR}/sdk_returning.beam")
add_custom_command(OUTPUT "${_module}"
    COMMAND $<TARGET_FILE:BEAM::CLI> compile
        --depfile "${_module}.d" -o "${CMAKE_CURRENT_BINARY_DIR}" --
        "${CMAKE_CURRENT_SOURCE_DIR}/sdk_returning.erl"
    DEPFILE "${_module}.d"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/sdk_returning.erl" BEAM::CLI
    VERBATIM)
add_custom_target(sdk_returning_module ALL DEPENDS "${_module}")
foreach(_language c cxx)
    if(_language STREQUAL c)
        set(_source main.c)
    else()
        set(_source main.cpp)
    endif()
    add_executable(returning_${_language}_host "${_source}")
    target_link_libraries(returning_${_language}_host PRIVATE BEAM::Runtime ${CMAKE_DL_LIBS})
    add_dependencies(returning_${_language}_host sdk_returning_module)
    target_include_directories(returning_${_language}_host PRIVATE "${TEST_SUPPORT_DIR}")
    beam_target_bytecode_verifier(returning_${_language}_host INVENTORIES base boot services)
endforeach()
