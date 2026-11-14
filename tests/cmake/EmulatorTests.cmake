# Private Common Test support, compiled on this build's VM and bootstrap compiler.
# Keep the suite list explicit: expanding the retained corpus is a separate task.
set(_beam_upstream_support "${CMAKE_CURRENT_SOURCE_DIR}/emulator/support")
set(_beam_support_inputs)
foreach(_file IN LISTS BEAM_EMULATOR_UPSTREAM_FILES)
    list(APPEND _beam_support_inputs "${_beam_upstream_support}/${_file}")
endforeach()
file(GLOB_RECURSE _beam_bootstrap_inputs CONFIGURE_DEPENDS
    LIST_DIRECTORIES FALSE "${CMAKE_SOURCE_DIR}/bootstrap/*")

set(_beam_suites prim_eval atomics counters num_bif process_max_heap_size)
set(_beam_case_counts 1 6 6 10 1)
set(_beam_suite_inputs)
foreach(_suite IN LISTS _beam_suites)
    list(APPEND _beam_suite_inputs "${CMAKE_SOURCE_DIR}/erts/emulator/test/${_suite}_SUITE.erl")
endforeach()

# The generated list passes the shared CMake selection to script-mode preparation.
set(_beam_support_files "${CMAKE_CURRENT_BINARY_DIR}/emulator/support-files.txt")
list(JOIN BEAM_EMULATOR_UPSTREAM_FILES "\n" _beam_support_files_content)
file(GENERATE OUTPUT "${_beam_support_files}" CONTENT "${_beam_support_files_content}\n")

# Removed glob members must invalidate preparation after CMake regenerates.
set(_beam_preparation_inputs ${_beam_support_inputs} ${_beam_bootstrap_inputs} ${_beam_suite_inputs})
string(SHA256 _beam_preparation_inputs_hash "${_beam_preparation_inputs}")
set(_beam_preparation_membership "${CMAKE_CURRENT_BINARY_DIR}/emulator/preparation-inputs.sha256")
file(GENERATE OUTPUT "${_beam_preparation_membership}" CONTENT "${_beam_preparation_inputs_hash}\n")

set(_beam_test_runtime "${CMAKE_CURRENT_BINARY_DIR}/emulator/runtime")
set(_beam_prepared "${CMAKE_CURRENT_BINARY_DIR}/emulator/prepared.stamp")
set(_beam_runtime_dependencies beam)
if(NOT WIN32)
    list(APPEND _beam_runtime_dependencies erl_child_setup)
endif()
set(_beam_test_args
    "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
    "-DBINARY_DIR=${CMAKE_BINARY_DIR}"
    "-DRUNTIME_BIN=$<TARGET_FILE_DIR:beam>"
    "-DEXPECTED_FLAVOR=${ERTS_BUILD_FLAVOR}"
    "-DEXPECTED_BUILD_TYPE=${_erts_build_type}"
    "-DSUPPORT_ROOT=${_beam_test_runtime}"
    "-DSUPPORT_FILES=${_beam_support_files}"
    "-DTEST_ID=${_erts_test_id}")
add_custom_command(OUTPUT "${_beam_prepared}"
    BYPRODUCTS "${_beam_test_runtime}/candidate.sha256"
    COMMAND "${CMAKE_COMMAND}" ${_beam_test_args} -DACTION=prepare
        -P "${CMAKE_CURRENT_LIST_DIR}/EmulatorTest.cmake"
    COMMAND "${CMAKE_COMMAND}" -E touch "${_beam_prepared}"
    DEPENDS ${_beam_runtime_dependencies} ${_beam_support_inputs} ${_beam_bootstrap_inputs}
        ${_beam_suite_inputs} "${CMAKE_SOURCE_DIR}/UPSTREAM_COMMIT"
        "${_beam_support_files}" "${_beam_preparation_membership}"
        "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
        "${CMAKE_CURRENT_LIST_DIR}/EmulatorTests.cmake"
        "${CMAKE_CURRENT_LIST_DIR}/EmulatorTest.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/emulator/beam_emulator_harness.erl"
    COMMENT "Compiling private Common Test support and five emulator suites"
    VERBATIM)
add_custom_target(beam_emulator_test_support ALL DEPENDS "${_beam_prepared}")

foreach(_index RANGE 0 4)
    list(GET _beam_suites ${_index} _suite)
    list(GET _beam_case_counts ${_index} _count)
    add_test(NAME "emulator.${_suite}"
        COMMAND "${CMAKE_COMMAND}" ${_beam_test_args}
            -DACTION=run "-DSUITE=${_suite}_SUITE" "-DEXPECTED_COUNT=${_count}"
            -P "${CMAKE_CURRENT_LIST_DIR}/EmulatorTest.cmake")
    set_tests_properties("emulator.${_suite}" PROPERTIES
        LABELS "emulator;${ERTS_BUILD_FLAVOR}"
        RUN_SERIAL TRUE TIMEOUT 600)
endforeach()

add_custom_target(check-emulator
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}"
        -L "^emulator$" --output-on-failure --no-tests=error -j1
    DEPENDS beam_emulator_test_support
    USES_TERMINAL VERBATIM)
