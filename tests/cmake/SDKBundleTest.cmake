cmake_minimum_required(VERSION 3.25)
# Installed-only SDK assembly and selection acceptance. The existing SDK
# consumer driver owns C/C++ linkage, native extensions, staging and isolated
# deployment; run it for every configuration present in the assembled package.
foreach(_required SOURCE_DIR BEAM_SDK_INPUT_PREFIXES TEST_ROOT C_COMPILER
        CXX_COMPILER MAKE_PROGRAM TARGET_SYSTEM)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "Missing SDK bundle test input: ${_required}")
    endif()
endforeach()
get_filename_component(_workspace "${SOURCE_DIR}/.." ABSOLUTE)
cmake_path(ABSOLUTE_PATH TEST_ROOT NORMALIZE OUTPUT_VARIABLE TEST_ROOT)
get_filename_component(_test_parent "${TEST_ROOT}" DIRECTORY)
get_filename_component(_test_name "${TEST_ROOT}" NAME)
cmake_path(SET _expected_parent NORMALIZE "${_workspace}/tmp")
if(TARGET_SYSTEM STREQUAL "Windows")
    string(TOLOWER "${_test_parent}" _test_parent)
    string(TOLOWER "${_expected_parent}" _expected_parent)
endif()
if(NOT _test_parent STREQUAL _expected_parent OR NOT _test_name MATCHES "^beam-sdk-bundle-[0-9a-f]+$")
    message(FATAL_ERROR "Refusing unsafe SDK bundle test directory: ${TEST_ROOT}")
endif()
# Validate physical locations before removing disposable output. An explicit
# input may be reached through a symlink below the apparently separate tmp path.
if(EXISTS "${TEST_ROOT}")
    file(REAL_PATH "${TEST_ROOT}" _physical_test_root)
else()
    file(REAL_PATH "${_expected_parent}" _physical_test_parent)
    set(_physical_test_root "${_physical_test_parent}/${_test_name}")
endif()
if(TARGET_SYSTEM STREQUAL "Windows")
    string(TOLOWER "${_physical_test_root}" _physical_test_root)
endif()
set(_expected_available)
foreach(_input IN LISTS BEAM_SDK_INPUT_PREFIXES)
    if(NOT IS_DIRECTORY "${_input}" OR NOT EXISTS "${_input}/manifest.json")
        message(FATAL_ERROR "Missing installed SDK input: ${_input}")
    endif()
    file(REAL_PATH "${_input}" _physical_input)
    if(TARGET_SYSTEM STREQUAL "Windows")
        string(TOLOWER "${_physical_input}" _physical_input)
    endif()
    cmake_path(IS_PREFIX _physical_input "${_physical_test_root}" NORMALIZE _input_contains_test)
    cmake_path(IS_PREFIX _physical_test_root "${_physical_input}" NORMALIZE _test_contains_input)
    if(_input_contains_test OR _test_contains_input)
        message(FATAL_ERROR "SDK input overlaps disposable test directory: ${_input}")
    endif()
    file(READ "${_input}/manifest.json" _input_manifest)
    string(JSON _input_variant_count LENGTH "${_input_manifest}" variants)
    math(EXPR _input_last_variant "${_input_variant_count} - 1")
    foreach(_index RANGE 0 ${_input_last_variant})
        string(JSON _flavor GET "${_input_manifest}" variants ${_index} flavor)
        string(JSON _type GET "${_input_manifest}" variants ${_index} build_type)
        list(APPEND _expected_available "${_flavor}/${_type}")
    endforeach()
endforeach()
list(REMOVE_DUPLICATES _expected_available)
list(SORT _expected_available)
if(NOT BINARY_DIR)
    set(BINARY_DIR "${SOURCE_DIR}/build")
endif()
file(MAKE_DIRECTORY "${_workspace}/tmp")
file(LOCK "${TEST_ROOT}.lock" GUARD PROCESS TIMEOUT 10)
file(REMOVE_RECURSE "${TEST_ROOT}")
set(_payload "${TEST_ROOT}/payload")
file(MAKE_DIRECTORY "${_payload}")
list(GET BEAM_SDK_INPUT_PREFIXES 0 _first_input)
if(NOT ASSEMBLY_SCRIPT)
    set(ASSEMBLY_SCRIPT "${_first_input}/lib/cmake/BEAM/AssembleSDK.cmake")
endif()
if(NOT EXISTS "${ASSEMBLY_SCRIPT}")
    message(FATAL_ERROR "Missing installed SDK assembler: ${ASSEMBLY_SCRIPT}")
endif()

function(bundle_command label expected_status)
    # PARSE_ARGV preserves semicolons inside -D arguments, including the input
    # prefix list and Windows runtime search directories.
    cmake_parse_arguments(PARSE_ARGV 2 run "" "" "COMMAND")
    execute_process(COMMAND ${run_COMMAND} WORKING_DIRECTORY "${_payload}"
        RESULT_VARIABLE _status OUTPUT_VARIABLE _out ERROR_VARIABLE _err TIMEOUT 600)
    file(WRITE "${TEST_ROOT}/${label}.log" "${_out}${_err}")
    if(expected_status STREQUAL "failure")
        if("${_status}" STREQUAL "0")
            message(FATAL_ERROR "${label}: unexpectedly succeeded. Evidence: ${TEST_ROOT}")
        endif()
    elseif(NOT "${_status}" STREQUAL "${expected_status}")
        message(FATAL_ERROR "${label}: expected ${expected_status}, got ${_status}. Evidence: ${TEST_ROOT}\n${_out}${_err}")
    endif()
    set(BUNDLE_COMMAND_OUTPUT "${_out}${_err}" PARENT_SCOPE)
endfunction()

function(bundle_assemble label expected_status inputs destination)
    bundle_command("${label}" "${expected_status}" COMMAND "${CMAKE_COMMAND}"
        "-DBEAM_SDK_INPUT_PREFIXES=${inputs}"
        "-DBEAM_SDK_OUTPUT_PREFIX=${destination}" -P "${ASSEMBLY_SCRIPT}")
    set(BUNDLE_COMMAND_OUTPUT "${BUNDLE_COMMAND_OUTPUT}" PARENT_SCOPE)
endfunction()

function(bundle_diagnostic label pattern)
    if(NOT BUNDLE_COMMAND_OUTPUT MATCHES "${pattern}")
        message(FATAL_ERROR "${label}: expected diagnostic '${pattern}'. Evidence: ${TEST_ROOT}\n${BUNDLE_COMMAND_OUTPUT}")
    endif()
endfunction()

set(_original_sdk "${_payload}/SDK original λ")
set(_sdk "${_payload}/SDK moved λ")
bundle_assemble(assemble-full 0 "${BEAM_SDK_INPUT_PREFIXES}" "${_original_sdk}")
file(RENAME "${_original_sdk}" "${_sdk}")
file(READ "${_sdk}/manifest.json" _manifest)
string(JSON _variant_count LENGTH "${_manifest}" variants)
math(EXPR _last_variant "${_variant_count} - 1")
if(_variant_count LESS 1 OR _variant_count GREATER 4)
    message(FATAL_ERROR "Assembled SDK contains ${_variant_count} configurations")
endif()
foreach(_field version erts_version upstream_commit system_name architecture host_triple deployment_target)
    string(JSON _${_field} GET "${_manifest}" ${_field})
endforeach()

set(_probe_source "${_payload}/package probe source")
file(MAKE_DIRECTORY "${_probe_source}")
file(WRITE "${_probe_source}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.25)
project(sdk_bundle_probe LANGUAGES C CXX)
find_package(BEAM ${EXPECTED_VERSION} EXACT CONFIG REQUIRED)
if(NOT TARGET BEAM::Runtime OR TARGET BEAM::RuntimeStatic)
    message(FATAL_ERROR "SDK must expose Runtime without the removed RuntimeStatic interface")
endif()
if(NOT "${BEAM_SDK_FLAVOR}/${BEAM_SDK_BUILD_TYPE}" STREQUAL EXPECTED_VARIANT)
    message(FATAL_ERROR "Unexpected selection: ${BEAM_SDK_FLAVOR}/${BEAM_SDK_BUILD_TYPE}; expected ${EXPECTED_VARIANT}")
endif()
if(EXPECT_EMPTY_BUILD_TYPE AND CMAKE_BUILD_TYPE)
    message(FATAL_ERROR "SDK changed the consumer build type")
endif()
if(SECOND_FLAVOR)
    set(BEAM_RUNTIME_FLAVOR "${SECOND_FLAVOR}")
endif()
if(SECOND_SDK)
    set(BEAM_DIR "${SECOND_SDK}/lib/cmake/BEAM")
endif()
find_package(BEAM ${EXPECTED_VERSION} EXACT CONFIG REQUIRED)
]=])
set(_probe_args -G Ninja "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}"
    "-DCMAKE_C_COMPILER=${C_COMPILER}" "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}"
    -DCMAKE_FIND_USE_PACKAGE_REGISTRY=FALSE -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=FALSE
    "-DEXPECTED_VERSION=${_version}")
if(DEPLOYMENT_TARGET)
    list(APPEND _probe_args "-DCMAKE_OSX_DEPLOYMENT_TARGET=${DEPLOYMENT_TARGET}")
endif()
if(OSX_SYSROOT)
    list(APPEND _probe_args "-DCMAKE_OSX_SYSROOT=${OSX_SYSROOT}")
endif()

set(_available)
foreach(_index RANGE 0 ${_last_variant})
    string(JSON _flavor GET "${_manifest}" variants ${_index} flavor)
    string(JSON _build_type GET "${_manifest}" variants ${_index} build_type)
    list(APPEND _available "${_flavor}/${_build_type}")
endforeach()
list(SORT _available)
if(NOT _available STREQUAL _expected_available)
    message(FATAL_ERROR "Assembled SDK lost or added configurations. Expected ${_expected_available}; got ${_available}")
endif()
foreach(_index RANGE 0 ${_last_variant})
    foreach(_field flavor build_type build_id msvc_runtime_library)
        string(JSON _${_field} GET "${_manifest}" variants ${_index} ${_field})
    endforeach()
    if(_flavor STREQUAL "emu" AND _build_type STREQUAL "Release")
        set(_default_msvc_runtime "${_msvc_runtime_library}")
    endif()
    string(SHA256 _consumer_id "${TEST_ROOT}/${_flavor}/${_build_type}")
    string(SUBSTRING "${_consumer_id}" 0 12 _consumer_id)
    bundle_command("consumer-${_flavor}-${_build_type}" 0 COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${SOURCE_DIR}" "-DBINARY_DIR=${BINARY_DIR}"
        -DINSTALL_BINDIR=bin -DINSTALL_LIBDIR=lib -DINSTALL_INCLUDEDIR=include -DINSTALL_DATADIR=share
        "-DEXPECTED_VERSION=${_version}" "-DEXPECTED_FLAVOR=${_flavor}"
        "-DEXPECTED_BUILD_TYPE=${_build_type}" "-DEXPECTED_BUILD_ID=${_build_id}"
        "-DEXPECTED_HOST_TRIPLE=${_host_triple}" "-DEXPECTED_ARCHITECTURE=${_architecture}"
        "-DEXPECTED_UPSTREAM_COMMIT=${_upstream_commit}" "-DTARGET_SYSTEM=${TARGET_SYSTEM}"
        "-DEXPECTED_MSVC_RUNTIME_LIBRARY=${_msvc_runtime_library}"
        "-DC_COMPILER=${C_COMPILER}" "-DCXX_COMPILER=${CXX_COMPILER}"
        "-DMAKE_PROGRAM=${MAKE_PROGRAM}" "-DDEPLOYMENT_TARGET=${DEPLOYMENT_TARGET}"
        "-DOSX_SYSROOT=${OSX_SYSROOT}" "-DWINDOWS_RUNTIME_DIRS=${WINDOWS_RUNTIME_DIRS}"
        "-DSDK_INPUT_PREFIX=${_sdk}" "-DTEST_ROOT=${_workspace}/tmp/beam-sdk-${_consumer_id}"
        -P "${SOURCE_DIR}/tests/cmake/SDKTest.cmake")
endforeach()

# A default lookup must select EMU Release, without setting compiler flags.
if("emu/Release" IN_LIST _available)
    set(_default_runtime_args)
    if(TARGET_SYSTEM STREQUAL "Windows")
        list(APPEND _default_runtime_args "-DCMAKE_MSVC_RUNTIME_LIBRARY=${_default_msvc_runtime}")
    endif()
    bundle_command(default-selection 0 COMMAND "${CMAKE_COMMAND}" -S "${_probe_source}"
        -B "${_payload}/default consumer" ${_probe_args} -DCMAKE_BUILD_TYPE=
        -DEXPECT_EMPTY_BUILD_TYPE=TRUE -DEXPECTED_VARIANT=emu/Release
        ${_default_runtime_args}
        "-DBEAM_DIR=${_sdk}/lib/cmake/BEAM")
endif()
string(JSON _first_flavor GET "${_manifest}" variants 0 flavor)
string(JSON _first_type GET "${_manifest}" variants 0 build_type)
string(JSON _first_msvc_runtime GET "${_manifest}" variants 0 msvc_runtime_library)
set(_selected_args "-DBEAM_RUNTIME_FLAVOR=${_first_flavor}"
    "-DCMAKE_BUILD_TYPE=${_first_type}" "-DEXPECTED_VARIANT=${_first_flavor}/${_first_type}")
if(TARGET_SYSTEM STREQUAL "Windows")
    list(APPEND _selected_args "-DCMAKE_MSVC_RUNTIME_LIBRARY=${_first_msvc_runtime}")
endif()
bundle_command(repeated-selection 0 COMMAND "${CMAKE_COMMAND}" -S "${_probe_source}"
    -B "${_payload}/repeated consumer" ${_probe_args} ${_selected_args}
    "-DBEAM_DIR=${_sdk}/lib/cmake/BEAM")
bundle_command(reject-flavor failure COMMAND "${CMAKE_COMMAND}" -S "${_probe_source}"
    -B "${_payload}/invalid flavor" ${_probe_args} ${_selected_args}
    -DBEAM_RUNTIME_FLAVOR=unsupported "-DBEAM_DIR=${_sdk}/lib/cmake/BEAM")
bundle_command(reject-configuration failure COMMAND "${CMAKE_COMMAND}" -S "${_probe_source}"
    -B "${_payload}/invalid configuration" ${_probe_args} ${_selected_args}
    -DCMAKE_BUILD_TYPE=RelWithDebInfo "-DBEAM_DIR=${_sdk}/lib/cmake/BEAM")

# Assemble a genuine one-configuration package and check absent choices.
set(_partial "${_payload}/partial SDK λ")
bundle_assemble(assemble-partial 0 "${_first_input}" "${_partial}")
file(READ "${_partial}/manifest.json" _partial_manifest)
string(JSON _partial_count LENGTH "${_partial_manifest}" variants)
string(JSON _partial_flavor GET "${_partial_manifest}" variants 0 flavor)
string(JSON _partial_type GET "${_partial_manifest}" variants 0 build_type)
string(JSON _partial_msvc_runtime GET "${_partial_manifest}" variants 0 msvc_runtime_library)
set(_partial_selected_args "-DBEAM_RUNTIME_FLAVOR=${_partial_flavor}"
    "-DCMAKE_BUILD_TYPE=${_partial_type}" "-DEXPECTED_VARIANT=${_partial_flavor}/${_partial_type}")
if(TARGET_SYSTEM STREQUAL "Windows")
    list(APPEND _partial_selected_args "-DCMAKE_MSVC_RUNTIME_LIBRARY=${_partial_msvc_runtime}")
endif()
if(_partial_count EQUAL 1)
    bundle_command(partial-selection 0 COMMAND "${CMAKE_COMMAND}" -S "${_probe_source}"
        -B "${_payload}/partial consumer" ${_probe_args}
        ${_partial_selected_args}
        "-DBEAM_DIR=${_partial}/lib/cmake/BEAM")
    if(_partial_flavor STREQUAL "emu")
        set(_other_flavor jit)
    else()
        set(_other_flavor emu)
    endif()
    if(_partial_type STREQUAL "Release")
        set(_other_type Debug)
    else()
        set(_other_type Release)
    endif()
    bundle_command(reject-missing-flavor failure COMMAND "${CMAKE_COMMAND}" -S "${_probe_source}"
        -B "${_payload}/missing flavor" ${_probe_args} "-DCMAKE_BUILD_TYPE=${_partial_type}"
        "-DBEAM_RUNTIME_FLAVOR=${_other_flavor}" "-DBEAM_DIR=${_partial}/lib/cmake/BEAM")
    bundle_diagnostic(reject-missing-flavor "unavailable")
    bundle_command(reject-missing-configuration failure COMMAND "${CMAKE_COMMAND}" -S "${_probe_source}"
        -B "${_payload}/missing configuration" ${_probe_args} "-DCMAKE_BUILD_TYPE=${_other_type}"
        "-DBEAM_RUNTIME_FLAVOR=${_partial_flavor}" "-DBEAM_DIR=${_partial}/lib/cmake/BEAM")
    bundle_diagnostic(reject-missing-configuration "unavailable")
endif()
bundle_command(reject-conflicting-sdk failure COMMAND "${CMAKE_COMMAND}" -S "${_probe_source}"
    -B "${_payload}/conflicting SDK" ${_probe_args} ${_partial_selected_args}
    "-DBEAM_DIR=${_sdk}/lib/cmake/BEAM" "-DSECOND_SDK=${_partial}")
bundle_diagnostic(reject-conflicting-sdk "Conflicting")
if("emu/${_first_type}" IN_LIST _available AND "jit/${_first_type}" IN_LIST _available)
    if(_first_flavor STREQUAL "emu")
        set(_second_flavor jit)
    else()
        set(_second_flavor emu)
    endif()
    bundle_command(reject-conflicting-flavor failure COMMAND "${CMAKE_COMMAND}" -S "${_probe_source}"
        -B "${_payload}/conflicting flavor" ${_probe_args} ${_selected_args}
        "-DBEAM_DIR=${_sdk}/lib/cmake/BEAM" "-DSECOND_FLAVOR=${_second_flavor}")
    bundle_diagnostic(reject-conflicting-flavor "Conflicting")
endif()

bundle_assemble(reject-duplicate failure "${_first_input};${_first_input}" "${_payload}/duplicate output")
bundle_diagnostic(reject-duplicate "Duplicate SDK runtime configuration")
bundle_assemble(reject-too-many failure
    "${_first_input};${_first_input};${_first_input};${_first_input};${_first_input}"
    "${_payload}/too many output")
set(_nonempty "${_payload}/nonempty output")
file(MAKE_DIRECTORY "${_nonempty}")
file(WRITE "${_nonempty}/sentinel" "preserve existing output\n")
bundle_assemble(reject-nonempty failure "${BEAM_SDK_INPUT_PREFIXES}" "${_nonempty}")
bundle_diagnostic(reject-nonempty "must be empty")
file(READ "${_nonempty}/sentinel" _sentinel)
if(NOT _sentinel STREQUAL "preserve existing output\n")
    message(FATAL_ERROR "Assembler changed a preexisting output directory")
endif()

# Mutate only a private copy, never an installed input. Keep one copy so the
# rejection campaign does not duplicate large runtime archives for each case.
set(_mutant "${_payload}/mutated input λ")
file(MAKE_DIRECTORY "${_mutant}")
file(COPY "${_first_input}/" DESTINATION "${_mutant}")
file(READ "${_mutant}/manifest.json" _original_manifest)
bundle_assemble(reject-overlap failure "${_mutant}" "${_mutant}/nested output")
bundle_diagnostic(reject-overlap "overlap")
string(JSON _archive GET "${_original_manifest}" variants 0 archive)
file(RENAME "${_mutant}/${_archive}" "${_payload}/saved archive")
bundle_assemble(reject-missing-archive failure "${_mutant}" "${_payload}/missing archive output")
bundle_diagnostic(reject-missing-archive "Missing SDK asset")
file(RENAME "${_payload}/saved archive" "${_mutant}/${_archive}")

list(LENGTH BEAM_SDK_INPUT_PREFIXES _input_count)
if(_input_count GREATER 1)
    list(GET BEAM_SDK_INPUT_PREFIXES 1 _second_input)
    foreach(_field source_id version system_name architecture toolchain_id)
        if(_field STREQUAL "source_id" OR _field STREQUAL "toolchain_id")
            set(_different "0000000000000000000000000000000000000000000000000000000000000000")
        elseif(_field STREQUAL "version")
            set(_different "0.0")
        else()
            set(_different "unsupported")
        endif()
        string(JSON _changed_manifest SET "${_original_manifest}" ${_field} "\"${_different}\"")
        file(WRITE "${_mutant}/manifest.json" "${_changed_manifest}\n")
        bundle_assemble("reject-mixed-${_field}" failure "${_mutant};${_second_input}"
            "${_payload}/mixed ${_field} output")
        bundle_diagnostic("reject-mixed-${_field}" "[Ii]ncompatib|[Mm]ismatch")
        bundle_diagnostic("reject-mixed-${_field}" "${_field}")
    endforeach()
    file(WRITE "${_mutant}/manifest.json" "${_original_manifest}")

    string(JSON _common_count LENGTH "${_original_manifest}" common_files)
    math(EXPR _last_common "${_common_count} - 1")
    set(_header_index "")
    set(_preload_index "")
    foreach(_index RANGE 0 ${_last_common})
        string(JSON _path GET "${_original_manifest}" common_files ${_index} path)
        if(_path MATCHES "/beam[.]h$")
            set(_header_index ${_index})
        elseif(_preload_index STREQUAL "" AND _path MATCHES "/ebin/[^/]+[.]beam$")
            set(_preload_index ${_index})
        endif()
    endforeach()
    foreach(_asset header preload)
        if("${_${_asset}_index}" STREQUAL "")
            message(FATAL_ERROR "Manifest lacks common ${_asset} assets")
        endif()
        set(_index "${_${_asset}_index}")
        string(JSON _path GET "${_original_manifest}" common_files ${_index} path)
        file(COPY_FILE "${_mutant}/${_path}" "${_payload}/saved common file")
        file(APPEND "${_mutant}/${_path}" "\nchanged common asset\n")
        file(SHA256 "${_mutant}/${_path}" _changed_hash)
        string(JSON _changed_manifest SET "${_original_manifest}" common_files ${_index}
            sha256 "\"${_changed_hash}\"")
        file(WRITE "${_mutant}/manifest.json" "${_changed_manifest}\n")
        bundle_assemble("reject-unequal-${_asset}" failure "${_mutant};${_second_input}"
            "${_payload}/unequal ${_asset} output")
        bundle_diagnostic("reject-unequal-${_asset}" "Unequal common SDK assets")
        file(COPY_FILE "${_payload}/saved common file" "${_mutant}/${_path}")
        file(WRITE "${_mutant}/manifest.json" "${_original_manifest}")
    endforeach()
endif()
message(STATUS "SDK bundle acceptance passed for ${_variant_count} configurations: ${_available}")
file(REMOVE_RECURSE "${_payload}")
