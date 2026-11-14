cmake_minimum_required(VERSION 3.25)

foreach(_required MODE SOURCE_DIR BINARY_DIR RUNTIME_BIN INSTALL_BINDIR
        EXPECTED_FLAVOR EXPECTED_BUILD_TYPE EXPECTED_BACKEND EXPECTED_KERNEL_POLL
        EXPECTED_ARCH TARGET_SYSTEM TEST_ROOT)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "Missing runtime test input: ${_required}")
    endif()
endforeach()
if(NOT MODE MATCHES "^(build|install)$")
    message(FATAL_ERROR "Unknown runtime test mode: ${MODE}")
endif()
if(NOT EXPECTED_BUILD_TYPE MATCHES "^(opt|debug)$")
    message(FATAL_ERROR "Unknown runtime build type: ${EXPECTED_BUILD_TYPE}")
endif()
if(NOT EXPECTED_KERNEL_POLL MATCHES "^(true|false)$")
    message(FATAL_ERROR "Unknown kernel poll expectation: ${EXPECTED_KERNEL_POLL}")
endif()
if(MODE STREQUAL "install" AND IS_ABSOLUTE "${INSTALL_BINDIR}")
    message(STATUS "Runtime install integration skipped: absolute CMAKE_INSTALL_BINDIR cannot be relocated safely inside the disposable prefix")
    return()
endif()

# Only remove the disposable directory belonging to this exact build/test pair.
get_filename_component(_workspace "${SOURCE_DIR}/.." ABSOLUTE)
cmake_path(SET _workspace NORMALIZE "${_workspace}")
cmake_path(ABSOLUTE_PATH TEST_ROOT NORMALIZE OUTPUT_VARIABLE TEST_ROOT)
cmake_path(SET _expected_parent NORMALIZE "${_workspace}/tmp")
set(_workspace_key "${_workspace}")
set(_test_root_key "${TEST_ROOT}")
set(_parent_key "${_expected_parent}")
if(TARGET_SYSTEM STREQUAL "Windows")
    string(TOLOWER "${_workspace_key}" _workspace_key)
    string(TOLOWER "${_test_root_key}" _test_root_key)
    string(TOLOWER "${_parent_key}" _parent_key)
endif()
cmake_path(IS_PREFIX _workspace_key "${_test_root_key}" NORMALIZE _inside_workspace)
cmake_path(GET _test_root_key FILENAME _test_name)
cmake_path(GET _test_root_key PARENT_PATH _test_parent)
if(NOT _inside_workspace OR NOT _test_parent STREQUAL "${_parent_key}"
        OR NOT _test_name MATCHES "^beam-runtime-[0-9a-f]+-(build|install)$")
    message(FATAL_ERROR "Refusing unsafe runtime test directory: ${TEST_ROOT}")
endif()
if(MODE STREQUAL "install")
    set(_probe_prefix "${TEST_ROOT}/install prefix with spaces")
    set(_probe_bindir "${_probe_prefix}/${INSTALL_BINDIR}")
    cmake_path(IS_PREFIX _probe_prefix "${_probe_bindir}" NORMALIZE _inside_prefix)
    if(NOT _inside_prefix)
        message(FATAL_ERROR "Refusing CMAKE_INSTALL_BINDIR that escapes the disposable install prefix: ${INSTALL_BINDIR}")
    endif()
endif()

file(MAKE_DIRECTORY "${_workspace}/tmp")
file(LOCK "${TEST_ROOT}.lock" GUARD PROCESS TIMEOUT 10)
file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}/fixture" "${TEST_ROOT}/home" "${TEST_ROOT}/work")
file(COPY "${SOURCE_DIR}/bootstrap/" DESTINATION "${TEST_ROOT}/fixture")
file(COPY "${SOURCE_DIR}/tests/runtime/beam_runtime_smoke.erl" DESTINATION "${TEST_ROOT}/work")
# The bootstrap's Linux kernel otherwise resolves the machine hostname at
# startup through inet_gethost. These tests use numeric loopback addresses and
# deliberately provide only the VM's child helper, so select a local-only
# resolver and a fixed test domain before starting the fixture kernel.
file(WRITE "${TEST_ROOT}/inetrc" "{lookup, [file]}.\n{domain, \"beam.test\"}.\n")
set(_fixture "${TEST_ROOT}/fixture")
include("${SOURCE_DIR}/erts/emulator/cmake/StageDestDir.cmake")

function(erts_checked_command label)
    execute_process(COMMAND ${ARGN}
        WORKING_DIRECTORY "${TEST_ROOT}/work"
        RESULT_VARIABLE _result OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr
        TIMEOUT 30)
    file(WRITE "${TEST_ROOT}/${label}.log" "${_stdout}${_stderr}")
    if(NOT "${_result}" STREQUAL "0")
        message(FATAL_ERROR
            "${label} failed (${_result}). Evidence: ${TEST_ROOT}\n${_stdout}${_stderr}")
    endif()
    set(ERTS_COMMAND_OUTPUT "${_stdout}${_stderr}" PARENT_SCOPE)
endfunction()

function(erts_normalize_installed_path input out_var)
    file(TO_CMAKE_PATH "${input}" _normalized)
    cmake_path(SET _normalized NORMALIZE "${_normalized}")
    if(TARGET_SYSTEM STREQUAL "Windows")
        # DESTDIR installs record the same path with the drive letter removed.
        if(_normalized MATCHES "^[A-Za-z]:(.*)$")
            set(_normalized "${CMAKE_MATCH_1}")
        endif()
        string(TOLOWER "${_normalized}" _normalized)
    endif()
    set(${out_var} "${_normalized}" PARENT_SCOPE)
endfunction()

function(erts_require_same_file left right)
    if(NOT EXISTS "${left}")
        message(FATAL_ERROR "Missing runtime file: ${left}")
    endif()
    if(NOT EXISTS "${right}")
        message(FATAL_ERROR "Missing runtime file: ${right}")
    endif()
    file(SHA256 "${left}" _left_hash)
    file(SHA256 "${right}" _right_hash)
    if(NOT _left_hash STREQUAL _right_hash)
        message(FATAL_ERROR "Expected ${right} to be a copy of ${left}")
    endif()
endfunction()

function(erts_check_pe executable)
    file(READ "${executable}" _header LIMIT 4096 HEX)
    string(LENGTH "${_header}" _header_length)
    if(_header_length LESS 128 OR NOT _header MATCHES "^4d5a")
        message(FATAL_ERROR "Expected a PE executable: ${executable}")
    endif()
    string(SUBSTRING "${_header}" 120 8 _lfanew_le)
    string(SUBSTRING "${_lfanew_le}" 0 2 _b0)
    string(SUBSTRING "${_lfanew_le}" 2 2 _b1)
    string(SUBSTRING "${_lfanew_le}" 4 2 _b2)
    string(SUBSTRING "${_lfanew_le}" 6 2 _b3)
    math(EXPR _lfanew "0x${_b3}${_b2}${_b1}${_b0}")
    math(EXPR _sig_index "${_lfanew} * 2")
    math(EXPR _need "${_sig_index} + 12")
    if(_header_length LESS _need)
        message(FATAL_ERROR "PE header is truncated: ${executable}")
    endif()
    string(SUBSTRING "${_header}" ${_sig_index} 8 _sig)
    if(NOT _sig STREQUAL "50450000")
        message(FATAL_ERROR "Missing PE signature in ${executable}: ${_sig}")
    endif()
    math(EXPR _machine_index "(${_lfanew} + 4) * 2")
    string(SUBSTRING "${_header}" ${_machine_index} 4 _machine)
    if(NOT EXPECTED_ARCH STREQUAL "x86" OR NOT _machine STREQUAL "6486")
        message(FATAL_ERROR "Expected AMD64 PE machine 6486, got ${_machine}: ${executable}")
    endif()
endfunction()

function(erts_check_windows_imports label executable)
    find_program(_dumpbin dumpbin)
    if(NOT _dumpbin)
        message(STATUS "dumpbin not found; skipped import check for ${executable}")
        return()
    endif()
    erts_checked_command("${label}-imports" "${_dumpbin}" /dependents "${executable}")
    string(REPLACE "\n" ";" _import_lines "${ERTS_COMMAND_OUTPUT}")
    foreach(_line IN LISTS _import_lines)
        if(NOT _line MATCHES "^[ \t]+([A-Za-z0-9_.+-]+\\.[Dd][Ll][Ll])[ \t]*$")
            continue()
        endif()
        set(_import "${CMAKE_MATCH_1}")
        string(TOLOWER "${_import}" _import)
        if(_import MATCHES "^(kernel32|ws2_32|mswsock|iphlpapi|advapi32|shell32|user32|ntdll)\\.dll$"
                OR _import MATCHES "^(vcruntime140|vcruntime140d|vcruntime140_1|vcruntime140_1d)\\.dll$"
                OR _import MATCHES "^(msvcp140|msvcp140d|msvcp140_1|msvcp140_1d|msvcp140_2|msvcp140_2d)\\.dll$"
                OR _import MATCHES "^(ucrtbase|ucrtbased|concrt140|concrt140d)\\.dll$"
                OR _import MATCHES "^(api-ms-|ext-ms-)")
            continue()
        endif()
        message(FATAL_ERROR "Non-system import in ${executable}: ${_import}")
    endforeach()
endfunction()

function(erts_check_runtime runtime_bin label)
    if(TARGET_SYSTEM STREQUAL "Windows")
        set(_programs beam.exe)
    else()
        set(_programs beam erl_child_setup)
    endif()
    foreach(_program IN LISTS _programs)
        set(_executable "${runtime_bin}/${_program}")
        if(NOT EXISTS "${_executable}")
            message(FATAL_ERROR "Missing ${label} executable: ${_executable}")
        endif()
        if(TARGET_SYSTEM STREQUAL "Darwin")
            erts_checked_command("${label}-${_program}-architecture"
                /usr/bin/lipo -archs "${_executable}")
            string(STRIP "${ERTS_COMMAND_OUTPUT}" _architecture)
            if(NOT _architecture STREQUAL "arm64")
                message(FATAL_ERROR "Expected arm64 ${_executable}, got ${_architecture}")
            endif()
            erts_checked_command("${label}-${_program}-dependencies"
                /usr/bin/otool -L "${_executable}")
            string(REPLACE "\n" ";" _dependency_lines "${ERTS_COMMAND_OUTPUT}")
            foreach(_line IN LISTS _dependency_lines)
                if(_line MATCHES "^[ \t]+([^ \t]+) \\(")
                    set(_dependency_path "${CMAKE_MATCH_1}")
                    if(NOT _dependency_path MATCHES "^(/usr/lib/|/System/Library/)")
                        message(FATAL_ERROR "Non-system dependency in ${_executable}: ${_line}")
                    endif()
                endif()
            endforeach()
            erts_checked_command("${label}-${_program}-load-commands"
                /usr/bin/otool -l "${_executable}")
            string(REPLACE "." "\\." _deployment_pattern "${DEPLOYMENT_TARGET}")
            if(DEPLOYMENT_TARGET AND NOT ERTS_COMMAND_OUTPUT MATCHES
                    "minos[ \t]+${_deployment_pattern}([ \t\r\n]|$)")
                message(FATAL_ERROR "Missing deployment target ${DEPLOYMENT_TARGET}: ${_executable}")
            endif()
            string(REGEX MATCHALL
                "cmd LC_RPATH[^\n]*\n[^\n]*\n[ \t]*path [^\n]+"
                _rpath_commands "${ERTS_COMMAND_OUTPUT}")
            foreach(_rpath_command IN LISTS _rpath_commands)
                if(NOT _rpath_command MATCHES "path ([^\n]+) \\(offset [0-9]+\\)")
                    message(FATAL_ERROR "Cannot inspect LC_RPATH in ${_executable}: ${_rpath_command}")
                endif()
                set(_rpath "${CMAKE_MATCH_1}")
                if(IS_ABSOLUTE "${_rpath}")
                    if(NOT _rpath MATCHES "^(/usr/lib/|/System/Library/)")
                        message(FATAL_ERROR "Non-system LC_RPATH in ${_executable}: ${_rpath}")
                    endif()
                elseif(NOT _rpath MATCHES "^@(loader_path|executable_path)(/|$)")
                    message(FATAL_ERROR "Non-relocatable LC_RPATH in ${_executable}: ${_rpath}")
                endif()
            endforeach()
        elseif(TARGET_SYSTEM STREQUAL "Linux")
            file(READ "${_executable}" _elf LIMIT 20 HEX)
            if(NOT _elf MATCHES "^7f454c460201")
                message(FATAL_ERROR "Expected a little-endian 64-bit ELF: ${_executable}")
            endif()
            string(SUBSTRING "${_elf}" 36 4 _machine)
            if((EXPECTED_ARCH STREQUAL "x86" AND NOT _machine STREQUAL "3e00")
                    OR (EXPECTED_ARCH STREQUAL "arm" AND NOT _machine STREQUAL "b700"))
                message(FATAL_ERROR "Unexpected ELF architecture ${_machine}: ${_executable}")
            endif()
            find_program(_ldd ldd REQUIRED)
            erts_checked_command("${label}-${_program}-dependencies" "${_ldd}" "${_executable}")
            if(ERTS_COMMAND_OUTPUT MATCHES "not found")
                message(FATAL_ERROR "Unresolved dependency in ${_executable}")
            endif()
            string(REPLACE "\n" ";" _dependency_lines "${ERTS_COMMAND_OUTPUT}")
            foreach(_line IN LISTS _dependency_lines)
                set(_dependency_path "")
                string(STRIP "${_line}" _dependency_line)
                if(_dependency_line MATCHES "=>[ \t]+(/[^ \t]+)")
                    set(_dependency_path "${CMAKE_MATCH_1}")
                elseif(_dependency_line MATCHES "^(/[^ \t]+)")
                    set(_dependency_path "${CMAKE_MATCH_1}")
                endif()
                if(_dependency_path)
                    if(NOT _dependency_path MATCHES "^/(usr/)?lib(64)?/")
                        message(FATAL_ERROR "Non-system dependency in ${_executable}: ${_line}")
                    endif()
                endif()
            endforeach()
        elseif(TARGET_SYSTEM STREQUAL "Windows")
            erts_check_pe("${_executable}")
            erts_check_windows_imports("${label}-${_program}" "${_executable}")
        else()
            message(FATAL_ERROR "Unsupported runtime test system: ${TARGET_SYSTEM}")
        endif()
    endforeach()

    if(TARGET_SYSTEM STREQUAL "Windows")
        foreach(_alias beam.smp.exe "beam.${EXPECTED_FLAVOR}.exe")
            erts_require_same_file("${runtime_bin}/beam.exe" "${runtime_bin}/${_alias}")
            erts_checked_command("${label}-${_alias}-version" "${runtime_bin}/${_alias}" -V)
        endforeach()
        if(EXPECTED_BUILD_TYPE STREQUAL "debug")
            foreach(_alias beam.smp.pdb "beam.${EXPECTED_FLAVOR}.pdb")
                erts_require_same_file("${runtime_bin}/beam.pdb" "${runtime_bin}/${_alias}")
            endforeach()
        endif()
    else()
        if(EXPECTED_BUILD_TYPE STREQUAL "debug")
            set(_helper_alias "${runtime_bin}/erl_child_setup.debug")
            if(NOT IS_SYMLINK "${_helper_alias}")
                message(FATAL_ERROR "Missing Debug helper alias: ${_helper_alias}")
            endif()
            file(READ_SYMLINK "${_helper_alias}" _helper_target)
            if(NOT _helper_target STREQUAL "erl_child_setup")
                message(FATAL_ERROR "Debug helper alias must point to erl_child_setup, got ${_helper_target}")
            endif()
        endif()

        foreach(_alias beam.smp "beam.${EXPECTED_FLAVOR}")
            if(NOT IS_SYMLINK "${runtime_bin}/${_alias}")
                message(FATAL_ERROR "Missing relative alias: ${runtime_bin}/${_alias}")
            endif()
            file(READ_SYMLINK "${runtime_bin}/${_alias}" _alias_target)
            if(NOT _alias_target STREQUAL "beam")
                message(FATAL_ERROR "${_alias} must point to beam, got ${_alias_target}")
            endif()
            erts_checked_command("${label}-${_alias}-version" "${runtime_bin}/${_alias}" -V)
        endforeach()
    endif()
endfunction()

function(erts_check_install_manifest prefix)
    file(STRINGS "${BINARY_DIR}/install_manifest.txt" _manifest)
    if(TARGET_SYSTEM STREQUAL "Windows")
        set(_expected_manifest
            "${prefix}/${INSTALL_BINDIR}/beam.exe"
            "${prefix}/${INSTALL_BINDIR}/beam.smp.exe"
            "${prefix}/${INSTALL_BINDIR}/beam.${EXPECTED_FLAVOR}.exe")
        if(EXPECTED_BUILD_TYPE STREQUAL "debug")
            list(APPEND _expected_manifest
                "${prefix}/${INSTALL_BINDIR}/beam.pdb"
                "${prefix}/${INSTALL_BINDIR}/beam.smp.pdb"
                "${prefix}/${INSTALL_BINDIR}/beam.${EXPECTED_FLAVOR}.pdb")
        endif()
    else()
        set(_expected_manifest
            "${prefix}/${INSTALL_BINDIR}/beam"
            "${prefix}/${INSTALL_BINDIR}/erl_child_setup"
            "${prefix}/${INSTALL_BINDIR}/beam.smp"
            "${prefix}/${INSTALL_BINDIR}/beam.${EXPECTED_FLAVOR}")
        if(EXPECTED_BUILD_TYPE STREQUAL "debug")
            list(APPEND _expected_manifest "${prefix}/${INSTALL_BINDIR}/erl_child_setup.debug")
        endif()
    endif()
    set(_normalized_manifest "")
    foreach(_entry IN LISTS _manifest)
        erts_normalize_installed_path("${_entry}" _entry)
        list(APPEND _normalized_manifest "${_entry}")
    endforeach()
    set(_normalized_expected "")
    foreach(_entry IN LISTS _expected_manifest)
        erts_normalize_installed_path("${_entry}" _entry)
        list(APPEND _normalized_expected "${_entry}")
    endforeach()
    set(_manifest "${_normalized_manifest}")
    set(_expected_manifest "${_normalized_expected}")
    list(SORT _manifest)
    list(SORT _expected_manifest)
    if(NOT "${_manifest}" STREQUAL "${_expected_manifest}")
        message(FATAL_ERROR
            "Unexpected install manifest; expected logical prefix paths without DESTDIR.\n"
            "Expected: ${_expected_manifest}\nActual: ${_manifest}")
    endif()
endfunction()

function(erts_boot runtime_bin label)
    set(_eval [=[
try
    [Source, Scratch, Flavor, Backend, Fixture, BuildType, KernelPoll] = init:get_plain_arguments(),
    _ = code:del_path("."),
    Compiled = compile:file(Source, [binary, return_errors, return_warnings]),
    {ok, beam_runtime_smoke, Binary, []} = Compiled,
    {module, beam_runtime_smoke} = code:load_binary(beam_runtime_smoke, Source, Binary),
    ok = beam_runtime_smoke:run(Scratch, list_to_atom(Flavor), list_to_atom(Backend),
                                Fixture, list_to_atom(BuildType), list_to_atom(KernelPoll)),
    erlang:display({runtime_smoke_ok, list_to_atom(Flavor), list_to_atom(Backend),
                    list_to_atom(BuildType)}),
    erlang:halt(0)
catch Class:Reason:Stack ->
    erlang:display({runtime_smoke_failed, Class, Reason, Stack}),
    erlang:halt(1)
end.
]=])
    # cmd.exe ends a quoted argument at a newline. Keep the program one line.
    string(REPLACE "\r" "" _eval "${_eval}")
    string(REPLACE "\n" " " _eval "${_eval}")
    file(MAKE_DIRECTORY "${TEST_ROOT}/work/${label}")
    set(_boot_args
        -S 2:2 -SDcpu 1 -SDio 1 -K "${EXPECTED_KERNEL_POLL}" --
        -root "${_fixture}" -bindir "${runtime_bin}" -progname beam
        -home "${TEST_ROOT}/home" -boot "${_fixture}/bin/start_clean"
        -path "${_fixture}/lib/kernel/ebin" "${_fixture}/lib/stdlib/ebin"
            "${_fixture}/lib/compiler/ebin"
        -noshell -noinput -eval "${_eval}" -extra
        "${TEST_ROOT}/work/beam_runtime_smoke.erl" "${TEST_ROOT}/work/${label}"
        "${EXPECTED_FLAVOR}" "${EXPECTED_BACKEND}" "${_fixture}" "${EXPECTED_BUILD_TYPE}"
        "${EXPECTED_KERNEL_POLL}")
    if(TARGET_SYSTEM STREQUAL "Windows")
        file(TO_CMAKE_PATH "$ENV{SystemRoot}" _system_root)
        set(_system32 "${_system_root}/System32")
        if(NOT _system_root MATCHES "^([A-Za-z]):")
            message(FATAL_ERROR "SystemRoot has no drive letter: ${_system_root}")
        endif()
        set(_system_drive "${CMAKE_MATCH_1}:")
        set(_line "\"${runtime_bin}/beam.exe\"")
        foreach(_arg IN LISTS _boot_args)
            string(REPLACE "\"" "\\\"" _quoted "${_arg}")
            string(APPEND _line " \"${_quoted}\"")
        endforeach()
        set(_script "${TEST_ROOT}/work/${label}-boot.cmd")
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
set \"HOME=${TEST_ROOT}/home\"
set \"TMP=${TEST_ROOT}/work/${label}\"
set \"TEMP=${TEST_ROOT}/work/${label}\"
set \"BINDIR=${runtime_bin}\"
set \"ERL_INETRC=${TEST_ROOT}/inetrc\"
set \"ERL_CRASH_DUMP=${TEST_ROOT}/${label}-erl_crash.dump\"
${_line}
set \"RC=%ERRORLEVEL%\"
exit /b %RC%
")
        erts_checked_command("${label}-boot" "${_script}")
    else()
        erts_checked_command("${label}-boot"
            /usr/bin/env -i
            "PATH=/usr/bin:/bin:/usr/sbin:/sbin"
            "HOME=${TEST_ROOT}/home"
            "TMPDIR=${TEST_ROOT}/work/${label}"
            "BINDIR=${runtime_bin}"
            "ERL_INETRC=${TEST_ROOT}/inetrc"
            "ERL_CRASH_DUMP=${TEST_ROOT}/${label}-erl_crash.dump"
            "${runtime_bin}/beam" ${_boot_args})
    endif()
    # Erlang quotes atoms that are not plain lowercase identifiers.
    if(NOT ERTS_COMMAND_OUTPUT MATCHES "runtime_smoke_ok,${EXPECTED_FLAVOR},'?${EXPECTED_BACKEND}'?,${EXPECTED_BUILD_TYPE}")
        message(FATAL_ERROR "Missing success marker for ${label}: ${ERTS_COMMAND_OUTPUT}")
    endif()
    if(TARGET_SYSTEM STREQUAL "Windows")
        # CMake passes native Unicode arguments directly. A .cmd file would
        # first reinterpret its UTF-8 source using the console code page.
        set(_unicode_eval "[[955],[65,32,955],[26085,26412,35486],[128512]]=init:get_plain_arguments(),erlang:display(windows_unicode_arguments_ok),erlang:halt(0).")
        erts_checked_command("${label}-unicode-arguments"
            "${CMAKE_COMMAND}" -E env --unset=ERL_FLAGS --unset=ERL_AFLAGS
            --unset=ERL_ZFLAGS --unset=ERL_LIBS
            "PATH=${_system32}" "HOME=${TEST_ROOT}/home"
            "TMP=${TEST_ROOT}/work/${label}" "TEMP=${TEST_ROOT}/work/${label}"
            "BINDIR=${runtime_bin}" "ERL_INETRC=${TEST_ROOT}/inetrc"
            "ERL_CRASH_DUMP=${TEST_ROOT}/${label}-unicode-erl_crash.dump"
            "${runtime_bin}/beam.exe" -S 2:2 -SDcpu 1 -SDio 1
            -K "${EXPECTED_KERNEL_POLL}" -- -root "${_fixture}"
            -bindir "${runtime_bin}" -progname beam -home "${TEST_ROOT}/home"
            -boot "${_fixture}/bin/start_clean"
            -path "${_fixture}/lib/kernel/ebin" "${_fixture}/lib/stdlib/ebin"
                "${_fixture}/lib/compiler/ebin"
            -noshell -noinput -eval "${_unicode_eval}" -extra "λ" "A λ" "日本語" "😀")
        if(NOT ERTS_COMMAND_OUTPUT MATCHES "windows_unicode_arguments_ok")
            message(FATAL_ERROR "Missing Unicode argument success marker for ${label}: ${ERTS_COMMAND_OUTPUT}")
        endif()
    endif()
endfunction()

if(MODE STREQUAL "build")
    erts_check_runtime("${RUNTIME_BIN}" build)
    erts_boot("${RUNTIME_BIN}" build)
else()
    set(_prefix "${TEST_ROOT}/install prefix with spaces")
    set(_destdir "${TEST_ROOT}/destdir with spaces")
    set(_relocated "${TEST_ROOT}/relocated prefix with spaces")
    foreach(_install_pass first repeat)
        erts_checked_command("install-${_install_pass}"
            "${CMAKE_COMMAND}" -E env --unset=DESTDIR
            "${CMAKE_COMMAND}" --install "${BINARY_DIR}" --prefix "${_prefix}")
        erts_check_install_manifest("${_prefix}")
    endforeach()
    erts_check_runtime("${_prefix}/${INSTALL_BINDIR}" installed)
    erts_boot("${_prefix}/${INSTALL_BINDIR}" installed)

    erts_checked_command(install-destdir
        "${CMAKE_COMMAND}" -E env "DESTDIR=${_destdir}"
        "${CMAKE_COMMAND}" --install "${BINARY_DIR}" --prefix "${_prefix}")
    erts_check_install_manifest("${_prefix}")
    erts_staged_path("${_destdir}" "${_prefix}" _staged)
    erts_check_runtime("${_staged}/${INSTALL_BINDIR}" staged)
    erts_boot("${_staged}/${INSTALL_BINDIR}" staged)
    file(RENAME "${_staged}" "${_relocated}")
    file(REMOVE_RECURSE "${_prefix}" "${_destdir}")
    erts_check_runtime("${_relocated}/${INSTALL_BINDIR}" relocated)
    erts_boot("${_relocated}/${INSTALL_BINDIR}" relocated)
endif()

message(STATUS "${MODE} runtime integration passed (${EXPECTED_FLAVOR}, ${EXPECTED_BUILD_TYPE}, ${EXPECTED_BACKEND})")
file(REMOVE_RECURSE "${TEST_ROOT}")
