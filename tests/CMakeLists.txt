find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
set(_package_test_args)
if(NOT BEAM_BUNDLE_PACKAGE_TOOL)
    list(APPEND _package_test_args --without-package)
endif()

# Native implementation coverage uses the same library/configuration as the VM.
# These private inputs are test-only and are not SDK exports or install assets.
add_executable(beam_ethread_tests
    vm/ethread_SUITE_data/ethread_tests.c
    "${PROJECT_SOURCE_DIR}/src/erts/lib_src/common/erl_misc_utils.c")
target_compile_definitions(beam_ethread_tests PRIVATE
    $<TARGET_PROPERTY:ethread,COMPILE_DEFINITIONS>)
target_include_directories(beam_ethread_tests PRIVATE
    $<TARGET_PROPERTY:ethread,INCLUDE_DIRECTORIES>)
target_link_libraries(beam_ethread_tests PRIVATE
    ethread Threads::Threads beam-erlang::erts_headers)
foreach(_case IN ITEMS create_join_thread equal_tids mutex try_lock_mutex cond_wait
        broadcast tsd spinlock rwspinlock rwmutex atomic dw_atomic_massage thread_name)
    add_test(NAME vm-ethread-${_case}
        COMMAND ${Python3_EXECUTABLE} -B -m vm.ethread
            --helper $<TARGET_FILE:beam_ethread_tests>
            --case ${_case}
            --test-dir "${CMAKE_CURRENT_BINARY_DIR}/vm/ethread/${_case}")
    set_tests_properties(vm-ethread-${_case} PROPERTIES
        TIMEOUT 620 RUN_SERIAL TRUE SKIP_RETURN_CODE 77
        LABELS "vm;native;shared" WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endforeach()

add_executable(beam_api_validation checks/api_validation.c)
target_link_libraries(beam_api_validation PRIVATE BEAM::Runtime Threads::Threads)
add_test(NAME api-validation COMMAND beam_api_validation)
add_executable(beam_bytecode_verifier_tests checks/bytecode_verifier.c
    "${PROJECT_SOURCE_DIR}/src/beam_bytecode_verify.c")
target_link_libraries(beam_bytecode_verifier_tests PRIVATE BEAM::Runtime)
add_test(NAME bytecode-verifier COMMAND beam_bytecode_verifier_tests)

add_test(NAME cli-smoke
    COMMAND ${CMAKE_COMMAND}
        -DBEAM=$<TARGET_FILE:beam>
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli
        -P ${CMAKE_CURRENT_SOURCE_DIR}/checks/cli_smoke.cmake
)
add_test(NAME installed-sdk
    COMMAND ${Python3_EXECUTABLE} -B -m installed.sdk.consumer.runner
        --cmake ${CMAKE_COMMAND}
        --build-dir ${CMAKE_BINARY_DIR}
        --test-dir ${CMAKE_CURRENT_BINARY_DIR}/sdk
        --source-dir ${CMAKE_CURRENT_SOURCE_DIR}/installed/sdk/consumer
        --doc-example-source ${CMAKE_CURRENT_SOURCE_DIR}/installed/sdk/consumer/first-call
        ${_package_test_args})
add_test(NAME installed-runtime-artifacts
    COMMAND ${Python3_EXECUTABLE} -B -m installed.sdk.runtime_artifacts.runner
        --cmake ${CMAKE_COMMAND}
        --build-dir ${CMAKE_BINARY_DIR}
        --test-dir ${CMAKE_CURRENT_BINARY_DIR}/runtime-artifact-sdk
        --source-dir ${CMAKE_CURRENT_SOURCE_DIR}/installed/sdk/runtime_artifacts)
set_tests_properties(installed-runtime-artifacts PROPERTIES TIMEOUT 120
    LABELS "installed;SDK;shared"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME installed-compile
    COMMAND ${Python3_EXECUTABLE} -B -m installed.cli.compile.runner
        --cmake ${CMAKE_COMMAND}
        --build-dir ${CMAKE_BINARY_DIR}
        --test-dir ${CMAKE_CURRENT_BINARY_DIR}/installed-compile
        --source-dir ${CMAKE_CURRENT_SOURCE_DIR}/installed/cli/compile
        --project-source-dir ${PROJECT_SOURCE_DIR})
set_tests_properties(installed-compile PROPERTIES TIMEOUT 900)
add_test(NAME installed-run
    COMMAND ${Python3_EXECUTABLE} -B -m installed.cli.run.runner
        --cmake ${CMAKE_COMMAND}
        --build-dir ${CMAKE_BINARY_DIR}
        --test-dir ${CMAKE_CURRENT_BINARY_DIR}/installed-run
        --source-dir ${CMAKE_CURRENT_SOURCE_DIR}/installed/cli/run
        --project-source-dir ${PROJECT_SOURCE_DIR}
        --hello-fixture ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/hello_world.erl)
set_tests_properties(installed-run PROPERTIES TIMEOUT 900)
add_test(NAME installed-interactive
    COMMAND ${Python3_EXECUTABLE} -B -m installed.cli.interactive.runner
        --cmake ${CMAKE_COMMAND}
        --build-dir ${CMAKE_BINARY_DIR}
        --test-dir ${CMAKE_CURRENT_BINARY_DIR}/installed-interactive
        --source-dir ${CMAKE_CURRENT_SOURCE_DIR}/installed/cli/interactive
        --project-source-dir ${PROJECT_SOURCE_DIR}
        --hello-fixture ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/hello_world.erl)
set_tests_properties(installed-interactive PROPERTIES TIMEOUT 900)
add_test(NAME native-artifacts-beam
    COMMAND ${CMAKE_COMMAND}
        -DBOOTSTRAP=$<TARGET_FILE:beam_bootstrap>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DRUNTIME=beam
        -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/native-artifacts/beam
        -P ${CMAKE_CURRENT_SOURCE_DIR}/checks/native_artifacts.cmake)
set_tests_properties(native-artifacts-beam PROPERTIES TIMEOUT 60)
add_test(NAME compiler-artifacts
    COMMAND ${Python3_EXECUTABLE} -B "${CMAKE_CURRENT_SOURCE_DIR}/checks/compiler_artifacts.py"
        --cmake ${CMAKE_COMMAND} --source-dir "${CMAKE_SOURCE_DIR}"
        --build-dir "${CMAKE_BINARY_DIR}" --test-dir "${CMAKE_CURRENT_BINARY_DIR}/compiler-artifacts")
set_tests_properties(compiler-artifacts PROPERTIES TIMEOUT 60 LABELS "checks;BEAM")
# Capture actual native target linkage in the production runtime. The boot
# probe links this same library; there is no alternate native runtime.
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/libbeam-native-links-$<CONFIG>.txt"
    CONTENT "$<JOIN:$<TARGET_PROPERTY:libbeam,LINK_LIBRARIES>,\n>\n$<JOIN:$<TARGET_PROPERTY:libbeam,SOURCES>,\n>\n")
set(_native_components --component beam_erl_tracer_nif
    "beam/tracing" $<TARGET_FILE:beam_erl_tracer_nif>)
set(_runtime_units)
foreach(_unit IN ITEMS file network zlib)
    set(_unit_source "${_unit}")
    list(APPEND _runtime_units --unit beam_${_unit}_component "beam/${_unit_source}"
        $<TARGET_FILE:beam_${_unit}_component>)
endforeach()
add_test(NAME native-component-boundary
    COMMAND ${Python3_EXECUTABLE} -B -m checks.native_component_boundary
        --nm ${CMAKE_NM}
        --readelf ${CMAKE_READELF}
        --source-dir ${PROJECT_SOURCE_DIR}
        --compile-commands ${CMAKE_BINARY_DIR}/compile_commands.json
        --test-dir ${CMAKE_CURRENT_BINARY_DIR}/native-boundary
        ${_native_components} ${_runtime_units}
        --test-file-component $<TARGET_FILE:beam_file_fault_component>
        --core-component beam_core_shared_data_native $<TARGET_OBJECTS:beam_core_shared_data_native>
        --core-preload $<TARGET_OBJECTS:beam_core_preload>
        --base-inventory ${CMAKE_BINARY_DIR}/lib/beam/library
        --runtime libbeam
            "${CMAKE_CURRENT_BINARY_DIR}/libbeam-native-links-$<CONFIG>.txt"
            $<TARGET_FILE:libbeam>
    COMMAND_EXPAND_LISTS)
set_tests_properties(native-component-boundary PROPERTIES TIMEOUT 60)
set_tests_properties(api-validation cli-smoke installed-sdk PROPERTIES TIMEOUT 180)
add_test(NAME installed-runtime-beam
    COMMAND ${Python3_EXECUTABLE} -B -m installed.cli.baseline.runner
        --cmake ${CMAKE_COMMAND}
        --build-dir ${CMAKE_BINARY_DIR}
        --test-dir ${CMAKE_CURRENT_BINARY_DIR}/installed-runtime-beam
        --fixture ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/hello_world.erl
        --module-artifacts ${BEAM_INSTALLED_BEAM_FILES})
set_tests_properties(installed-runtime-beam PROPERTIES TIMEOUT 180)
add_test(NAME installed-bare-beam
    COMMAND ${Python3_EXECUTABLE} -B -m installed.runtime.bare.runner
        --cmake ${CMAKE_COMMAND}
        --build-dir ${CMAKE_BINARY_DIR}
        --test-dir ${CMAKE_CURRENT_BINARY_DIR}/installed-bare-beam
        --source-dir ${CMAKE_CURRENT_SOURCE_DIR}/installed/runtime/bare)
set_tests_properties(installed-bare-beam PROPERTIES TIMEOUT 300)

add_test(NAME installed-returning
    COMMAND ${Python3_EXECUTABLE} -B -m installed.boot.returning.runner
        --cmake ${CMAKE_COMMAND}
        --build-dir ${CMAKE_BINARY_DIR}
        --test-dir ${CMAKE_CURRENT_BINARY_DIR}/installed-returning
        --source-dir ${CMAKE_CURRENT_SOURCE_DIR}/installed/boot/returning
        --project-source-dir ${PROJECT_SOURCE_DIR})
set_tests_properties(installed-returning PROPERTIES TIMEOUT 900)

add_test(NAME installed-deployment
    COMMAND ${Python3_EXECUTABLE} -B -m installed.sdk.deployment.runner
        --cmake ${CMAKE_COMMAND} --build-dir ${CMAKE_BINARY_DIR}
        --test-dir ${CMAKE_CURRENT_BINARY_DIR}/installed-deployment
        --source-dir ${CMAKE_CURRENT_SOURCE_DIR}/installed/sdk/deployment)
set_tests_properties(installed-deployment PROPERTIES TIMEOUT 240)

include("${CMAKE_CURRENT_SOURCE_DIR}/installed/runtime/components/file_faults.cmake")

add_test(NAME installed-components
    COMMAND ${Python3_EXECUTABLE} -B -m installed.runtime.components.runner
        --cmake ${CMAKE_COMMAND} --build-dir ${CMAKE_BINARY_DIR}
        --test-dir ${CMAKE_CURRENT_BINARY_DIR}/installed-components
        --source-dir ${CMAKE_CURRENT_SOURCE_DIR}/installed/runtime/components
        --file-fault-component $<TARGET_FILE:beam_file_fault_component>)
set_tests_properties(installed-components PROPERTIES TIMEOUT 300)

add_test(NAME installed-process-owned
    COMMAND ${Python3_EXECUTABLE} -B -m installed.runtime.process_owned.runner
        --cmake ${CMAKE_COMMAND} --build-dir ${CMAKE_BINARY_DIR}
        --test-dir ${CMAKE_CURRENT_BINARY_DIR}/installed-process-owned
        --source-dir ${CMAKE_CURRENT_SOURCE_DIR}/installed/runtime/process_owned)
set_tests_properties(installed-process-owned PROPERTIES TIMEOUT 180)

add_executable(beam_test_etf installed/boot/returning/etf.c installed/boot/returning/etf_test.c)
target_compile_features(beam_test_etf PRIVATE c_std_11)
target_compile_options(beam_test_etf PRIVATE -Wall -Wextra -Werror)
add_test(NAME test-support
    COMMAND ${Python3_EXECUTABLE} -B -m support.runner
        --etf-check $<TARGET_FILE:beam_test_etf>
        --test-dir ${CMAKE_CURRENT_BINARY_DIR}/support)
set_tests_properties(test-support PROPERTIES
    TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

# Labels select focused development checks and complete integration gates.
set_tests_properties(api-validation PROPERTIES LABELS "checks;shared")
set_tests_properties(cli-smoke PROPERTIES LABELS "checks;CLI;shared")
set_tests_properties(native-artifacts-beam PROPERTIES LABELS "checks;BEAM")
set_tests_properties(native-component-boundary PROPERTIES LABELS "checks;shared")
set_tests_properties(installed-sdk PROPERTIES LABELS "installed;SDK;shared")
set_tests_properties(installed-compile PROPERTIES LABELS "installed;CLI;shared")
set_tests_properties(installed-run PROPERTIES LABELS "installed;CLI;BEAM;network")
set_tests_properties(installed-interactive PROPERTIES LABELS "installed;CLI;BEAM;terminal;network")
set_tests_properties(installed-runtime-beam PROPERTIES LABELS "installed;CLI;BEAM;terminal;network")
set_tests_properties(installed-bare-beam PROPERTIES LABELS "installed;SDK;shared;BEAM;terminal")
set_tests_properties(installed-returning PROPERTIES LABELS "installed;SDK;BEAM;network")
set_tests_properties(installed-deployment PROPERTIES LABELS "installed;SDK;shared;network")
set_tests_properties(installed-components PROPERTIES LABELS "installed;SDK;shared;network")
set_tests_properties(installed-process-owned PROPERTIES LABELS "installed;SDK;shared")
set_tests_properties(test-support PROPERTIES LABELS "checks;shared")
set_tests_properties(
    native-component-boundary
    installed-sdk
    installed-compile
    installed-run
    installed-interactive
    installed-runtime-beam
    installed-bare-beam
    installed-returning
    installed-deployment
    installed-components
    installed-process-owned
    test-support
    PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

add_test(NAME installed-application
    COMMAND ${Python3_EXECUTABLE} -B -m installed.cli.application.runner
        --build-dir ${CMAKE_BINARY_DIR}
        --test-dir ${CMAKE_CURRENT_BINARY_DIR}/installed-application)
set_tests_properties(installed-application PROPERTIES TIMEOUT 180
    LABELS "installed;CLI;BEAM" WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

if(BEAM_BUNDLE_PACKAGE_TOOL)
add_test(NAME installed-pkg
    COMMAND ${Python3_EXECUTABLE} -B -m installed.cli.pkg.runner
        --cmake ${CMAKE_COMMAND}
        --build-dir ${CMAKE_BINARY_DIR}
        --test-dir ${CMAKE_CURRENT_BINARY_DIR}/installed-pkg)
set_tests_properties(installed-pkg PROPERTIES TIMEOUT 180
    LABELS "installed;CLI;BEAM"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

add_test(NAME installed-nifs
    COMMAND ${Python3_EXECUTABLE} -B -m installed.runtime.nifs.runner
        --cmake ${CMAKE_COMMAND} --build-dir ${CMAKE_BINARY_DIR}
        --test-dir ${CMAKE_CURRENT_BINARY_DIR}/installed-nifs
        --source-dir ${CMAKE_CURRENT_SOURCE_DIR}/installed/runtime/nifs
        ${_package_test_args})
set_tests_properties(installed-nifs PROPERTIES TIMEOUT 300
    LABELS "installed;SDK;BEAM" WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

# Reuse the public bare/native fixtures; modern component/project workflows
# remain shared-only and retain their existing acceptance owners.
foreach(_fixture IN ITEMS bare nifs)
    add_test(NAME installed-static-${_fixture}
        COMMAND ${Python3_EXECUTABLE} -B -m installed.runtime.${_fixture}.runner
            --cmake ${CMAKE_COMMAND} --build-dir ${CMAKE_BINARY_DIR}
            --test-dir ${CMAKE_CURRENT_BINARY_DIR}/installed-static-${_fixture}
            --source-dir ${CMAKE_CURRENT_SOURCE_DIR}/installed/runtime/${_fixture}
            --linkage static)
    set_tests_properties(installed-static-${_fixture} PROPERTIES TIMEOUT 300
        LABELS "installed;SDK;static" WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endforeach()
