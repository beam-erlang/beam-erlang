# Ordinary modern library code is supplied explicitly, outside the VM.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_ordinary_base beam beam_format beam_time beam_atomics beam_counters beam_persistent_term)
set(_direct_library beam_binary beam_lists beam_maps beam_records beam_string beam_unicode
    beam_math beam_ddll beam_debugger)
foreach(_module IN LISTS _direct_library)
    if(_module IN_LIST BEAM_PROJECTED_LIBRARY_MODULES)
        message(FATAL_ERROR "Direct library module also has a projected producer: ${_module}")
    endif()
endforeach()
set(BEAM_BASE_MODULES ${_ordinary_base} beam_binary beam_lists beam_maps beam_records
    beam_ets beam_string beam_unicode beam_runtime_unicode_util beam_math beam_ddll beam_debugger)
set(BEAM_BASE_SOURCES)
set(BEAM_BASE_SEED_FILES)
set(_direct_library_sources)
foreach(_module IN LISTS _ordinary_base _direct_library)
    list(APPEND BEAM_BASE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${_module}.erl")
endforeach()
foreach(_module IN LISTS _direct_library)
    list(APPEND _direct_library_sources "${CMAKE_CURRENT_SOURCE_DIR}/${_module}.erl")
endforeach()
foreach(_module IN LISTS BEAM_BASE_MODULES)
    list(APPEND BEAM_BASE_SEED_FILES "${CMAKE_CURRENT_SOURCE_DIR}/ebin/${_module}.beam")
endforeach()
foreach(_value IN ITEMS MODULES SOURCES SEED_FILES)
    set(BEAM_BASE_${_value} "${BEAM_BASE_${_value}}" PARENT_SCOPE)
endforeach()

set(_base_check_stamp "${CMAKE_BINARY_DIR}/base-verification/verified.stamp")
set(_base_ebin "${CMAKE_BINARY_DIR}/lib/beam/library/ebin")
set(BEAM_BASE_OUTPUT_DIRECTORY "${_base_ebin}" PARENT_SCOPE)
set(_direct_library_headers "${CMAKE_CURRENT_SOURCE_DIR}/include/swar_ascii.hrl")
set(_base_attest "${CMAKE_SOURCE_DIR}/src/beam/runtime_support/attest.py")
set(_base_receipt "${CMAKE_BINARY_DIR}/runtime-support/attestations/beam_direct_library_modules.json")
set(_base_projected_inputs)
foreach(_module IN LISTS BEAM_PROJECTED_LIBRARY_MODULES)
    list(APPEND _base_projected_inputs "${BEAM_PROJECTED_LIBRARY_OUTPUT_DIRECTORY}/${_module}.beam")
endforeach()
set(_base_outputs)
foreach(_module IN LISTS BEAM_BASE_MODULES)
    list(APPEND _base_outputs "${_base_ebin}/${_module}.beam")
endforeach()
add_custom_command(OUTPUT "${_base_check_stamp}"
    BYPRODUCTS ${_base_outputs} "${_base_receipt}"
    COMMAND ${CMAKE_COMMAND}
        -DBOOTSTRAP=$<TARGET_FILE:beam_bootstrap> -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        "-DMODULES=${BEAM_BASE_MODULES}" "-DSOURCES=${BEAM_BASE_SOURCES}"
        "-DPROJECTED_MODULES=${BEAM_PROJECTED_LIBRARY_MODULES}"
        "-DPROJECTED_DIR=${BEAM_PROJECTED_LIBRARY_OUTPUT_DIRECTORY}"
        "-DDIRECT_SOURCES=${_direct_library_sources}" "-DHEADERS=${_direct_library_headers}"
        "-DINCLUDE_DIRECTORIES=${CMAKE_CURRENT_SOURCE_DIR}/include"
        "-DPYTHON=${Python3_EXECUTABLE}" "-DATTEST=${_base_attest}"
        "-DSDK=${CMAKE_BINARY_DIR}/sdk.toml" "-DATTESTATION=${_base_receipt}"
        -DOUTPUT_DIR=${_base_ebin} -DVERIFY=ON
        -P ${CMAKE_SOURCE_DIR}/cmake/RefreshBaseArtifacts.cmake
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/base-verification"
    COMMAND ${CMAKE_COMMAND} -E touch "${_base_check_stamp}"
    DEPENDS beam_bootstrap beam_public_library_modules
        ${BEAM_BASE_SOURCES} ${BEAM_BASE_SEED_FILES} ${_direct_library_headers}
        ${_base_projected_inputs}
        "${_base_attest}" "${CMAKE_BINARY_DIR}/sdk.toml"
        "${CMAKE_SOURCE_DIR}/src/beam/compiler/seeds/manifest.json"
        "${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/beam_native_contracts.def"
        "${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/beam_native_instructions.def"
        "${CMAKE_SOURCE_DIR}/cmake/RefreshBaseArtifacts.cmake"
        "${CMAKE_SOURCE_DIR}/src/beam/support/include/beam_time_conversion.hrl"
    COMMENT "Compiling and verifying ordinary base bytecode" VERBATIM)
add_custom_target(beam_verify_base_artifacts ALL DEPENDS "${_base_check_stamp}")

beam_add_bytecode_inventory(beam_base_inventory NAME base
    MODULES ${BEAM_BASE_MODULES} INPUT_DIRECTORY "${_base_ebin}"
    INSTALL_ROOT lib/beam/library DEPENDS beam_verify_base_artifacts)

add_custom_target(beam_refresh_base_artifacts
    COMMAND ${CMAKE_COMMAND}
        -DBOOTSTRAP=$<TARGET_FILE:beam_bootstrap> -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        "-DMODULES=${BEAM_BASE_MODULES}" "-DSOURCES=${BEAM_BASE_SOURCES}"
        "-DPROJECTED_MODULES=${BEAM_PROJECTED_LIBRARY_MODULES}"
        "-DPROJECTED_DIR=${BEAM_PROJECTED_LIBRARY_OUTPUT_DIRECTORY}"
        "-DDIRECT_SOURCES=${_direct_library_sources}" "-DHEADERS=${_direct_library_headers}"
        "-DINCLUDE_DIRECTORIES=${CMAKE_CURRENT_SOURCE_DIR}/include"
        "-DPYTHON=${Python3_EXECUTABLE}" "-DATTEST=${_base_attest}"
        "-DSDK=${CMAKE_BINARY_DIR}/sdk.toml"
        "-DATTESTATION=${CMAKE_BINARY_DIR}/base-artifacts/beam_direct_library_modules.json"
        -DOUTPUT_DIR=${CMAKE_BINARY_DIR}/base-artifacts
        -P ${CMAKE_SOURCE_DIR}/cmake/RefreshBaseArtifacts.cmake
    DEPENDS beam_bootstrap beam_public_library_modules
    COMMENT "Refreshing base component artifact candidates" VERBATIM)
