# Native fixtures for the retained emulator corpus. Test-only outputs:
# never link beam_emulator or another runtime into a driver, NIF, or helper.
if(NOT APPLE AND NOT (WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8))
    message(FATAL_ERROR "The full emulator native fixtures require native macOS or Windows x64 MSVC")
endif()
if(NOT BEAM_FULL_NATIVE_ROOT OR NOT BEAM_FULL_UPSTREAM)
    message(FATAL_ERROR "Full emulator native root and pinned upstream root are required")
endif()

set(_beam_full_native_targets)
set(BEAM_FULL_NATIVE_ARTIFACTS)
set(_beam_full_native_records)
set(_beam_full_corpus "${CMAKE_SOURCE_DIR}/erts/emulator/test")
get_target_property(_beam_full_candidate_includes beam_emulator INCLUDE_DIRECTORIES)
set(_beam_full_candidate_includes
    "${CMAKE_BINARY_DIR}/erts"
    "${CMAKE_BINARY_DIR}/erts/include"
    "${CMAKE_BINARY_DIR}/erts/include/internal"
    "${CMAKE_SOURCE_DIR}/erts/include"
    "${CMAKE_SOURCE_DIR}/erts/include/internal"
    "${CMAKE_SOURCE_DIR}/erts/emulator/beam"
    ${_beam_full_candidate_includes})

# The original CT configure supplies these DEFS directly to fixture sources;
# several intentionally do not include config.h before using their guards.
include(CheckIncludeFile)
include(CheckSymbolExists)
set(_beam_full_fixture_definitions)
foreach(_probe "unistd.h|HAVE_UNISTD_H" "poll.h|HAVE_POLL_H")
    string(REPLACE "|" ";" _parts "${_probe}")
    list(GET _parts 0 _header)
    list(GET _parts 1 _macro)
    check_include_file("${_header}" "BEAM_FULL_FIXTURE_${_macro}")
    if(BEAM_FULL_FIXTURE_${_macro})
        list(APPEND _beam_full_fixture_definitions "${_macro}=1")
    endif()
endforeach()
foreach(_probe "vsnprintf|stdio.h|HAVE_VSNPRINTF" "strerror|string.h|HAVE_STRERROR"
        "random_r|stdlib.h|HAVE_RANDOM_R")
    string(REPLACE "|" ";" _parts "${_probe}")
    list(GET _parts 0 _symbol)
    list(GET _parts 1 _header)
    list(GET _parts 2 _macro)
    check_symbol_exists("${_symbol}" "${_header}" "BEAM_FULL_FIXTURE_${_macro}")
    if(BEAM_FULL_FIXTURE_${_macro})
        list(APPEND _beam_full_fixture_definitions "${_macro}=1")
    endif()
endforeach()

function(_beam_full_json_string output value)
    string(REPLACE "\\" "\\\\" value "${value}")
    string(REPLACE "\"" "\\\"" value "${value}")
    string(REPLACE "\n" "\\n" value "${value}")
    string(REPLACE "\r" "\\r" value "${value}")
    string(REPLACE "\t" "\\t" value "${value}")
    # Records are accumulated as a CMake list before being joined as JSON.
    string(REPLACE ";" "\\u003b" value "${value}")
    set(${output} "\"${value}\"" PARENT_SCOPE)
endfunction()

function(_beam_full_json_array output)
    set(_items)
    foreach(_value IN LISTS ARGN)
        _beam_full_json_string(_quoted "${_value}")
        list(APPEND _items "${_quoted}")
    endforeach()
    list(JOIN _items "," _joined)
    set(${output} "[${_joined}]" PARENT_SCOPE)
endfunction()

function(_beam_full_describe target relative kind)
    get_target_property(_sources ${target} SOURCES)
    get_target_property(_origins ${target} BEAM_FULL_SOURCE_ORIGINS)
    if(_origins AND NOT _origins MATCHES "-NOTFOUND$")
        list(APPEND _sources ${_origins})
    endif()
    set(_relative_sources)
    set(_source_definition_records)
    foreach(_source IN LISTS _sources)
        get_source_file_property(_source_definitions "${_source}" COMPILE_DEFINITIONS)
        if(IS_ABSOLUTE "${_source}")
            file(RELATIVE_PATH _relative_source "${CMAKE_SOURCE_DIR}" "${_source}")
            if(NOT _relative_source MATCHES "^\\.\\.(/|$)")
                set(_source "${_relative_source}")
            endif()
        endif()
        list(APPEND _relative_sources "${_source}")
        if(_source_definitions AND NOT _source_definitions MATCHES "-NOTFOUND$")
            _beam_full_json_string(_defined_source "${_source}")
            _beam_full_json_array(_defined_values ${_source_definitions})
            list(APPEND _source_definition_records "{\"source\":${_defined_source},\"compile_definitions\":${_defined_values}}")
        endif()
    endforeach()
    list(JOIN _source_definition_records "," _source_definitions_json)
    _beam_full_json_array(_source_json ${_relative_sources})
    _beam_full_json_string(_path_json "${relative}")
    _beam_full_json_string(_target_json "${target}")
    _beam_full_json_string(_kind_json "${kind}")
    set(_record "{\"path\":${_path_json},\"kind\":${_kind_json},\"target\":${_target_json},\"sources\":${_source_json}")
    string(APPEND _record ",\"source_compile_definitions\":[${_source_definitions_json}]")
    foreach(_property COMPILE_DEFINITIONS COMPILE_OPTIONS INCLUDE_DIRECTORIES LINK_OPTIONS LINK_LIBRARIES)
        get_target_property(_values ${target} ${_property})
        if(NOT _values OR _values MATCHES "-NOTFOUND$")
            set(_values)
        endif()
        string(TOLOWER "${_property}" _key)
        _beam_full_json_array(_values_json ${_values})
        string(APPEND _record ",\"${_key}\":${_values_json}")
    endforeach()
    # get_target_property above reports direct settings only. Record the shared
    # ERTS usage requirements too, including their language guard, so Debug's
    # inherited checks/options remain part of the native preparation identity.
    get_target_property(_links ${target} LINK_LIBRARIES)
    if("beam-erlang::erts_headers" IN_LIST _links)
        get_target_property(_definitions beam_erts_headers INTERFACE_COMPILE_DEFINITIONS)
        get_target_property(_options beam_erts_headers INTERFACE_COMPILE_OPTIONS)
        foreach(_value _definitions _options)
            if(NOT ${_value} OR "${${_value}}" MATCHES "-NOTFOUND$")
                set(${_value} "")
            endif()
        endforeach()
        _beam_full_json_array(_definitions_json ${_definitions})
        _beam_full_json_string(_options_json "${_options}")
        string(APPEND _record ",\"erts_interface\":{\"compile_definitions\":${_definitions_json},\"compile_options_expression\":${_options_json}}")
    endif()
    string(APPEND _record "}")
    set(_beam_full_native_records ${_beam_full_native_records} "${_record}" PARENT_SCOPE)
endfunction()

function(_beam_full_register target relative kind)
    set_target_properties(${target} PROPERTIES
        BEAM_FULL_ARTIFACT_PATH "${relative}"
        BEAM_FULL_ARTIFACT_KIND "${kind}")
    set(_beam_full_native_targets ${_beam_full_native_targets} ${target} PARENT_SCOPE)
    set(BEAM_FULL_NATIVE_ARTIFACTS ${BEAM_FULL_NATIVE_ARTIFACTS} "${relative}" PARENT_SCOPE)
endfunction()

function(_beam_full_fixture_target target)
    target_include_directories(${target} PRIVATE ${_beam_full_candidate_includes})
    # These consumers live in tests/, outside the custom-command directory.
    # Make their ordering explicit even when the candidate has not been built.
    add_dependencies(${target} emulator_generated_headers_${ERTS_BUILD_FLAVOR})
    # Release's default -DNDEBUG must not disable upstream fixture assert().
    if(WIN32)
        target_compile_options(${target} PRIVATE /UNDEBUG)
        target_compile_definitions(${target} PRIVATE __WIN32__ WIN32 WINDOWS NT
            _CRT_SECURE_NO_DEPRECATE _WIN32_WINNT=0x0600 WINVER=0x0600
            FD_SETSIZE=1024 ERTS_MIXED_VC USE_THREADS WIN32_THREADS
            _THREAD_SAFE _REENTRANT ${_beam_full_fixture_definitions})
    else()
        target_compile_options(${target} PRIVATE -UNDEBUG -fno-common)
        target_compile_definitions(${target} PRIVATE _DARWIN_C_SOURCE=
            ${_beam_full_fixture_definitions})
    endif()
    target_link_libraries(${target} PRIVATE beam-erlang::erts_headers)
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()

macro(_beam_full_module suite name)
    set(_target "beam_full_${suite}_${name}")
    if(WIN32)
        set(_module_suffix ".dll")
    else()
        set(_module_suffix ".so")
    endif()
    set(_relative "emulator_test/${suite}_SUITE_data/${name}${_module_suffix}")
    add_library(${_target} MODULE ${ARGN})
    _beam_full_fixture_target(${_target})
    # CMake emits a Mach-O bundle for MODULE. Resolve callbacks in the process's
    # existing VM; this deliberately replaces the upstream test configure's
    # legacy -flat_namespace -undefined suppress with dynamic_lookup.
    if(APPLE)
        target_link_options(${_target} PRIVATE "LINKER:-undefined,dynamic_lookup")
    endif()
    set_target_properties(${_target} PROPERTIES PREFIX "" SUFFIX "${_module_suffix}"
        OUTPUT_NAME "${name}"
        ARCHIVE_OUTPUT_DIRECTORY "${BEAM_FULL_NATIVE_ROOT}/emulator_test/${suite}_SUITE_data"
        RUNTIME_OUTPUT_DIRECTORY "${BEAM_FULL_NATIVE_ROOT}/emulator_test/${suite}_SUITE_data"
        LIBRARY_OUTPUT_DIRECTORY "${BEAM_FULL_NATIVE_ROOT}/emulator_test/${suite}_SUITE_data")
    _beam_full_register(${_target} "${_relative}" module)
endmacro()

macro(_beam_full_program suite name)
    set(_target "beam_full_${suite}_${name}")
    set(_relative "emulator_test/${suite}_SUITE_data/${name}${CMAKE_EXECUTABLE_SUFFIX}")
    add_executable(${_target} "${_beam_full_corpus}/${suite}_SUITE_data/${name}.c")
    _beam_full_fixture_target(${_target})
    set_target_properties(${_target} PROPERTIES OUTPUT_NAME "${name}"
        RUNTIME_OUTPUT_DIRECTORY "${BEAM_FULL_NATIVE_ROOT}/emulator_test/${suite}_SUITE_data")
endmacro()

# EI's own config.h is separate from the VM config. No external GMP or other
# package discovery changes this private library's reproducible input closure.
function(_beam_full_configure_ei output_dir)
    include(CheckIncludeFile)
    include(CheckSymbolExists)
    include(CheckTypeSize)
    foreach(_header IN ITEMS alloca.h arpa/inet.h fcntl.h inttypes.h limits.h
            netdb.h netinet/in.h pthread.h stddef.h stdint.h stdio.h stdlib.h
            strings.h string.h sys/param.h sys/select.h sys/socket.h sys/stat.h
            sys/time.h sys/types.h sys/uio.h sys/wait.h unistd.h)
        string(TOUPPER "${_header}" _macro)
        string(REGEX REPLACE "[/.]" "_" _macro "${_macro}")
        check_include_file("${_header}" "BEAM_FULL_EI_HAVE_${_macro}")
        set("HAVE_${_macro}" "${BEAM_FULL_EI_HAVE_${_macro}}")
    endforeach()
    foreach(_probe IN ITEMS "alloca|alloca.h" "dup2|unistd.h"
            "gethostbyaddr|netdb.h" "gethostbyaddr_r|netdb.h"
            "gethostbyname|netdb.h" "gethostbyname_r|netdb.h"
            "gethostname|unistd.h" "gethrtime|sys/time.h"
            "gettimeofday|sys/time.h" "inet_ntoa|arpa/inet.h"
            "memchr|string.h" "memmove|string.h" "memset|string.h"
            "select|sys/select.h" "socket|sys/socket.h" "strchr|string.h"
            "strerror|string.h" "strrchr|string.h" "strstr|string.h"
            "sysconf|unistd.h" "uname|sys/utsname.h" "writev|sys/uio.h")
        string(REPLACE "|" ";" _parts "${_probe}")
        list(GET _parts 0 _symbol)
        list(GET _parts 1 _headers)
        string(TOUPPER "${_symbol}" _macro)
        check_symbol_exists("${_symbol}" "${_headers}" "BEAM_FULL_EI_HAVE_${_macro}")
        set("HAVE_${_macro}" "${BEAM_FULL_EI_HAVE_${_macro}}")
    endforeach()
    if(WIN32)
        # EI's retained Windows sources include winsock2.h, not ws2tcpip.h.
        # Match that visibility: newer SDKs typedef socklen_t only in the latter.
        set(CMAKE_EXTRA_INCLUDE_FILES winsock2.h)
    else()
        set(CMAKE_EXTRA_INCLUDE_FILES sys/types.h sys/socket.h)
    endif()
    check_type_size(socklen_t BEAM_FULL_EI_SIZEOF_VISIBLE_SOCKLEN_T)
    unset(CMAKE_EXTRA_INCLUDE_FILES)
    unset(HAVE_SOCKLEN_T)
    if(BEAM_FULL_EI_SIZEOF_VISIBLE_SOCKLEN_T)
        set(HAVE_SOCKLEN_T 1)
    endif()
    set(STDC_HEADERS 1)
    set(PACKAGE_NAME "\"erl_interface\"")
    set(PACKAGE_TARNAME "\"erl_interface\"")
    set(PACKAGE_STRING "\"private emulator test EI\"")
    # Unset means use the safe portable implementation, without optional GMP.
    unset(HAVE_GMP_H)
    unset(HAVE_LIBGMP)
    unset(HAVE_UNALIGNED_WORD_ACCESS)
    file(READ "${BEAM_FULL_UPSTREAM}/lib/erl_interface/config.h.in" _template)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${BEAM_FULL_UPSTREAM}/lib/erl_interface/config.h.in")
    string(REGEX REPLACE "#undef ([A-Za-z0-9_]+)" "#cmakedefine \\1 @\\1@" _template "${_template}")
    file(MAKE_DIRECTORY "${output_dir}")
    file(WRITE "${output_dir}/config.h.cmake.in" "${_template}")
    configure_file("${output_dir}/config.h.cmake.in" "${output_dir}/config.h" @ONLY)
endfunction()

set(_beam_full_ei "${BEAM_FULL_UPSTREAM}/lib/erl_interface")
set(_beam_full_ei_sources)
foreach(_group IN ITEMS
        "connect:ei_connect,ei_resolve,eirecv,send,send_exit,send_reg"
        "decode:decode_atom,decode_big,decode_bignum,decode_binary,decode_boolean,decode_char,decode_double,decode_fun,decode_intlist,decode_iodata,decode_list_header,decode_long,decode_pid,decode_port,decode_ref,decode_skip,decode_string,decode_trace,decode_tuple_header,decode_ulong,decode_version,decode_longlong,decode_ulonglong"
        "encode:encode_atom,encode_big,encode_bignum,encode_binary,encode_boolean,encode_char,encode_double,encode_fun,encode_list_header,encode_long,encode_pid,encode_port,encode_ref,encode_string,encode_trace,encode_tuple_header,encode_ulong,encode_version,encode_longlong,encode_ulonglong"
        "epmd:epmd_port,epmd_publish,epmd_unpublish"
        "misc:ei_decode_term,ei_format,ei_locking,ei_malloc,ei_portio,ei_printterm,ei_pthreads,ei_trace,ei_x_encode,get_type,show_msg,ei_compat,ei_init,ei_cmp_nc"
        "global:global_names,global_register,global_unregister,global_whereis")
    string(REPLACE ":" ";" _parts "${_group}")
    list(GET _parts 0 _directory)
    list(GET _parts 1 _names)
    string(REPLACE "," ";" _names "${_names}")
    foreach(_name IN LISTS _names)
        list(APPEND _beam_full_ei_sources "${_beam_full_ei}/src/${_directory}/${_name}.c")
    endforeach()
endforeach()
list(APPEND _beam_full_ei_sources "${CMAKE_SOURCE_DIR}/erts/emulator/beam/erl_md5.c")
_beam_full_configure_ei("${BEAM_FULL_NATIVE_ROOT}/config/ei")
add_library(beam_full_ei STATIC ${_beam_full_ei_sources})
target_include_directories(beam_full_ei PRIVATE
    "${BEAM_FULL_NATIVE_ROOT}/config/ei" "${_beam_full_ei}/include"
    "${_beam_full_ei}/src" "${_beam_full_ei}/src/connect"
    "${_beam_full_ei}/src/encode" "${_beam_full_ei}/src/decode"
    "${_beam_full_ei}/src/misc" "${_beam_full_ei}/src/epmd"
    "${_beam_full_ei}/src/global" "${CMAKE_SOURCE_DIR}/erts/emulator/beam")
target_compile_definitions(beam_full_ei PRIVATE ERL_INTERFACE EI_NO_DEPR_WARN
    _REENTRANT _THREAD_SAFE)
if(WIN32)
    target_compile_definitions(beam_full_ei PRIVATE __WIN32__ WIN32
        _CRT_SECURE_NO_DEPRECATE _WIN32_WINNT=0x0600)
    target_compile_options(beam_full_ei PRIVATE /UNDEBUG)
    target_link_libraries(beam_full_ei PUBLIC ws2_32)
else()
    target_compile_definitions(beam_full_ei PRIVATE _DARWIN_C_SOURCE=)
    target_compile_options(beam_full_ei PRIVATE -UNDEBUG)
endif()
target_link_libraries(beam_full_ei PRIVATE Threads::Threads)
set_target_properties(beam_full_ei PROPERTIES OUTPUT_NAME ei
    POSITION_INDEPENDENT_CODE ON
    ARCHIVE_OUTPUT_DIRECTORY "${BEAM_FULL_NATIVE_ROOT}/lib/erl_interface/lib")
_beam_full_register(beam_full_ei "lib/erl_interface/lib/${CMAKE_STATIC_LIBRARY_PREFIX}ei${CMAKE_STATIC_LIBRARY_SUFFIX}" static-library)

set(_beam_full_header_outputs)
foreach(_header ei.h ei_connect.h ei_global.h eicode.h)
    set(_relative "lib/erl_interface/include/${_header}")
    set(_source "${_beam_full_ei}/include/${_header}")
    set(_output "${BEAM_FULL_NATIVE_ROOT}/${_relative}")
    add_custom_command(OUTPUT "${_output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${BEAM_FULL_NATIVE_ROOT}/lib/erl_interface/include"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_source}" "${_output}"
        DEPENDS "${_source}" VERBATIM)
    list(APPEND _beam_full_header_outputs "${_output}")
    list(APPEND BEAM_FULL_NATIVE_ARTIFACTS "${_relative}")
    _beam_full_json_string(_path_json "${_relative}")
    file(RELATIVE_PATH _source_relative "${CMAKE_SOURCE_DIR}" "${_source}")
    _beam_full_json_array(_sources_json "${_source_relative}")
    list(APPEND _beam_full_native_records
        "{\"path\":${_path_json},\"kind\":\"header\",\"sources\":${_sources_json}}")
endforeach()

_beam_full_module(a timer_driver "${_beam_full_corpus}/a_SUITE_data/timer_driver.c")
foreach(_name basic coalesce threads realloc_copy bucket_index bucket_mask
        rbtree mseg_clear_cache cpool migration)
    _beam_full_module(alloc "${_name}"
        "${_beam_full_corpus}/alloc_SUITE_data/${_name}.c"
        "${_beam_full_corpus}/alloc_SUITE_data/testcase_driver.c")
endforeach()
_beam_full_program(async_ports cport)
_beam_full_register(${_target} "${_relative}" executable)
foreach(_name send_term_local_drv call_local_drv)
    _beam_full_module(binary "${_name}" "${_beam_full_corpus}/binary_SUITE_data/${_name}.c")
    target_include_directories(${_target} PRIVATE "${_beam_full_ei}/include")
    target_link_libraries(${_target} PRIVATE beam_full_ei Threads::Threads)
endforeach()
foreach(_name busy_drv hard_busy_drv soft_busy_drv scheduling_drv)
    _beam_full_module(busy_port "${_name}" "${_beam_full_corpus}/busy_port_SUITE_data/${_name}.c")
endforeach()
foreach(_name echo_drv echo_drv_fail_init noinit_drv wrongname_drv initfail_drv dummy_drv lock_drv)
    _beam_full_module(ddll "${_name}" "${_beam_full_corpus}/ddll_SUITE_data/${_name}.c")
endforeach()
foreach(_name dirty_nif_SUITE on_halt_a on_halt_b on_halt_c on_halt_d on_halt_e on_halt_f echo_drv)
    _beam_full_module(dirty_nif "${_name}" "${_beam_full_corpus}/dirty_nif_SUITE_data/${_name}.c")
endforeach()
foreach(_name outputv_drv timer_drv queue_drv io_ready_exit_drv chkio_drv monitor_drv
        ioq_exit_drv peek_non_existing_queue_drv otp_6879_drv caller_drv many_events_drv
        missing_callback_drv thr_alloc_drv otp_9302_drv thr_free_drv async_blast_drv
        thr_msg_blast_drv consume_timeslice_drv env_drv erl_errno_id_drv
        sys_info_base_drv sys_info_prev_drv sys_info_curr_drv
        zero_extended_marker_garb_drv invalid_extended_marker_drv larger_major_vsn_drv
        larger_minor_vsn_drv smaller_major_vsn_drv smaller_minor_vsn_drv)
    _beam_full_module(driver "${_name}" "${_beam_full_corpus}/driver_SUITE_data/${_name}.c")
endforeach()
_beam_full_program(driver lots_of_fds_used_wrapper)
_beam_full_register(${_target} "${_relative}" executable)
foreach(_name basic rwlock tsd)
    _beam_full_module(erl_drv_thread "${_name}"
        "${_beam_full_corpus}/erl_drv_thread_SUITE_data/${_name}.c"
        "${_beam_full_corpus}/erl_drv_thread_SUITE_data/testcase_driver.c")
endforeach()
_beam_full_program(estone estone_cat)
_beam_full_register(${_target} "${_relative}" executable)
_beam_full_module(float fp_drv "${_beam_full_corpus}/float_SUITE_data/fp_drv.c")
include(CheckSymbolExists)
if(WIN32)
    check_symbol_exists(_finite "float.h" BEAM_FULL_HAVE_FINITE)
    if(NOT BEAM_FULL_HAVE_FINITE)
        message(FATAL_ERROR "The full emulator fp_drv fixture requires native _finite")
    endif()
else()
    check_symbol_exists(isfinite "math.h" BEAM_FULL_HAVE_ISFINITE)
    if(NOT BEAM_FULL_HAVE_ISFINITE)
        message(FATAL_ERROR "The full emulator fp_drv fixture requires native isfinite")
    endif()
    target_compile_definitions(${_target} PRIVATE HAVE_ISFINITE=1)
endif()
_beam_full_module(lttng caller_drv "${_beam_full_corpus}/lttng_SUITE_data/caller_drv.c")
_beam_full_module(mtx mtx_SUITE "${_beam_full_corpus}/mtx_SUITE_data/mtx_SUITE.c")
if(NOT WIN32)
    target_compile_definitions(${_target} PRIVATE USE_THREADS _THREAD_SAFE _REENTRANT
        POSIX_THREADS _POSIX_THREAD_SAFE_FUNCTIONS)
endif()
foreach(_name nif_SUITE.1 nif_mod.1 nif_mod.2 nif_mod.3 nif_mod.4 nif_mod.5
        nif_mod.1.2_0 nif_mod.2.2_0 nif_mod.3.2_0
        nif_mod.1.2_4 nif_mod.2.2_4 nif_mod.3.2_4 echo_drv)
    _beam_full_module(nif "${_name}" "${_beam_full_corpus}/nif_SUITE_data/${_name}.c")
endforeach()
# Preserve the upstream sed/head/tail recipe without generating into source.
foreach(_name basic rwlock tsd)
    set(_input "${_beam_full_corpus}/erl_drv_thread_SUITE_data/${_name}.c")
    file(READ "${_input}" _body)
    string(REPLACE "erl_drv_" "enif_" _body "${_body}")
    string(REPLACE "driver_" "enif_" _body "${_body}")
    string(REPLACE "ErlDrv" "ErlNif" _body "${_body}")
    file(READ "${_beam_full_corpus}/nif_SUITE_data/head.txt" _head)
    file(READ "${_beam_full_corpus}/nif_SUITE_data/tail.txt" _tail)
    set(_generated "${BEAM_FULL_NATIVE_ROOT}/generated/nif/${_name}.c")
    file(MAKE_DIRECTORY "${BEAM_FULL_NATIVE_ROOT}/generated/nif")
    file(CONFIGURE OUTPUT "${_generated}" CONTENT "${_head}${_body}${_tail}" @ONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_input}"
        "${_beam_full_corpus}/nif_SUITE_data/head.txt" "${_beam_full_corpus}/nif_SUITE_data/tail.txt")
    _beam_full_module(nif "${_name}" "${_generated}")
    target_include_directories(${_target} PRIVATE "${_beam_full_corpus}/nif_SUITE_data")
    set_target_properties(${_target} PROPERTIES BEAM_FULL_SOURCE_ORIGINS
        "${_input};${_beam_full_corpus}/nif_SUITE_data/head.txt;${_beam_full_corpus}/nif_SUITE_data/tail.txt;${_beam_full_corpus}/nif_SUITE_data/tester.c")
endforeach()
# The unchanged suite's all/0 returns [] on Windows; its fixture uses fork/alarm.
if(NOT WIN32)
    _beam_full_module(os_signal os_signal_nif "${_beam_full_corpus}/os_signal_SUITE_data/os_signal_nif.c")
endif()
_beam_full_module(persistent_term erts_test_destructor
    "${_beam_full_corpus}/persistent_term_SUITE_data/erts_test_destructor.c")
foreach(_name port_test echo_args dead_port)
    _beam_full_program(port "${_name}")
    _beam_full_register(${_target} "${_relative}" executable)
endforeach()
foreach(_name echo_drv exit_drv failure_drv sleep_failure_drv)
    _beam_full_module(port "${_name}" "${_beam_full_corpus}/port_SUITE_data/${_name}.c")
endforeach()
_beam_full_module(port_bif control_drv "${_beam_full_corpus}/port_bif_SUITE_data/control_drv.c")
foreach(_name port_test sleeper)
    _beam_full_program(port_bif "${_name}")
    _beam_full_register(${_target} "${_relative}" executable)
endforeach()
foreach(_suite port_trace system_profile trace_port)
    _beam_full_module("${_suite}" echo_drv "${_beam_full_corpus}/${_suite}_SUITE_data/echo_drv.c")
endforeach()
_beam_full_program(process fwd_node)
target_include_directories(${_target} PRIVATE "${_beam_full_ei}/include")
target_compile_definitions(${_target} PRIVATE _REENTRANT _THREAD_SAFE)
target_link_libraries(${_target} PRIVATE beam_full_ei Threads::Threads)
_beam_full_register(${_target} "${_relative}" executable)
_beam_full_module(send_term send_term_drv "${_beam_full_corpus}/send_term_SUITE_data/send_term_drv.c")
_beam_full_module(signal unlink_signal_drv "${_beam_full_corpus}/signal_SUITE_data/unlink_signal_drv.c")
foreach(_name slow_drv trace_SUITE)
    _beam_full_module(trace "${_name}" "${_beam_full_corpus}/trace_SUITE_data/${_name}.c")
endforeach()
foreach(_suite trace_call_time trace_nif)
    _beam_full_module("${_suite}" trace_nif "${_beam_full_corpus}/${_suite}_SUITE_data/trace_nif.c")
endforeach()
_beam_full_module(tracer tracer_test "${_beam_full_corpus}/tracer_SUITE_data/tracer_test.c")

# Small upstream support libraries are independent of the emulator object library.
add_library(beam_full_erts_internal STATIC
    "${CMAKE_SOURCE_DIR}/erts/lib_src/common/erl_printf_format.c"
    "${CMAKE_SOURCE_DIR}/erts/lib_src/common/erl_printf.c"
    "${CMAKE_SOURCE_DIR}/erts/lib_src/common/erl_misc_utils.c")
_beam_full_fixture_target(beam_full_erts_internal)
target_compile_definitions(beam_full_erts_internal PRIVATE HAVE_CONFIG_H)
set_target_properties(beam_full_erts_internal PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${BEAM_FULL_NATIVE_ROOT}/lib/private")
foreach(_name erlexec heart inet_gethost epmd)
    set(_target "beam_full_tool_${_name}")
    if(_name STREQUAL "epmd")
        set(_tool_source_root "${BEAM_FULL_UPSTREAM}/erts/epmd/src")
        add_executable(${_target} "${_tool_source_root}/epmd.c"
            "${_tool_source_root}/epmd_cli.c" "${_tool_source_root}/epmd_srv.c")
        target_compile_definitions(${_target} PRIVATE EPMD_PORT_NO=4369)
    else()
        set(_tool_source_root "${BEAM_FULL_UPSTREAM}/erts/etc/common")
        add_executable(${_target} "${_tool_source_root}/${_name}.c")
    endif()
    _beam_full_fixture_target(${_target})
    target_include_directories(${_target} PRIVATE "${_tool_source_root}")
    target_compile_definitions(${_target} PRIVATE HAVE_CONFIG_H)
    if(_name STREQUAL "erlexec")
        target_compile_definitions(${_target} PRIVATE OTP_SYSTEM_VERSION="29")
        if(WIN32)
            target_sources(${_target} PRIVATE
                "${BEAM_FULL_UPSTREAM}/erts/etc/win32/init_file.c"
                "${CMAKE_CURRENT_SOURCE_DIR}/emulator/beam_emulator_windows_erlexec.c"
                "${CMAKE_CURRENT_SOURCE_DIR}/emulator/beam_emulator_windows_process.c"
                "${CMAKE_CURRENT_SOURCE_DIR}/emulator/beam_emulator_windows_process.h")
            target_include_directories(${_target} PRIVATE
                "${BEAM_FULL_UPSTREAM}/erts/etc/win32")
            target_compile_definitions(${_target} PRIVATE BEAM_TEST_PROCESS_LIBRARY
                BEAM_TEST_EXPECTED_FLAVOR="${ERTS_BUILD_FLAVOR}"
                BEAM_TEST_EXPECTED_BUILD_TYPE="${_erts_build_type}")
            if(CMAKE_BUILD_TYPE STREQUAL "Debug")
                # Preserve erlexec's upstream Debug default while allowing an
                # explicit incompatible opt request to be rejected by the adapter.
                set_property(SOURCE "${_tool_source_root}/erlexec.c" APPEND PROPERTY
                    COMPILE_DEFINITIONS WIN32_ALWAYS_DEBUG)
            endif()
            target_link_libraries(${_target} PRIVATE advapi32 iphlpapi ws2_32 shell32 ole32 user32)
        endif()
    endif()
    target_link_libraries(${_target} PRIVATE beam_full_erts_internal)
    if(WIN32)
        target_link_libraries(${_target} PRIVATE ws2_32 advapi32 user32)
    endif()
    set_target_properties(${_target} PROPERTIES
        BEAM_FULL_SOURCE_ORIGINS
            "${CMAKE_SOURCE_DIR}/erts/lib_src/common/erl_printf_format.c;${CMAKE_SOURCE_DIR}/erts/lib_src/common/erl_printf.c;${CMAKE_SOURCE_DIR}/erts/lib_src/common/erl_misc_utils.c")
    if(WIN32 AND _name STREQUAL "erlexec")
        set_target_properties(${_target} PROPERTIES OUTPUT_NAME erl
            RUNTIME_OUTPUT_DIRECTORY "${BEAM_FULL_NATIVE_ROOT}/bin")
        _beam_full_register(${_target} "bin/erl.exe" executable)
    else()
        set_target_properties(${_target} PROPERTIES OUTPUT_NAME "${_name}"
            RUNTIME_OUTPUT_DIRECTORY "${BEAM_FULL_NATIVE_ROOT}/erts-17.0/bin")
        _beam_full_register(${_target} "erts-17.0/bin/${_name}${CMAKE_EXECUTABLE_SUFFIX}" executable)
    endif()
endforeach()
if(WIN32)
    set(_beam_full_os_mon_programs win32sysinfo nteventlog)
else()
    set(_beam_full_os_mon_programs memsup cpu_sup)
endif()
foreach(_name IN LISTS _beam_full_os_mon_programs)
    set(_target "beam_full_os_mon_${_name}")
    if(_name STREQUAL "nteventlog")
        set(_os_mon_sources)
        foreach(_part main util registry pipe_stdin format)
            list(APPEND _os_mon_sources "${BEAM_FULL_UPSTREAM}/lib/os_mon/c_src/nteventlog/elog_${_part}.c")
        endforeach()
        add_executable(${_target} ${_os_mon_sources})
    else()
        add_executable(${_target} "${BEAM_FULL_UPSTREAM}/lib/os_mon/c_src/${_name}.c")
    endif()
    if(WIN32)
        target_sources(${_target} PRIVATE "${BEAM_FULL_UPSTREAM}/erts/etc/win32/port_entry.c")
        target_compile_options(${_target} PRIVATE /UNDEBUG)
        target_compile_definitions(${_target} PRIVATE __WIN32__ _CRT_SECURE_NO_DEPRECATE)
        target_link_options(${_target} PRIVATE /ENTRY:erl_port_entry)
        target_link_libraries(${_target} PRIVATE advapi32 user32)
    else()
        target_compile_options(${_target} PRIVATE -UNDEBUG)
    endif()
    set_target_properties(${_target} PROPERTIES OUTPUT_NAME "${_name}"
        RUNTIME_OUTPUT_DIRECTORY "${BEAM_FULL_NATIVE_ROOT}/lib/os_mon/priv/bin")
    _beam_full_register(${_target} "lib/os_mon/priv/bin/${_name}${CMAKE_EXECUTABLE_SUFFIX}" executable)
endforeach()

if(WIN32)
    # The retained port suites invoke these ordinary commands. Keep native,
    # project-owned implementations in the private image's command path.
    foreach(_name true ls sleep)
        set(_target "beam_full_command_${_name}")
        add_executable(${_target}
            "${CMAKE_CURRENT_SOURCE_DIR}/emulator/beam_emulator_windows_command.c")
        target_compile_options(${_target} PRIVATE /UNDEBUG)
        set_target_properties(${_target} PROPERTIES OUTPUT_NAME "${_name}"
            RUNTIME_OUTPUT_DIRECTORY "${BEAM_FULL_NATIVE_ROOT}/bin")
        _beam_full_register(${_target} "bin/${_name}.exe" executable)
    endforeach()
endif()

add_custom_target(beam_emulator_full_native
    DEPENDS ${_beam_full_native_targets} ${_beam_full_header_outputs})
foreach(_target IN LISTS _beam_full_native_targets)
    get_target_property(_relative ${_target} BEAM_FULL_ARTIFACT_PATH)
    get_target_property(_kind ${_target} BEAM_FULL_ARTIFACT_KIND)
    _beam_full_describe(${_target} "${_relative}" "${_kind}")
endforeach()
list(JOIN _beam_full_native_records ",\n" _records_json)
set(_toolchain_json "{")
foreach(_key CMAKE_C_COMPILER CMAKE_C_COMPILER_ID CMAKE_C_COMPILER_VERSION
        CMAKE_BUILD_TYPE CMAKE_C_FLAGS CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_DEBUG
        CMAKE_CXX_COMPILER CMAKE_CXX_COMPILER_ID CMAKE_CXX_COMPILER_VERSION
        CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_DEBUG CMAKE_OSX_SYSROOT
        CMAKE_OSX_DEPLOYMENT_TARGET CMAKE_OSX_ARCHITECTURES
        CMAKE_SYSTEM_NAME CMAKE_SYSTEM_VERSION CMAKE_SYSTEM_PROCESSOR
        CMAKE_MSVC_RUNTIME_LIBRARY CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
    _beam_full_json_string(_value "${${_key}}")
    string(APPEND _toolchain_json "\"${_key}\":${_value},")
endforeach()
string(REGEX REPLACE ",$" "}" _toolchain_json "${_toolchain_json}")
set(BEAM_FULL_NATIVE_MANIFEST "${BEAM_FULL_NATIVE_ROOT}/native-manifest.json")
file(MAKE_DIRECTORY "${BEAM_FULL_NATIVE_ROOT}")
file(WRITE "${BEAM_FULL_NATIVE_MANIFEST}"
    "{\"schema_version\":2,\"toolchain\":${_toolchain_json},\"artifacts\":[\n${_records_json}\n]}\n")
# port_SUITE:otp_3906 consults {'CC', Path} from the code path. Keep this file
# out of test_server/ so ts_install_cth does not start the upstream make path.
get_filename_component(_beam_full_cc "${CMAKE_C_COMPILER}" ABSOLUTE)
if(NOT EXISTS "${_beam_full_cc}")
    message(FATAL_ERROR "Selected C compiler does not exist: ${_beam_full_cc}")
endif()
file(MAKE_DIRECTORY "${BEAM_FULL_NATIVE_ROOT}/compiler_config")
set(BEAM_FULL_COMPILER_VARIABLES "${BEAM_FULL_NATIVE_ROOT}/compiler_config/variables")
file(WRITE "${BEAM_FULL_COMPILER_VARIABLES}" "{'CC', \"${_beam_full_cc}\"}.\n")
