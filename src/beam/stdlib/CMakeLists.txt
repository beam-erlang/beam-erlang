# Ordinary standard-library modules have no BIF-implemented functions.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(BEAM_STDLIB_MODULES array dict graph gb_sets gb_trees orddict ordsets
    proplists queue sets sofs digraph digraph_utils)
set(BEAM_STDLIB_SOURCES)
set(_outputs)
set(_ebin "${CMAKE_BINARY_DIR}/lib/beam/stdlib/ebin")
foreach(_module IN LISTS BEAM_STDLIB_MODULES)
    list(APPEND BEAM_STDLIB_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${_module}.erl")
    list(APPEND _outputs "${_ebin}/${_module}.beam")
endforeach()
set(_resources "${CMAKE_CURRENT_SOURCE_DIR}/doc/sofs.md")
set(_attest "${CMAKE_SOURCE_DIR}/src/beam/runtime_support/attest.py")
set(_attestation_dir "${CMAKE_BINARY_DIR}/runtime-support/attestations")
set(_receipt "${_attestation_dir}/beam_stdlib_modules.json")
add_custom_command(OUTPUT ${_outputs} "${_receipt}"
    COMMAND ${CMAKE_COMMAND}
        -DBOOTSTRAP=$<TARGET_FILE:beam_bootstrap> "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
        "-DSOURCES=${BEAM_STDLIB_SOURCES}" "-DRESOURCES=${_resources}"
        "-DPYTHON=${Python3_EXECUTABLE}" "-DATTEST=${_attest}"
        "-DSDK=${CMAKE_BINARY_DIR}/sdk.toml" "-DATTESTATION_DIR=${_attestation_dir}"
        "-DOUTPUT_DIR=${_ebin}" -P "${CMAKE_SOURCE_DIR}/cmake/CompileStdlib.cmake"
    DEPENDS beam_bootstrap ${BEAM_STDLIB_SOURCES} ${_resources} "${_attest}"
        "${CMAKE_BINARY_DIR}/sdk.toml"
        "${CMAKE_SOURCE_DIR}/src/beam/compiler/seeds/manifest.json"
        "${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/beam_native_contracts.def"
        "${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/beam_native_instructions.def"
        "${CMAKE_SOURCE_DIR}/cmake/CompileStdlib.cmake"
    COMMENT "Compiling and attesting ordinary standard-library bytecode" VERBATIM)
add_custom_target(beam_stdlib_modules DEPENDS ${_outputs} "${_receipt}")
beam_add_bytecode_inventory(beam_stdlib_inventory NAME stdlib
    MODULES ${BEAM_STDLIB_MODULES} INPUT_DIRECTORY "${_ebin}"
    INSTALL_ROOT lib/beam/stdlib DEPENDS beam_stdlib_modules)
foreach(_value IN ITEMS MODULES SOURCES)
    set(BEAM_STDLIB_${_value} "${BEAM_STDLIB_${_value}}" PARENT_SCOPE)
endforeach()
