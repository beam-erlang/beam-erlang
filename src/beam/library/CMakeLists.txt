# Ordinary modern library code is supplied explicitly, outside the VM.
set(BEAM_BASE_MODULES beam beam_list beam_map beam_bytes beam_text beam_format beam_time)
set(BEAM_BASE_SOURCES)
set(BEAM_BASE_SEED_FILES)
foreach(_module IN LISTS BEAM_BASE_MODULES)
    list(APPEND BEAM_BASE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${_module}.erl")
    list(APPEND BEAM_BASE_SEED_FILES "${CMAKE_CURRENT_SOURCE_DIR}/ebin/${_module}.beam")
endforeach()
foreach(_value IN ITEMS MODULES SOURCES SEED_FILES)
    set(BEAM_BASE_${_value} "${BEAM_BASE_${_value}}" PARENT_SCOPE)
endforeach()

set(_base_check_stamp "${CMAKE_BINARY_DIR}/base-verification/verified.stamp")
set(_base_ebin "${CMAKE_BINARY_DIR}/lib/beam/library/ebin")
set(_base_outputs)
foreach(_module IN LISTS BEAM_BASE_MODULES)
    list(APPEND _base_outputs "${_base_ebin}/${_module}.beam")
endforeach()
add_custom_command(OUTPUT "${_base_check_stamp}"
    BYPRODUCTS ${_base_outputs}
    COMMAND ${CMAKE_COMMAND}
        -DBOOTSTRAP=$<TARGET_FILE:beam_bootstrap> -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        "-DMODULES=${BEAM_BASE_MODULES}" "-DSOURCES=${BEAM_BASE_SOURCES}"
        -DOUTPUT_DIR=${_base_ebin} -DVERIFY=ON
        -P ${CMAKE_SOURCE_DIR}/cmake/RefreshBaseArtifacts.cmake
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/base-verification"
    COMMAND ${CMAKE_COMMAND} -E touch "${_base_check_stamp}"
    DEPENDS beam_bootstrap
        ${BEAM_BASE_SOURCES} ${BEAM_BASE_SEED_FILES}
        "${CMAKE_SOURCE_DIR}/cmake/RefreshBaseArtifacts.cmake"
    COMMENT "Compiling and verifying ordinary base bytecode" VERBATIM)
add_custom_target(beam_verify_base_artifacts ALL DEPENDS "${_base_check_stamp}")


beam_add_bytecode_inventory(beam_base_inventory NAME base
    MODULES ${BEAM_BASE_MODULES} INPUT_DIRECTORY "${_base_ebin}"
    INSTALL_ROOT lib/beam/library DEPENDS beam_verify_base_artifacts)

add_custom_target(beam_refresh_base_artifacts
    COMMAND ${CMAKE_COMMAND}
        -DBOOTSTRAP=$<TARGET_FILE:beam_bootstrap> -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        "-DMODULES=${BEAM_BASE_MODULES}" "-DSOURCES=${BEAM_BASE_SOURCES}"
        -DOUTPUT_DIR=${CMAKE_BINARY_DIR}/base-artifacts
        -P ${CMAKE_SOURCE_DIR}/cmake/RefreshBaseArtifacts.cmake
    DEPENDS beam_bootstrap
    COMMENT "Refreshing base component artifact candidates" VERBATIM)
