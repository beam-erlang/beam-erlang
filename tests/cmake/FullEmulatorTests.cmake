# Full-corpus support remains private to the selected build and its candidate VM.
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
set(BEAM_FULL_UPSTREAM "${CMAKE_CURRENT_SOURCE_DIR}/emulator/support")
set(BEAM_FULL_ROOT "${CMAKE_CURRENT_BINARY_DIR}/emulator/full")
set(BEAM_FULL_NATIVE_ROOT "${BEAM_FULL_ROOT}/native")
# Additional full-corpus test inputs from upstream Erlang/OTP 550d7b7898706c7822362c42e7b93120c1d1f29a.
# Paths are relative to tests/emulator/support.
set(BEAM_EMULATOR_FULL_UPSTREAM_FILES
    "erts/epmd/src/epmd.c"
    "erts/epmd/src/epmd.h"
    "erts/epmd/src/epmd_cli.c"
    "erts/epmd/src/epmd_int.h"
    "erts/epmd/src/epmd_srv.c"
    "erts/etc/common/erlexec.c"
    "erts/etc/common/etc_common.h"
    "erts/etc/common/heart.c"
    "erts/etc/common/inet_gethost.c"
    "lib/dialyzer/src/erl_bif_types.erl"
    "lib/dialyzer/src/erl_types.erl"
    "lib/erl_interface/config.h.in"
    "lib/erl_interface/include/ei.h"
    "lib/erl_interface/include/ei_connect.h"
    "lib/erl_interface/include/ei_global.h"
    "lib/erl_interface/include/eicode.h"
    "lib/erl_interface/src/connect/ei_connect.c"
    "lib/erl_interface/src/connect/ei_connect_int.h"
    "lib/erl_interface/src/connect/ei_resolve.c"
    "lib/erl_interface/src/connect/ei_resolve.h"
    "lib/erl_interface/src/connect/eirecv.c"
    "lib/erl_interface/src/connect/eirecv.h"
    "lib/erl_interface/src/connect/eisend.h"
    "lib/erl_interface/src/connect/send.c"
    "lib/erl_interface/src/connect/send_exit.c"
    "lib/erl_interface/src/connect/send_reg.c"
    "lib/erl_interface/src/decode/decode_atom.c"
    "lib/erl_interface/src/decode/decode_big.c"
    "lib/erl_interface/src/decode/decode_bignum.c"
    "lib/erl_interface/src/decode/decode_binary.c"
    "lib/erl_interface/src/decode/decode_boolean.c"
    "lib/erl_interface/src/decode/decode_char.c"
    "lib/erl_interface/src/decode/decode_double.c"
    "lib/erl_interface/src/decode/decode_fun.c"
    "lib/erl_interface/src/decode/decode_intlist.c"
    "lib/erl_interface/src/decode/decode_iodata.c"
    "lib/erl_interface/src/decode/decode_list_header.c"
    "lib/erl_interface/src/decode/decode_long.c"
    "lib/erl_interface/src/decode/decode_longlong.c"
    "lib/erl_interface/src/decode/decode_pid.c"
    "lib/erl_interface/src/decode/decode_port.c"
    "lib/erl_interface/src/decode/decode_ref.c"
    "lib/erl_interface/src/decode/decode_skip.c"
    "lib/erl_interface/src/decode/decode_skip.h"
    "lib/erl_interface/src/decode/decode_string.c"
    "lib/erl_interface/src/decode/decode_trace.c"
    "lib/erl_interface/src/decode/decode_tuple_header.c"
    "lib/erl_interface/src/decode/decode_ulong.c"
    "lib/erl_interface/src/decode/decode_ulonglong.c"
    "lib/erl_interface/src/decode/decode_version.c"
    "lib/erl_interface/src/encode/eicode.h"
    "lib/erl_interface/src/encode/encode_atom.c"
    "lib/erl_interface/src/encode/encode_big.c"
    "lib/erl_interface/src/encode/encode_bignum.c"
    "lib/erl_interface/src/encode/encode_binary.c"
    "lib/erl_interface/src/encode/encode_boolean.c"
    "lib/erl_interface/src/encode/encode_char.c"
    "lib/erl_interface/src/encode/encode_double.c"
    "lib/erl_interface/src/encode/encode_fun.c"
    "lib/erl_interface/src/encode/encode_list_header.c"
    "lib/erl_interface/src/encode/encode_long.c"
    "lib/erl_interface/src/encode/encode_longlong.c"
    "lib/erl_interface/src/encode/encode_pid.c"
    "lib/erl_interface/src/encode/encode_port.c"
    "lib/erl_interface/src/encode/encode_ref.c"
    "lib/erl_interface/src/encode/encode_string.c"
    "lib/erl_interface/src/encode/encode_trace.c"
    "lib/erl_interface/src/encode/encode_tuple_header.c"
    "lib/erl_interface/src/encode/encode_ulong.c"
    "lib/erl_interface/src/encode/encode_ulonglong.c"
    "lib/erl_interface/src/encode/encode_version.c"
    "lib/erl_interface/src/epmd/ei_epmd.h"
    "lib/erl_interface/src/epmd/epmd_port.c"
    "lib/erl_interface/src/epmd/epmd_publish.c"
    "lib/erl_interface/src/epmd/epmd_unpublish.c"
    "lib/erl_interface/src/global/global_names.c"
    "lib/erl_interface/src/global/global_register.c"
    "lib/erl_interface/src/global/global_unregister.c"
    "lib/erl_interface/src/global/global_whereis.c"
    "lib/erl_interface/src/misc/ei_cmp_nc.c"
    "lib/erl_interface/src/misc/ei_compat.c"
    "lib/erl_interface/src/misc/ei_decode_term.c"
    "lib/erl_interface/src/misc/ei_decode_term.h"
    "lib/erl_interface/src/misc/ei_format.c"
    "lib/erl_interface/src/misc/ei_format.h"
    "lib/erl_interface/src/misc/ei_init.c"
    "lib/erl_interface/src/misc/ei_internal.h"
    "lib/erl_interface/src/misc/ei_locking.c"
    "lib/erl_interface/src/misc/ei_locking.h"
    "lib/erl_interface/src/misc/ei_malloc.c"
    "lib/erl_interface/src/misc/ei_malloc.h"
    "lib/erl_interface/src/misc/ei_portio.c"
    "lib/erl_interface/src/misc/ei_portio.h"
    "lib/erl_interface/src/misc/ei_printterm.c"
    "lib/erl_interface/src/misc/ei_printterm.h"
    "lib/erl_interface/src/misc/ei_pthreads.c"
    "lib/erl_interface/src/misc/ei_trace.c"
    "lib/erl_interface/src/misc/ei_trace.h"
    "lib/erl_interface/src/misc/ei_x_encode.c"
    "lib/erl_interface/src/misc/ei_x_encode.h"
    "lib/erl_interface/src/misc/eidef.h"
    "lib/erl_interface/src/misc/eiext.h"
    "lib/erl_interface/src/misc/get_type.c"
    "lib/erl_interface/src/misc/putget.h"
    "lib/erl_interface/src/misc/show_msg.c"
    "lib/erl_interface/src/misc/show_msg.h"
    "lib/eunit/include/eunit.hrl"
    "lib/eunit/src/eunit_autoexport.erl"
    "lib/eunit/src/eunit_internal.hrl"
    "lib/eunit/src/eunit_striptests.erl"
    "lib/kernel/examples/gen_tcp_dist/src/gen_tcp_dist.erl"
    "lib/kernel/src/code.erl"
    "lib/kernel/src/erl_ddll.erl"
    "lib/kernel/src/erl_debugger.erl"
    "lib/kernel/src/error_logger.erl"
    "lib/kernel/src/erts_debug.erl"
    "lib/kernel/src/file.erl"
    "lib/kernel/src/file_int.hrl"
    "lib/kernel/src/inet_boot.hrl"
    "lib/kernel/src/inet_int.hrl"
    "lib/kernel/src/net_kernel.erl"
    "lib/kernel/src/os.erl"
    "lib/os_mon/c_src/cpu_sup.c"
    "lib/os_mon/c_src/memsup.c"
    "lib/os_mon/c_src/memsup.h"
    "lib/os_mon/include/memsup.hrl"
    "lib/os_mon/src/cpu_sup.erl"
    "lib/os_mon/src/disksup.erl"
    "lib/os_mon/src/memsup.erl"
    "lib/os_mon/src/nteventlog.erl"
    "lib/os_mon/src/os_mon.app.src"
    "lib/os_mon/src/os_mon.appup.src"
    "lib/os_mon/src/os_mon.erl"
    "lib/os_mon/src/os_mon_mib.erl"
    "lib/os_mon/src/os_mon_sysinfo.erl"
    "lib/os_mon/src/os_sup.erl"
    "lib/os_mon/vsn.mk"
    "lib/runtime_tools/src/dbg.erl"
    "lib/runtime_tools/src/instrument.erl"
    "lib/sasl/src/alarm_handler.erl"
    "lib/sasl/src/erlsrv.erl"
    "lib/sasl/src/format_lib_supp.erl"
    "lib/sasl/src/misc_supp.erl"
    "lib/sasl/src/rb.erl"
    "lib/sasl/src/rb_format_supp.erl"
    "lib/sasl/src/release_handler.erl"
    "lib/sasl/src/release_handler_1.erl"
    "lib/sasl/src/sasl.app.src"
    "lib/sasl/src/sasl.appup.src"
    "lib/sasl/src/sasl.erl"
    "lib/sasl/src/sasl_report.erl"
    "lib/sasl/src/sasl_report_file_h.erl"
    "lib/sasl/src/sasl_report_tty_h.erl"
    "lib/sasl/src/systools.erl"
    "lib/sasl/src/systools.hrl"
    "lib/sasl/src/systools_lib.erl"
    "lib/sasl/src/systools_make.erl"
    "lib/sasl/src/systools_rc.erl"
    "lib/sasl/src/systools_relup.erl"
    "lib/sasl/vsn.mk"
    "lib/stdlib/src/binary.erl"
    "lib/stdlib/src/ets.erl"
    "lib/stdlib/src/io.erl"
    "lib/stdlib/src/lists.erl"
    "lib/stdlib/src/maps.erl"
    "lib/stdlib/src/math.erl"
    "lib/stdlib/src/re.erl"
    "lib/stdlib/src/records.erl"
    "lib/stdlib/src/string.erl"
    "lib/stdlib/src/swar_ascii.hrl"
    "lib/stdlib/src/unicode.erl"
    "lib/stdlib/test/error_info_lib.erl"
)
if(WIN32)
    list(APPEND BEAM_EMULATOR_FULL_UPSTREAM_FILES
        "erts/etc/win32/init_file.c"
        "erts/etc/win32/init_file.h"
        "erts/etc/win32/port_entry.c"
        "lib/os_mon/c_src/win32sysinfo.c"
        "lib/os_mon/c_src/nteventlog/elog_main.c"
        "lib/os_mon/c_src/nteventlog/elog_util.c"
        "lib/os_mon/c_src/nteventlog/elog_util.h"
        "lib/os_mon/c_src/nteventlog/elog_registry.c"
        "lib/os_mon/c_src/nteventlog/elog_registry.h"
        "lib/os_mon/c_src/nteventlog/elog_pipe_stdin.c"
        "lib/os_mon/c_src/nteventlog/elog_pipe_stdin.h"
        "lib/os_mon/c_src/nteventlog/elog_format.c"
        "lib/os_mon/c_src/nteventlog/elog_format.h"
        "lib/os_mon/c_src/nteventlog/elog_global.h")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/FullEmulatorNative.cmake")
set(_beam_full_native_inputs)
foreach(_artifact IN LISTS BEAM_FULL_NATIVE_ARTIFACTS)
    list(APPEND _beam_full_native_inputs "${BEAM_FULL_NATIVE_ROOT}/${_artifact}")
endforeach()

if(WIN32)
    add_executable(beam_emulator_process
        "${CMAKE_CURRENT_SOURCE_DIR}/emulator/beam_emulator_windows_process.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/emulator/beam_emulator_windows_process.h")
    target_link_libraries(beam_emulator_process PRIVATE advapi32 iphlpapi ws2_32)
else()
    add_executable(beam_emulator_process "${CMAKE_CURRENT_SOURCE_DIR}/emulator/emulator_process.c")
endif()
set_target_properties(beam_emulator_process PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${BEAM_FULL_NATIVE_ROOT}/tools")
if(APPLE)
    target_link_libraries(beam_emulator_process PRIVATE proc)
endif()

set(_beam_full_inputs)
foreach(_file IN LISTS BEAM_EMULATOR_UPSTREAM_FILES BEAM_EMULATOR_FULL_UPSTREAM_FILES)
    list(APPEND _beam_full_inputs "${BEAM_FULL_UPSTREAM}/${_file}")
endforeach()
if(WIN32)
    list(APPEND _beam_full_inputs
        "${CMAKE_CURRENT_SOURCE_DIR}/emulator/beam_emulator_windows_process.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/emulator/beam_emulator_windows_process.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/emulator/beam_emulator_windows_erlexec.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/emulator/beam_emulator_windows_command.c")
endif()
# Retained corpus/data and preload inspection inputs follow their source trees.
file(GLOB_RECURSE _beam_full_corpus_inputs CONFIGURE_DEPENDS
    LIST_DIRECTORIES FALSE "${CMAKE_SOURCE_DIR}/erts/emulator/test/*"
    "${CMAKE_SOURCE_DIR}/erts/preloaded/src/*"
    "${CMAKE_SOURCE_DIR}/erts/preloaded/ebin/*")
list(APPEND _beam_full_inputs ${_beam_full_corpus_inputs})
set(_beam_full_support_files "${BEAM_FULL_ROOT}/support-files.txt")
set(_beam_full_support_paths ${BEAM_EMULATOR_UPSTREAM_FILES} ${BEAM_EMULATOR_FULL_UPSTREAM_FILES})
list(JOIN _beam_full_support_paths "\n" _beam_full_support_content)
file(GENERATE OUTPUT "${_beam_full_support_files}" CONTENT "${_beam_full_support_content}\n")
file(GLOB_RECURSE _beam_full_bootstrap CONFIGURE_DEPENDS
    LIST_DIRECTORIES FALSE "${CMAKE_SOURCE_DIR}/bootstrap/*")
# Removed glob members must invalidate preparation after CMake regenerates.
set(_beam_full_preparation_inputs ${_beam_full_inputs} ${_beam_full_bootstrap})
string(SHA256 _beam_full_preparation_inputs_hash "${_beam_full_preparation_inputs}")
set(_beam_full_preparation_membership "${BEAM_FULL_ROOT}/preparation-inputs.sha256")
file(GENERATE OUTPUT "${_beam_full_preparation_membership}" CONTENT "${_beam_full_preparation_inputs_hash}\n")
set(_beam_full_driver "${CMAKE_CURRENT_SOURCE_DIR}/emulator/full_emulator.py")
set(_beam_full_args
    --source-root "${CMAKE_SOURCE_DIR}" --binary-root "${CMAKE_BINARY_DIR}"
    --full-root "${BEAM_FULL_ROOT}" --runtime-bin "$<TARGET_FILE_DIR:beam>"
    --flavor "${ERTS_BUILD_FLAVOR}" --build-type "${_erts_build_type}"
    --native-root "${BEAM_FULL_NATIVE_ROOT}"
    --native-manifest "${BEAM_FULL_NATIVE_MANIFEST}"
    --support-files "${_beam_full_support_files}"
    --process-tool "$<TARGET_FILE:beam_emulator_process>")

# Keep the focused command tied to this build without exposing its internal
# path arguments or invoking a shell. Resolve target paths at generation time.
function(_beam_full_python_string out value)
    string(REPLACE "\\" "\\\\" _value "${value}")
    string(REPLACE "\"" "\\\"" _value "${_value}")
    string(REPLACE "\n" "\\n" _value "${_value}")
    string(REPLACE "\r" "\\r" _value "${_value}")
    string(REPLACE "\t" "\\t" _value "${_value}")
    set(${out} "\"${_value}\"" PARENT_SCOPE)
endfunction()
_beam_full_python_string(BEAM_FULL_LAUNCHER_PYTHON "${Python3_EXECUTABLE}")
_beam_full_python_string(BEAM_FULL_LAUNCHER_DRIVER "${_beam_full_driver}")
set(BEAM_FULL_LAUNCHER_ARGUMENTS)
foreach(_argument IN LISTS _beam_full_args)
    _beam_full_python_string(_literal "${_argument}")
    string(APPEND BEAM_FULL_LAUNCHER_ARGUMENTS "    ${_literal},\n")
endforeach()
set(_beam_full_launcher_template "${CMAKE_CURRENT_LIST_DIR}/RunEmulator.py.in")
configure_file("${_beam_full_launcher_template}"
    "${BEAM_FULL_ROOT}/run.py.in" @ONLY)
file(GENERATE OUTPUT "${BEAM_FULL_ROOT}/run.py"
    INPUT "${BEAM_FULL_ROOT}/run.py.in")

set(_beam_full_prepared "${BEAM_FULL_ROOT}/prepared.stamp")
set(_beam_full_runtime_targets beam)
if(TARGET erl_child_setup)
    list(APPEND _beam_full_runtime_targets erl_child_setup)
endif()
add_custom_command(OUTPUT "${_beam_full_prepared}"
    BYPRODUCTS "${BEAM_FULL_ROOT}/runtime/prepared-receipt.json"
        "${BEAM_FULL_ROOT}/suites/CTestTestfile.cmake"
    COMMAND "${Python3_EXECUTABLE}" -B "${_beam_full_driver}" prepare ${_beam_full_args}
    COMMAND "${CMAKE_COMMAND}" -E touch "${_beam_full_prepared}"
    DEPENDS ${_beam_full_runtime_targets} beam_emulator_process beam_emulator_full_native
        ${_beam_full_native_inputs} "${BEAM_FULL_NATIVE_MANIFEST}"
        ${_beam_full_inputs} ${_beam_full_bootstrap}
        "${CMAKE_SOURCE_DIR}/UPSTREAM_COMMIT" "${_beam_full_driver}"
        "${_beam_full_support_files}" "${_beam_full_preparation_membership}"
        "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
        "${CMAKE_CURRENT_LIST_DIR}/FullEmulatorTests.cmake"
        "${_beam_full_launcher_template}"
        "${CMAKE_CURRENT_SOURCE_DIR}/emulator/beam_emulator_harness.erl"
        "${CMAKE_CURRENT_SOURCE_DIR}/emulator/beam_emulator_full_prepare.erl"
        "${CMAKE_CURRENT_SOURCE_DIR}/emulator/beam_emulator_full_run.erl"
        "${CMAKE_CURRENT_SOURCE_DIR}/emulator/beam_emulator_focus.erl"
        "${CMAKE_CURRENT_SOURCE_DIR}/emulator/beam_emulator_events.erl"
        "${CMAKE_CURRENT_SOURCE_DIR}/emulator/emulator_skip_policy.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/emulator/emulator_scope.json"
        "${BEAM_FULL_COMPILER_VARIABLES}"
    COMMENT "Preparing the complete private emulator corpus"
    VERBATIM)
add_custom_target(beam_emulator_full_test_support ALL DEPENDS "${_beam_full_prepared}")
add_test(NAME emulator.full
    COMMAND "${Python3_EXECUTABLE}" -B "${_beam_full_driver}" run ${_beam_full_args})
set_tests_properties(emulator.full PROPERTIES
    LABELS "emulator-full;${ERTS_BUILD_FLAVOR}" RUN_SERIAL TRUE TIMEOUT 21660)
add_custom_target(check-emulator-full
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}"
        -L "^emulator-full$" --verbose --output-on-failure --no-tests=error -j1
    DEPENDS beam_emulator_full_test_support USES_TERMINAL VERBATIM)
