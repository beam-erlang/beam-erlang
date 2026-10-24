cmake_minimum_required(VERSION 3.25)
project(BEAMVMCommonTest NONE)
include(CTest)
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)

foreach(_name BEAM_SDK_ROOT BEAM_VM_TEST_OTP_PREFIX BEAM_VM_TEST_OTP_PROVENANCE)
    set(${_name} "" CACHE PATH "Explicit installed VM test input")
    if(NOT IS_ABSOLUTE "${${_name}}" OR NOT EXISTS "${${_name}}")
        message(FATAL_ERROR "${_name} must name an absolute existing input")
    endif()
endforeach()
foreach(_name BEAM_VM_TEST_SDK_VERSION BEAM_VM_TEST_SDK_BUILD_ID BEAM_VM_TEST_FLAVOR)
    set(${_name} "" CACHE STRING "Independent expected SDK identity")
    if("${${_name}}" STREQUAL "")
        message(FATAL_ERROR "${_name} must be selected independently of the installed prefix")
    endif()
endforeach()
get_filename_component(_tests "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(_selection
    --sdk "${BEAM_SDK_ROOT}"
    --otp-prefix "${BEAM_VM_TEST_OTP_PREFIX}"
    --provenance "${BEAM_VM_TEST_OTP_PROVENANCE}"
    --sdk-version "${BEAM_VM_TEST_SDK_VERSION}"
    --sdk-build-id "${BEAM_VM_TEST_SDK_BUILD_ID}"
    --flavor "${BEAM_VM_TEST_FLAVOR}")
execute_process(COMMAND "${Python3_EXECUTABLE}" -B -m vm.common_test.runner
    ${_selection} --validate-only WORKING_DIRECTORY "${_tests}"
    RESULT_VARIABLE _verified OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
if(NOT _verified EQUAL 0)
    message(FATAL_ERROR "VM test input validation failed:\n${_output}${_error}")
endif()
foreach(_suite atomics_SUITE counters_SUITE bs_bincomp_SUITE bs_match_tail_SUITE)
    add_test(NAME "vm-ct-${_suite}"
        COMMAND "${Python3_EXECUTABLE}" -B -m vm.common_test.runner
            ${_selection} --suite "${_suite}"
            --test-dir "${CMAKE_CURRENT_BINARY_DIR}/results/${_suite}")
    set_tests_properties("vm-ct-${_suite}" PROPERTIES
        WORKING_DIRECTORY "${_tests}" TIMEOUT 660 RUN_SERIAL TRUE
        LABELS "vm;shared;common-test;installed;otp-hosted")
endforeach()
