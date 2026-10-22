# The modern compiler and components build from attested BEAM compiler seeds.
set(_beam_host_beam "${CMAKE_BINARY_DIR}/lib/beam/ebin/beam_host.beam")

# One compiler output also supplies the explicitly loadable host bridge asset.
add_custom_command(OUTPUT "${_beam_host_beam}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/lib/beam/ebin"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BEAM_HOST_BEAM}" "${_beam_host_beam}"
    DEPENDS "${BEAM_HOST_BEAM}" beam_services_modules
    COMMENT "Staging the optional host bridge" VERBATIM)
add_custom_target(beam_host_artifact DEPENDS "${_beam_host_beam}")

set(_beam_seed_outputs)
foreach(_module IN LISTS BEAM_SERVICE_SEED_MODULES)
    list(APPEND _beam_seed_outputs
        "${CMAKE_BINARY_DIR}/lib/beam/runtime/beam/ebin/${_module}.beam")
endforeach()
add_custom_command(
    OUTPUT ${_beam_seed_outputs}
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_BINARY_DIR}/lib/beam/runtime/beam/ebin"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${BEAM_SERVICE_SEED_FILES}
        "${CMAKE_BINARY_DIR}/lib/beam/runtime/beam/ebin"
    DEPENDS ${BEAM_SERVICE_SEED_FILES}
    COMMENT "Staging BEAM component artifacts"
    VERBATIM)
add_custom_target(beam_service_artifacts DEPENDS ${_beam_seed_outputs})
add_custom_target(beam_run ALL)
add_dependencies(beam_run beam_host_artifact
    beam_service_artifacts beam_compiler_tooling beam_cli_support beam_run_support beam_interactive_support ${BEAM_COMPONENT_TARGETS}
    ${BEAM_DYNAMIC_NIF_TARGETS} ${BEAM_DYNAMIC_DRIVER_TARGETS})
if(TARGET beam_pkg_support)
    add_dependencies(beam_run beam_pkg_support)
endif()
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
        -DOUTPUT_DIR=${_base_ebin} -DVERIFY=ON
        -P ${CMAKE_SOURCE_DIR}/cmake/RefreshBaseArtifacts.cmake
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/base-verification"
    COMMAND ${CMAKE_COMMAND} -E touch "${_base_check_stamp}"
    DEPENDS beam_bootstrap
        ${BEAM_BASE_SOURCES} ${BEAM_BASE_SEED_FILES}
        "${CMAKE_SOURCE_DIR}/cmake/RefreshBaseArtifacts.cmake"
    COMMENT "Compiling and verifying ordinary base bytecode" VERBATIM)
add_custom_target(beam_verify_base_artifacts ALL DEPENDS "${_base_check_stamp}")
add_dependencies(beam_run beam_verify_base_artifacts)

# Expectations follow compilation; neither these tables nor their verifier
# become inputs to libbeam or its bootstrap compiler.
set(_bytecode_include "${CMAKE_BINARY_DIR}/bytecode/include")
set(_base_header "${_bytecode_include}/beam_base_inventory.h")
set(_base_expectations "${_bytecode_include}/BEAMBaseInventory.cmake")
set(_base_manifest "${CMAKE_BINARY_DIR}/lib/beam/library/modules.txt")
add_custom_command(OUTPUT "${_base_header}" "${_base_expectations}" "${_base_manifest}"
    BYPRODUCTS "${_bytecode_include}/base-modules.txt"
    COMMAND ${CMAKE_COMMAND}
        -DNAME=base "-DBUILD_ID=${BEAM_COMPONENT_BUILD_ID}" "-DMODULES=${BEAM_BASE_MODULES}"
        "-DINPUT_DIR=${_base_ebin}" "-DOUTPUT_DIR=${_bytecode_include}"
        -DRELATIVE_ROOT=lib/beam/library
        -P "${CMAKE_SOURCE_DIR}/cmake/GenerateBytecodeInventory.cmake"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_bytecode_include}/base-modules.txt" "${_base_manifest}"
    DEPENDS "${_base_check_stamp}" ${_base_outputs}
        "${CMAKE_SOURCE_DIR}/cmake/GenerateBytecodeInventory.cmake"
    COMMENT "Generating verified base inventory expectations" VERBATIM)
add_custom_target(beam_base_inventory DEPENDS "${_base_header}" "${_base_expectations}" "${_base_manifest}")
add_dependencies(beam_run beam_base_inventory)
add_dependencies(beam_cli_recipe beam_base_inventory)
target_sources(beam_cli_recipe PRIVATE "${CMAKE_SOURCE_DIR}/src/beam_bytecode_verify.c")
target_include_directories(beam_cli_recipe PRIVATE "${_bytecode_include}")
# Ordinary support expectations are downstream of the verified compiler.
foreach(_inventory IN ITEMS boot services)
    string(TOUPPER "${_inventory}" _upper)
    string(SUBSTRING "${_inventory}" 0 1 _initial)
    string(TOUPPER "${_initial}" _initial)
    string(SUBSTRING "${_inventory}" 1 -1 _rest)
    set(_root "${CMAKE_BINARY_DIR}/lib/beam/${_inventory}")
    set(_header "${_bytecode_include}/beam_${_inventory}_inventory.h")
    set(_expectations "${_bytecode_include}/BEAM${_initial}${_rest}Inventory.cmake")
    set(_manifest "${_root}/modules.txt")
    add_custom_command(OUTPUT "${_header}" "${_expectations}" "${_manifest}"
        BYPRODUCTS "${_bytecode_include}/${_inventory}-modules.txt"
        COMMAND ${CMAKE_COMMAND}
            -DNAME=${_inventory} "-DBUILD_ID=${BEAM_COMPONENT_BUILD_ID}"
            "-DMODULES=${BEAM_${_upper}_MODULES}" "-DINPUT_DIR=${_root}/ebin"
            "-DOUTPUT_DIR=${_bytecode_include}" -DRELATIVE_ROOT=lib/beam/${_inventory}
            -P "${CMAKE_SOURCE_DIR}/cmake/GenerateBytecodeInventory.cmake"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_bytecode_include}/${_inventory}-modules.txt" "${_manifest}"
        DEPENDS beam_${_inventory}_modules ${BEAM_${_upper}_BEAM_FILES}
            "${CMAKE_SOURCE_DIR}/cmake/GenerateBytecodeInventory.cmake"
        COMMENT "Generating verified ${_inventory} inventory expectations" VERBATIM)
    add_custom_target(beam_${_inventory}_inventory DEPENDS "${_header}" "${_expectations}" "${_manifest}")
    add_dependencies(beam_run beam_${_inventory}_inventory)
    add_dependencies(beam_cli_recipe beam_${_inventory}_inventory)
    install(FILES "${_header}" DESTINATION include)
    install(FILES "${_expectations}" DESTINATION lib/cmake/BEAM)
    install(FILES "${_manifest}" DESTINATION lib/beam/${_inventory})
    install(FILES ${BEAM_${_upper}_BEAM_FILES} DESTINATION lib/beam/${_inventory}/ebin)
endforeach()

add_custom_target(beam_refresh_base_artifacts
    COMMAND ${CMAKE_COMMAND}
        -DBOOTSTRAP=$<TARGET_FILE:beam_bootstrap> -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DOUTPUT_DIR=${CMAKE_BINARY_DIR}/base-artifacts
        -P ${CMAKE_SOURCE_DIR}/cmake/RefreshBaseArtifacts.cmake
    DEPENDS beam_bootstrap
    COMMENT "Refreshing base component artifact candidates" VERBATIM)

string(JOIN "\n" _manifest ${BEAM_INSTALLED_MODULES})
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/lib/beam/runtime/beam")
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/lib/beam/runtime/beam/modules.txt"
    CONTENT "${_manifest}\n" @ONLY)

add_custom_target(beam_refresh_native_artifacts
    COMMAND ${CMAKE_COMMAND}
        -DBOOTSTRAP=$<TARGET_FILE:beam_bootstrap>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DOUTPUT_DIR=${CMAKE_BINARY_DIR}/native-artifacts/beam
        -P ${CMAKE_SOURCE_DIR}/cmake/RefreshNativeArtifacts.cmake
    DEPENDS beam_run VERBATIM)
set(_core_refresh_dir "${CMAKE_BINARY_DIR}/core-artifacts")
set(_core_refresh_outputs)
foreach(_module IN LISTS BEAM_CORE_MODULES)
    list(APPEND _core_refresh_outputs "${_core_refresh_dir}/${_module}.beam")
endforeach()
set(_core_check_stamp "${CMAKE_BINARY_DIR}/core-verification/verified.stamp")
add_custom_command(OUTPUT "${_core_check_stamp}"
    COMMAND ${CMAKE_COMMAND}
        -DBOOTSTRAP=$<TARGET_FILE:beam_bootstrap> -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DOUTPUT_DIR=${CMAKE_BINARY_DIR}/core-verification -DVERIFY=ON
        -P ${CMAKE_SOURCE_DIR}/cmake/RefreshCoreArtifacts.cmake
    COMMAND ${CMAKE_COMMAND} -E touch "${_core_check_stamp}"
    DEPENDS beam_bootstrap
        ${BEAM_CORE_SOURCES} ${BEAM_CORE_BEAM_FILES}
        "${CMAKE_SOURCE_DIR}/cmake/RuntimeCoreModules.cmake"
        "${CMAKE_SOURCE_DIR}/cmake/RefreshCoreArtifacts.cmake"
    COMMENT "Verifying embedded core bytecode and inspection metadata" VERBATIM)
add_custom_target(beam_verify_core_artifacts ALL DEPENDS "${_core_check_stamp}")
add_dependencies(beam_run beam_verify_core_artifacts)
add_custom_target(beam_refresh_core_artifacts
    COMMAND ${CMAKE_COMMAND}
        -DBOOTSTRAP=$<TARGET_FILE:beam_bootstrap>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DOUTPUT_DIR=${_core_refresh_dir}
        -P ${CMAKE_SOURCE_DIR}/cmake/RefreshCoreArtifacts.cmake
    BYPRODUCTS ${_core_refresh_outputs}
    DEPENDS beam_bootstrap
    COMMENT "Refreshing shared core artifact candidates with inspection metadata"
    VERBATIM)
add_custom_target(beam_refresh_boot_artifacts)
add_dependencies(beam_refresh_boot_artifacts
    beam_refresh_native_artifacts beam_refresh_core_artifacts beam_refresh_base_artifacts)
