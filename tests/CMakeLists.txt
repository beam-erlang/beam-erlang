# Exercise the VM directly, using the checked-in upstream bootstrap as test data.
# No installed Erlang launcher, compiler, or OTP distribution participates.

# Compile each public header as the first include in C11 and C++17 consumers.
# These object targets deliberately use only public source/generated include
# paths, without the internal ERTS interface, VM linkage or injected sizes.
foreach(_erts_public_header nif driver fixed)
    string(TOUPPER "${_erts_public_header}" _erts_public_header_upper)
    foreach(_erts_public_language c cpp)
        set(_erts_public_target "public_headers_${_erts_public_header}_${_erts_public_language}")
        add_library(${_erts_public_target} OBJECT
            "runtime/beam_public_headers.${_erts_public_language}")
        target_include_directories(${_erts_public_target} PRIVATE
            "${CMAKE_BINARY_DIR}/erts/include"
            "${CMAKE_SOURCE_DIR}/erts/include"
            "${CMAKE_SOURCE_DIR}/erts/emulator/beam")
        if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
            # erl_driver.h includes the retained Windows driver companion.
            target_include_directories(${_erts_public_target} PRIVATE
                "${CMAKE_SOURCE_DIR}/erts/emulator/sys/win32")
        endif()
        target_compile_definitions(${_erts_public_target} PRIVATE
            "BEAM_PUBLIC_HEADER_${_erts_public_header_upper}=1")
        set_target_properties(${_erts_public_target} PROPERTIES
            C_STANDARD 11 C_STANDARD_REQUIRED TRUE C_EXTENSIONS FALSE
            CXX_STANDARD 17 CXX_STANDARD_REQUIRED TRUE CXX_EXTENSIONS FALSE
            COMPILE_WARNING_AS_ERROR TRUE)
    endforeach()
endforeach()

get_filename_component(_erts_workspace "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)
string(SHA256 _erts_test_id "${CMAKE_BINARY_DIR}")
string(SUBSTRING "${_erts_test_id}" 0 12 _erts_test_id)

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(_erts_poll_backend kqueue)
    set(_erts_kernel_poll true)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(_erts_poll_backend WaitForMultipleObjects)
    set(_erts_kernel_poll false)
else()
    set(_erts_poll_backend epoll)
    set(_erts_kernel_poll true)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_erts_build_type debug)
else()
    set(_erts_build_type opt)
endif()

foreach(_erts_test_mode build install)
    add_test(NAME runtime.${_erts_test_mode}
        COMMAND "${CMAKE_COMMAND}"
            "-DMODE=${_erts_test_mode}"
            "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
            "-DBINARY_DIR=${CMAKE_BINARY_DIR}"
            "-DRUNTIME_BIN=$<TARGET_FILE_DIR:beam>"
            "-DINSTALL_BINDIR=${CMAKE_INSTALL_BINDIR}"
            "-DEXPECTED_FLAVOR=${ERTS_BUILD_FLAVOR}"
            "-DEXPECTED_BUILD_TYPE=${_erts_build_type}"
            "-DEXPECTED_BACKEND=${_erts_poll_backend}"
            "-DEXPECTED_KERNEL_POLL=${_erts_kernel_poll}"
            "-DEXPECTED_ARCH=${ERTS_TARGET_ARCH}"
            "-DTARGET_SYSTEM=${CMAKE_SYSTEM_NAME}"
            "-DDEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}"
            "-DTEST_ROOT=${_erts_workspace}/tmp/beam-runtime-${_erts_test_id}-${_erts_test_mode}"
            -P "${CMAKE_CURRENT_LIST_DIR}/cmake/RuntimeTest.cmake"
    )
    set_tests_properties(runtime.${_erts_test_mode} PROPERTIES
        LABELS "runtime;${ERTS_BUILD_FLAVOR}"
        TIMEOUT 180
    )
endforeach()

# The installer writes its manifest in the build tree.
set_tests_properties(runtime.install PROPERTIES
    RUN_SERIAL TRUE
    SKIP_REGULAR_EXPRESSION "Runtime install integration skipped: absolute CMAKE_INSTALL_BINDIR")

option(BEAM_BUILD_EMULATOR_TESTS "Build the first five retained Common Test emulator suites" OFF)
option(BEAM_BUILD_FULL_EMULATOR_TESTS "Build the complete retained Common Test emulator corpus" OFF)

if(BEAM_BUILD_EMULATOR_TESTS OR BEAM_BUILD_FULL_EMULATOR_TESTS)
    # Imported test support from upstream Erlang/OTP 550d7b7898706c7822362c42e7b93120c1d1f29a.
    # Shared paths are relative to tests/emulator/support.
    set(BEAM_EMULATOR_UPSTREAM_FILES
        "lib/common_test/include/ct.hrl"
        "lib/common_test/include/ct_event.hrl"
        "lib/common_test/include/ct_property_test.hrl"
        "lib/common_test/priv/ct_default.css"
        "lib/common_test/priv/jquery-latest.js"
        "lib/common_test/priv/jquery-latest.js.license"
        "lib/common_test/priv/jquery-migrate.js"
        "lib/common_test/priv/jquery-migrate.js.license"
        "lib/common_test/priv/jquery.tablesorter.min.js"
        "lib/common_test/priv/tile1.jpg"
        "lib/common_test/priv/tile1.jpg.license"
        "lib/common_test/priv/vendor.info"
        "lib/common_test/src/common_test.app.src"
        "lib/common_test/src/common_test.appup.src"
        "lib/common_test/src/ct.erl"
        "lib/common_test/src/ct_config.erl"
        "lib/common_test/src/ct_config_plain.erl"
        "lib/common_test/src/ct_config_xml.erl"
        "lib/common_test/src/ct_conn_log_h.erl"
        "lib/common_test/src/ct_cover.erl"
        "lib/common_test/src/ct_default_gl.erl"
        "lib/common_test/src/ct_doctest.erl"
        "lib/common_test/src/ct_event.erl"
        "lib/common_test/src/ct_framework.erl"
        "lib/common_test/src/ct_ftp.erl"
        "lib/common_test/src/ct_gen_conn.erl"
        "lib/common_test/src/ct_groups.erl"
        "lib/common_test/src/ct_hooks.erl"
        "lib/common_test/src/ct_hooks_lock.erl"
        "lib/common_test/src/ct_logs.erl"
        "lib/common_test/src/ct_make.erl"
        "lib/common_test/src/ct_master.erl"
        "lib/common_test/src/ct_master_event.erl"
        "lib/common_test/src/ct_master_logs.erl"
        "lib/common_test/src/ct_master_status.erl"
        "lib/common_test/src/ct_netconfc.erl"
        "lib/common_test/src/ct_netconfc.hrl"
        "lib/common_test/src/ct_property_test.erl"
        "lib/common_test/src/ct_release_test.erl"
        "lib/common_test/src/ct_repeat.erl"
        "lib/common_test/src/ct_rpc.erl"
        "lib/common_test/src/ct_run.erl"
        "lib/common_test/src/ct_slave.erl"
        "lib/common_test/src/ct_snmp.erl"
        "lib/common_test/src/ct_ssh.erl"
        "lib/common_test/src/ct_suite.erl"
        "lib/common_test/src/ct_telnet.erl"
        "lib/common_test/src/ct_telnet_client.erl"
        "lib/common_test/src/ct_testspec.erl"
        "lib/common_test/src/ct_util.erl"
        "lib/common_test/src/ct_util.hrl"
        "lib/common_test/src/cte_track.erl"
        "lib/common_test/src/cth_conn_log.erl"
        "lib/common_test/src/cth_log_redirect.erl"
        "lib/common_test/src/cth_surefire.erl"
        "lib/common_test/src/erl2html2.erl"
        "lib/common_test/src/test_server.erl"
        "lib/common_test/src/test_server_ctrl.erl"
        "lib/common_test/src/test_server_gl.erl"
        "lib/common_test/src/test_server_internal.hrl"
        "lib/common_test/src/test_server_io.erl"
        "lib/common_test/src/test_server_node.erl"
        "lib/common_test/src/test_server_sup.erl"
        "lib/common_test/src/unix_telnet.erl"
        "lib/common_test/test_server/ts_install_cth.erl"
        "lib/common_test/vsn.mk"
        "lib/kernel/src/logger_internal.hrl"
        "lib/snmp/include/snmp_types.hrl"
        "lib/syntax_tools/include/merl.hrl"
        "lib/syntax_tools/src/epp_dodger.erl"
        "lib/syntax_tools/src/erl_comment_scan.erl"
        "lib/syntax_tools/src/erl_prettypr.erl"
        "lib/syntax_tools/src/erl_recomment.erl"
        "lib/syntax_tools/src/erl_syntax.erl"
        "lib/syntax_tools/src/erl_syntax_lib.erl"
        "lib/syntax_tools/src/merl.erl"
        "lib/syntax_tools/src/merl_transform.erl"
        "lib/syntax_tools/src/prettypr.erl"
        "lib/syntax_tools/src/syntax_tools.app.src"
        "lib/syntax_tools/src/syntax_tools.appup.src"
        "lib/syntax_tools/vsn.mk"
        "lib/xmerl/include/xmerl.hrl"
    )
endif()

if(BEAM_BUILD_EMULATOR_TESTS)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/EmulatorTests.cmake")
endif()

if(BEAM_BUILD_FULL_EMULATOR_TESTS)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/FullEmulatorTests.cmake")
endif()
