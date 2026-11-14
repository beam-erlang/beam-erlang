cmake_minimum_required(VERSION 3.25)

foreach(_required ACTION SOURCE_DIR BINARY_DIR RUNTIME_BIN EXPECTED_FLAVOR
        EXPECTED_BUILD_TYPE SUPPORT_ROOT SUPPORT_FILES TEST_ID)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "Missing emulator test input: ${_required}")
    endif()
endforeach()
if(NOT ACTION MATCHES "^(prepare|run)$" OR NOT TEST_ID MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "Invalid emulator test action or build identity")
endif()
if(NOT EXPECTED_BUILD_TYPE MATCHES "^(opt|debug)$")
    message(FATAL_ERROR "Unknown emulator build type: ${EXPECTED_BUILD_TYPE}")
endif()
get_filename_component(_expected_support "${BINARY_DIR}/tests/emulator/runtime" ABSOLUTE)
cmake_path(SET _expected_support NORMALIZE "${_expected_support}")
cmake_path(ABSOLUTE_PATH SUPPORT_ROOT NORMALIZE OUTPUT_VARIABLE SUPPORT_ROOT)
set(_support_key "${SUPPORT_ROOT}")
set(_expected_support_key "${_expected_support}")
if(WIN32)
    string(TOLOWER "${_support_key}" _support_key)
    string(TOLOWER "${_expected_support_key}" _expected_support_key)
endif()
if(NOT _support_key STREQUAL _expected_support_key)
    message(FATAL_ERROR "Refusing unexpected emulator support directory: ${SUPPORT_ROOT}")
endif()
if(WIN32)
    set(_beam_name beam.exe)
    if(NOT EXISTS "${RUNTIME_BIN}/${_beam_name}")
        message(FATAL_ERROR "Missing candidate program: ${RUNTIME_BIN}/${_beam_name}")
    endif()
    foreach(_alias beam.smp.exe "beam.${EXPECTED_FLAVOR}.exe")
        if(NOT EXISTS "${RUNTIME_BIN}/${_alias}")
            message(FATAL_ERROR "Missing candidate flavor copy: ${RUNTIME_BIN}/${_alias}")
        endif()
    endforeach()
else()
    set(_beam_name beam)
    foreach(_program beam erl_child_setup)
        if(NOT EXISTS "${RUNTIME_BIN}/${_program}")
            message(FATAL_ERROR "Missing candidate program: ${RUNTIME_BIN}/${_program}")
        endif()
    endforeach()
endif()
set(_child_alias_target "")
if(EXPECTED_BUILD_TYPE STREQUAL "debug" AND NOT WIN32)
    if(NOT IS_SYMLINK "${RUNTIME_BIN}/erl_child_setup.debug")
        message(FATAL_ERROR "Missing candidate Debug helper alias: ${RUNTIME_BIN}/erl_child_setup.debug")
    endif()
    file(READ_SYMLINK "${RUNTIME_BIN}/erl_child_setup.debug" _child_alias_target)
    if(NOT _child_alias_target STREQUAL "erl_child_setup")
        message(FATAL_ERROR "Candidate Debug helper alias must point to erl_child_setup, got ${_child_alias_target}")
    endif()
endif()
get_filename_component(_workspace "${SOURCE_DIR}/.." ABSOLUTE)
if(ACTION STREQUAL "prepare")
    set(_job prepare)
else()
    if(NOT SUITE MATCHES "^[a-z][a-z0-9_]*_SUITE$" OR NOT EXPECTED_COUNT MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "Invalid emulator suite or expected testcase count")
    endif()
    set(_job "${SUITE}")
endif()
set(_scratch "${_workspace}/tmp/beam-emulator-${TEST_ID}-${_job}")
file(MAKE_DIRECTORY "${_workspace}/tmp")
file(LOCK "${_scratch}.lock" GUARD PROCESS TIMEOUT 10)
# The scratch name and parent are constructed here, never supplied by callers.
file(REMOVE_RECURSE "${_scratch}")
file(MAKE_DIRECTORY "${_scratch}/home" "${_scratch}/work" "${_scratch}/logs")
file(WRITE "${_scratch}/inetrc" "{lookup,[file]}.\n{domain,\"beam.test\"}.\n")

file(SHA256 "${RUNTIME_BIN}/${_beam_name}" _beam_hash)
if(WIN32)
    set(_child_hash "windows-no-erl_child_setup")
else()
    file(SHA256 "${RUNTIME_BIN}/erl_child_setup" _child_hash)
endif()
file(READ "${SOURCE_DIR}/UPSTREAM_COMMIT" _pin)
string(STRIP "${_pin}" _pin)
file(STRINGS "${SUPPORT_FILES}" _support_files)
if(NOT _support_files)
    message(FATAL_ERROR "Empty imported test support selection: ${SUPPORT_FILES}")
endif()
file(SHA256 "${SUPPORT_FILES}" _support_list_hash)
set(_support_identity "")
foreach(_relative IN LISTS _support_files)
    if(NOT _relative MATCHES "^lib/" OR _relative MATCHES "(^|/)\\.\\.(/|$)")
        message(FATAL_ERROR "Invalid imported test support path: ${_relative}")
    endif()
    file(SHA256 "${SOURCE_DIR}/tests/emulator/support/${_relative}" _actual_hash)
    string(APPEND _support_identity "${_relative}=${_actual_hash}\n")
endforeach()
string(SHA256 _support_hash "${_support_identity}")
file(WRITE "${_scratch}/support-inputs.sha256" "${_support_identity}")
file(SHA256 "${SOURCE_DIR}/bootstrap/lib/compiler/ebin/compile.beam" _compiler_hash)
file(SHA256 "${SOURCE_DIR}/tests/emulator/beam_emulator_harness.erl" _harness_hash)
file(SHA256 "${CMAKE_CURRENT_LIST_FILE}" _driver_hash)
set(_prepared_identity "${_beam_hash}\n${_child_hash}\n${_support_hash}\n${_compiler_hash}\n${_harness_hash}\n${_driver_hash}\n${_pin}\n${EXPECTED_FLAVOR}\n${EXPECTED_BUILD_TYPE}\n${_child_alias_target}\n")
# Changing the shared selection or preparation rules also invalidates direct CTest runs.
string(APPEND _prepared_identity "support_list=${_support_list_hash}\n")
foreach(_cmake_input CMakeLists.txt cmake/EmulatorTests.cmake)
    file(SHA256 "${SOURCE_DIR}/tests/${_cmake_input}" _cmake_hash)
    string(APPEND _prepared_identity "${_cmake_input}=${_cmake_hash}\n")
endforeach()
file(WRITE "${_scratch}/identity.txt"
    "upstream_commit=${_pin}\nbeam=${RUNTIME_BIN}/${_beam_name}\nbeam_sha256=${_beam_hash}\nchild_setup_sha256=${_child_hash}\nflavor=${EXPECTED_FLAVOR}\nsupport_sources_sha256=${_support_hash}\nbootstrap_compiler_sha256=${_compiler_hash}\nharness_source_sha256=${_harness_hash}\nhost=${CMAKE_HOST_SYSTEM}\ncmake=${CMAKE_VERSION}\n")
file(APPEND "${_scratch}/identity.txt"
    "build_type=${EXPECTED_BUILD_TYPE}\nchild_setup_debug_alias_target=${_child_alias_target}\n")
foreach(_selected prim_eval atomics counters num_bif process_max_heap_size)
    set(_suite_source "${SOURCE_DIR}/erts/emulator/test/${_selected}_SUITE.erl")
    file(SHA256 "${_suite_source}" _suite_hash)
    file(APPEND "${_scratch}/identity.txt" "${_selected}_SUITE_source_sha256=${_suite_hash}\n")
    string(APPEND _prepared_identity "${_selected}_SUITE=${_suite_hash}\n")
endforeach()
# The fixture is copied, so direct CTest runs must reject changed bootstrap
# inputs just as a normal build would rebuild the preparation target.
file(GLOB_RECURSE _bootstrap_files LIST_DIRECTORIES FALSE RELATIVE "${SOURCE_DIR}/bootstrap"
    "${SOURCE_DIR}/bootstrap/*")
set(_bootstrap_identity "")
foreach(_relative IN LISTS _bootstrap_files)
    file(SHA256 "${SOURCE_DIR}/bootstrap/${_relative}" _hash)
    string(APPEND _bootstrap_identity "${_relative}=${_hash}\n")
endforeach()
string(SHA256 _bootstrap_hash "${_bootstrap_identity}")
string(APPEND _prepared_identity "bootstrap=${_bootstrap_hash}\n")
file(APPEND "${_scratch}/identity.txt" "bootstrap_sha256=${_bootstrap_hash}\n")

find_program(_git git)
if(_git)
    execute_process(COMMAND "${_git}" -C "${SOURCE_DIR}" rev-parse HEAD
        OUTPUT_VARIABLE _head OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND "${_git}" -C "${SOURCE_DIR}" status --short
        OUTPUT_VARIABLE _status ERROR_QUIET)
    file(APPEND "${_scratch}/identity.txt" "source_head=${_head}\nsource_status:\n${_status}")
endif()
file(COPY "${SUPPORT_FILES}" DESTINATION "${_scratch}")
if(EXISTS "${BINARY_DIR}/CMakeCache.txt")
    file(STRINGS "${BINARY_DIR}/CMakeCache.txt" _toolchain
        REGEX "^(CMAKE_(C|CXX)_COMPILER|CMAKE_OSX_(SYSROOT|DEPLOYMENT_TARGET|ARCHITECTURES)):")
    foreach(_line IN LISTS _toolchain)
        file(APPEND "${_scratch}/identity.txt" "${_line}\n")
    endforeach()
endif()

if(ACTION STREQUAL "prepare")
    file(REMOVE_RECURSE "${SUPPORT_ROOT}")
    file(MAKE_DIRECTORY "${SUPPORT_ROOT}")
    file(COPY "${SOURCE_DIR}/bootstrap/" DESTINATION "${SUPPORT_ROOT}")
    foreach(_relative IN LISTS _support_files)
        set(_source "${SOURCE_DIR}/tests/emulator/support/${_relative}")
        get_filename_component(_parent "${SUPPORT_ROOT}/${_relative}" DIRECTORY)
        file(MAKE_DIRECTORY "${_parent}")
        file(COPY "${_source}" DESTINATION "${_parent}")
    endforeach()
    set(_arguments "${SUPPORT_ROOT}" "${SOURCE_DIR}/erts/emulator/test" "${_scratch}"
        "${EXPECTED_FLAVOR}" "${EXPECTED_BUILD_TYPE}")
else()
    if(NOT EXISTS "${SUPPORT_ROOT}/candidate.sha256")
        message(FATAL_ERROR "Emulator support is not prepared; build beam_emulator_test_support first")
    endif()
    file(READ "${SUPPORT_ROOT}/candidate.sha256" _prepared_hash)
    if(NOT _prepared_hash STREQUAL _prepared_identity)
        message(FATAL_ERROR "Candidate or support identity changed; rebuild beam_emulator_test_support")
    endif()
    set(_arguments "${SUPPORT_ROOT}" "${SUITE}" "${EXPECTED_COUNT}" "${_scratch}/logs"
        "${EXPECTED_FLAVOR}" "${EXPECTED_BUILD_TYPE}")
endif()

set(_eval [=[
try
    [Harness, Action, Flavor | Arguments] = init:get_plain_arguments(),
    _ = code:del_path("."),
    Flavor = atom_to_list(erlang:system_info(emu_flavor)),
    {ok, beam_emulator_harness, Binary} = compile:file(Harness, [binary, return_errors]),
    {module, beam_emulator_harness} = code:load_binary(beam_emulator_harness, Harness, Binary),
    ok = apply(beam_emulator_harness, list_to_existing_atom(Action), [Arguments]),
    erlang:halt(0)
catch Class:Reason:Stack ->
    erlang:display({emulator_harness_failed, Class, Reason, Stack}),
    erlang:halt(1)
end.
]=])
string(REPLACE "\r" "" _eval "${_eval}")
string(REPLACE "\n" " " _eval "${_eval}")
set(_kernel_poll true)
if(WIN32)
    set(_kernel_poll false)
endif()
set(_boot_args
    -S 2:2 -SDcpu 1 -SDio 1 -K "${_kernel_poll}" --
    -root "${SUPPORT_ROOT}" -bindir "${RUNTIME_BIN}" -progname beam
    -home "${_scratch}/home" -boot "${SUPPORT_ROOT}/bin/start_clean"
    -path "${SUPPORT_ROOT}/lib/kernel/ebin" "${SUPPORT_ROOT}/lib/stdlib/ebin"
        "${SUPPORT_ROOT}/lib/compiler/ebin" "${SUPPORT_ROOT}/lib/syntax_tools/ebin"
        "${SUPPORT_ROOT}/lib/common_test/ebin" "${SUPPORT_ROOT}/support" "${SUPPORT_ROOT}/tests"
    -noshell -noinput -eval "${_eval}" -extra
        "${SOURCE_DIR}/tests/emulator/beam_emulator_harness.erl" "${ACTION}" "${EXPECTED_FLAVOR}" ${_arguments})
if(WIN32)
    file(TO_CMAKE_PATH "$ENV{SystemRoot}" _system_root)
    set(_system32 "${_system_root}/System32")
    if(NOT _system_root MATCHES "^([A-Za-z]):")
        message(FATAL_ERROR "SystemRoot has no drive letter: ${_system_root}")
    endif()
    set(_system_drive "${CMAKE_MATCH_1}:")
    set(_line "\"${RUNTIME_BIN}/${_beam_name}\"")
    foreach(_arg IN LISTS _boot_args)
        string(REPLACE "\"" "\\\"" _quoted "${_arg}")
        string(APPEND _line " \"${_quoted}\"")
    endforeach()
    set(_script "${_scratch}/boot.cmd")
    file(WRITE "${_script}"
"@echo off
setlocal EnableExtensions
for /f \"tokens=1,* delims==\" %%A in ('set') do set \"%%A=\"
set \"PATH=${_system32}\"
set \"SystemRoot=${_system_root}\"
set \"WINDIR=${_system_root}\"
set \"SYSTEMDRIVE=${_system_drive}\"
set \"COMSPEC=${_system32}/cmd.exe\"
set \"PATHEXT=.COM;.EXE;.BAT;.CMD\"
set \"HOME=${_scratch}/home\"
set \"TMP=${_scratch}/work\"
set \"TEMP=${_scratch}/work\"
set \"BINDIR=${RUNTIME_BIN}\"
set \"ERL_INETRC=${_scratch}/inetrc\"
set \"ERL_CRASH_DUMP=${_scratch}/erl_crash.dump\"
${_line}
set \"RC=%ERRORLEVEL%\"
exit /b %RC%
")
    set(_boot_command "${_script}")
else()
    set(_boot_command /usr/bin/env -i
        "PATH=/usr/bin:/bin:/usr/sbin:/sbin"
        "HOME=${_scratch}/home" "TMPDIR=${_scratch}/work"
        "BINDIR=${RUNTIME_BIN}" "ERL_INETRC=${_scratch}/inetrc"
        "ERL_CRASH_DUMP=${_scratch}/erl_crash.dump"
        "${RUNTIME_BIN}/beam" ${_boot_args})
endif()
execute_process(COMMAND ${_boot_command}
    WORKING_DIRECTORY "${_scratch}/work"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr
    TIMEOUT 570)
file(WRITE "${_scratch}/vm.log" "${_stdout}${_stderr}")
if(NOT "${_result}" STREQUAL "0")
    message(FATAL_ERROR "Emulator ${_job} failed (${_result}). Evidence: ${_scratch}\n${_stdout}${_stderr}")
endif()
if(ACTION STREQUAL "prepare")
    file(WRITE "${SUPPORT_ROOT}/candidate.sha256" "${_prepared_identity}")
endif()
message(STATUS "Emulator ${_job} passed. Evidence: ${_scratch}\n${_stdout}")
