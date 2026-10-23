# Compiler implementation and its explicit private support inventory.
# The seed compiler produces all compiler and private helper artifacts.
set(_compiler_out "${CMAKE_BINARY_DIR}/lib/beam/tools/compiler/ebin")
set(_compiler_generated "${CMAKE_BINARY_DIR}/compiler/generated")
set(_compiler_include "${CMAKE_BINARY_DIR}/compiler/include")
file(MAKE_DIRECTORY "${_compiler_generated}" "${_compiler_include}")
add_custom_command(
    OUTPUT "${_compiler_generated}/beam_opcodes.erl" "${_compiler_generated}/beam_opcodes.hrl"
    COMMAND ${PERL_EXECUTABLE} "${CMAKE_SOURCE_DIR}/src/erts/emulator/utils/beam_makeops"
        -compiler -outdir "${_compiler_generated}" "${CMAKE_SOURCE_DIR}/src/lib/compiler/src/genop.tab"
    DEPENDS "${CMAKE_SOURCE_DIR}/src/erts/emulator/utils/beam_makeops"
        "${CMAKE_SOURCE_DIR}/src/lib/compiler/src/genop.tab"
    COMMENT "Generating compiler opcode tables" VERBATIM)
set(_compiler_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_a.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_asm.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_block.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_bounds.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_call_types.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_clean.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_core_to_ssa.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_dict.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_disasm.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_doc.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_flatten.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_jump.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_listing.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_ssa.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_ssa_alias.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_ssa_bc_size.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_ssa_bool.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_ssa_bsm.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_ssa_check.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_ssa_codegen.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_ssa_dead.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_ssa_destructive_update.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_ssa_lint.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_ssa_opt.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_ssa_pp.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_ssa_pre_codegen.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_ssa_recv.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_ssa_share.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_ssa_ss.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_ssa_throw.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_ssa_type.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_trim.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_types.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_utils.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_validator.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/beam_z.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/cerl.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/cerl_clauses.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/cerl_inline.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/cerl_trees.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/compile.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/core_lib.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/core_lint.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/core_pp.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/core_scan.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/erl_bifs.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/rec_env.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/sys_core_alias.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/sys_core_bsm.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/sys_core_fold.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/sys_core_fold_lists.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/sys_core_inline.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/sys_core_prepare.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/sys_coverage.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/sys_messages.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/sys_pre_attributes.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/passes/v3_core.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/frontend/epp.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/frontend/erl_expand_records.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/frontend/erl_features.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/frontend/erl_lint.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/host/beam_compiler_host.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/host/code.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/host/error_handler.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/host/file.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/host/init.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/host/io.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/host/os.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/beam_compiler_engine.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/beam_compiler_cli.erl"
    "${_compiler_generated}/beam_opcodes.erl")
add_custom_target(beam_compiler_opcode_tables DEPENDS
    "${_compiler_generated}/beam_opcodes.erl" "${_compiler_generated}/beam_opcodes.hrl")
add_custom_target(beam_compiler_modules)
foreach(_source IN LISTS _compiler_sources)
    get_filename_component(_module "${_source}" NAME_WE)
    set(_target "beam_compiler_module_${_module}")
    beam_add_erlang_component(${_target}
        COMPILER beam_seed_compiler
        SOURCES "${_source}"
        INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/passes" "${_compiler_generated}"
            "${BEAM_SUPPORT_INCLUDE_DIRECTORY}"
        OUTPUT_DIRECTORY "${_compiler_out}")
    add_dependencies(${_target} beam_compiler_opcode_tables)
    add_dependencies(beam_compiler_modules ${_target})
endforeach()
set(_compiler_support
    array beam_lib binary dict digraph digraph_utils erl_abstract_code
    erl_anno erl_bits erl_error erl_eval erl_internal erl_parse erl_posix_msg
    erl_pp erl_scan erl_stdlib_errors ets eval_bits filelib filename
    gb_sets gb_trees graph io_lib io_lib_format io_lib_fread io_lib_pretty lists
    maps orddict ordsets otp_internal proplists queue sets
    sofs string unicode unicode_util)
set(BEAM_COMPILER_MODULES)
set(BEAM_COMPILER_BEAM_FILES)
foreach(_source IN LISTS _compiler_sources)
    get_filename_component(_module "${_source}" NAME_WE)
    list(APPEND BEAM_COMPILER_MODULES "${_module}")
    list(APPEND BEAM_COMPILER_BEAM_FILES "${_compiler_out}/${_module}.beam")
endforeach()
set(_support_outputs)
foreach(_module IN LISTS _compiler_support)
    set(_input "${BEAM_SUPPORT_OUTPUT_DIRECTORY}/${_module}.beam")
    set(_output "${_compiler_out}/${_module}.beam")
    add_custom_command(OUTPUT "${_output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_compiler_out}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_input}" "${_output}"
        DEPENDS "${_input}" beam_support_modules VERBATIM)
    list(APPEND _support_outputs "${_output}")
    list(APPEND BEAM_COMPILER_MODULES "${_module}")
    list(APPEND BEAM_COMPILER_BEAM_FILES "${_output}")
endforeach()
beam_add_erlang_component(beam_compiler_host_module
    COMPILER beam_seed_compiler
    SOURCES "${CMAKE_SOURCE_DIR}/src/beam/services/beam_host.erl"
    OUTPUT_DIRECTORY "${_compiler_out}")
list(APPEND _support_outputs "${_compiler_out}/beam_host.beam")
list(APPEND BEAM_COMPILER_MODULES beam_host)
list(APPEND BEAM_COMPILER_BEAM_FILES "${_compiler_out}/beam_host.beam")
add_custom_target(beam_compiler_support DEPENDS ${_support_outputs})
add_dependencies(beam_compiler_support beam_compiler_host_module)
add_custom_target(beam_compiler_tooling)
add_dependencies(beam_compiler_tooling beam_compiler_modules beam_compiler_support)

set(_inventory "/* Private matching-build compiler module inventory. */\n#ifndef BEAM_COMPILER_INVENTORY_H\n#define BEAM_COMPILER_INVENTORY_H\nstatic const char *const beam_compiler_modules[] = {\n")
foreach(_module IN LISTS BEAM_COMPILER_MODULES)
    string(APPEND _inventory "    \"${_module}\",\n")
endforeach()
list(LENGTH BEAM_COMPILER_MODULES _module_count)
string(APPEND _inventory "};\n#define BEAM_COMPILER_MODULE_COUNT ${_module_count}\n#endif\n")
file(CONFIGURE OUTPUT "${_compiler_include}/beam_compiler_inventory.h" CONTENT "${_inventory}" @ONLY)
string(JOIN "\n" _manifest ${BEAM_COMPILER_MODULES})
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/lib/beam/tools/compiler/modules.txt" CONTENT "${_manifest}\n" @ONLY)

# Stage only the declared compiler seeds. Source verification follows their
# compilation, so the seed host also remains usable for candidate refreshes.
set(BEAM_COMPILER_SEED_ROOT "${CMAKE_BINARY_DIR}/compiler-seed")
set(_seed_out "${BEAM_COMPILER_SEED_ROOT}/lib/beam/tools/compiler/ebin")
set(_seed_source "${CMAKE_CURRENT_SOURCE_DIR}/seeds/ebin")
set(_seed_manifest "${CMAKE_CURRENT_SOURCE_DIR}/seeds/manifest.json")
set(_module_inventory "${CMAKE_BINARY_DIR}/lib/beam/tools/compiler/modules.txt")
set(_seed_files)
set(_seed_outputs)
foreach(_module IN LISTS BEAM_COMPILER_MODULES)
    list(APPEND _seed_files "${_seed_source}/${_module}.beam")
    list(APPEND _seed_outputs "${_seed_out}/${_module}.beam")
endforeach()
set(_seed_stamp "${BEAM_COMPILER_SEED_ROOT}/staged.stamp")
set(_verifier "${CMAKE_SOURCE_DIR}/cmake/VerifyCompilerSeeds.cmake")
execute_process(COMMAND ${CMAKE_COMMAND}
    "-DMANIFEST=${_seed_manifest}" "-DSEEDS=${_seed_source}"
    "-DINVENTORY=${_module_inventory}" "-DSOURCE_ROOT=${CMAKE_SOURCE_DIR}"
    -P "${_verifier}" RESULT_VARIABLE _seed_status)
if(NOT _seed_status EQUAL 0)
    message(FATAL_ERROR "Invalid BEAM compiler seeds; review the declared inventory and hashes")
endif()
add_custom_command(OUTPUT "${_seed_stamp}" ${_seed_outputs}
    COMMAND ${CMAKE_COMMAND} "-DMANIFEST=${_seed_manifest}" "-DSEEDS=${_seed_source}"
        "-DINVENTORY=${_module_inventory}" "-DSOURCE_ROOT=${CMAKE_SOURCE_DIR}" -P "${_verifier}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_seed_out}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_seed_files} "${_seed_out}"
    COMMAND ${CMAKE_COMMAND} -E touch "${_seed_stamp}"
    DEPENDS ${_seed_files} "${_seed_manifest}" "${_module_inventory}" "${_verifier}"
    COMMENT "Staging the attested BEAM compiler seeds" VERBATIM)
add_custom_target(beam_compiler_seed DEPENDS "${_seed_stamp}" ${_seed_outputs})

file(READ "${_seed_manifest}" _seed_metadata)
string(JSON _input_count LENGTH "${_seed_metadata}" inputs)
math(EXPR _input_last "${_input_count} - 1")
set(_attested_inputs)
foreach(_index RANGE 0 ${_input_last})
    string(JSON _scope GET "${_seed_metadata}" inputs ${_index} scope)
    string(JSON _path GET "${_seed_metadata}" inputs ${_index} path)
    if(_scope STREQUAL source)
        list(APPEND _attested_inputs "${CMAKE_SOURCE_DIR}/${_path}")
    elseif(_scope STREQUAL generated)
        list(APPEND _attested_inputs "${_compiler_generated}/${_path}")
    else()
        message(FATAL_ERROR "Invalid compiler seed input scope: ${_scope}")
    endif()
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_seed_manifest}")
set(_verified_stamp "${CMAKE_BINARY_DIR}/compiler/verified.stamp")
add_custom_command(OUTPUT "${_verified_stamp}"
    COMMAND ${CMAKE_COMMAND} "-DMANIFEST=${_seed_manifest}" "-DSEEDS=${_seed_source}"
        "-DINVENTORY=${_module_inventory}" "-DSOURCE_ROOT=${CMAKE_SOURCE_DIR}"
        "-DGENERATED_ROOT=${_compiler_generated}" "-DOUTPUTS=${_compiler_out}" -DVERIFY_INPUTS=ON
        -P "${_verifier}"
    COMMAND ${CMAKE_COMMAND} -E touch "${_verified_stamp}"
    DEPENDS ${BEAM_COMPILER_BEAM_FILES} ${_attested_inputs} ${_seed_files}
        beam_compiler_modules beam_compiler_support "${_seed_manifest}" "${_verifier}"
    COMMENT "Verifying compiler source attestation and seed reproduction" VERBATIM)
add_custom_target(beam_verify_compiler_seeds DEPENDS "${_verified_stamp}")
add_dependencies(beam_compiler_tooling beam_verify_compiler_seeds)
set(_refresh_sources ${_compiler_sources} "${CMAKE_SOURCE_DIR}/src/beam/services/beam_host.erl")
foreach(_module IN LISTS _compiler_support)
    list(APPEND _refresh_sources "${CMAKE_SOURCE_DIR}/src/beam/support/src/${_module}.erl")
endforeach()
set(_refresh_out "${CMAKE_BINARY_DIR}/compiler-seed-candidates")
add_custom_target(beam_refresh_compiler_seeds
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_refresh_out}"
    COMMAND $<TARGET_FILE:beam_seed_compiler> --deterministic
        -I "${CMAKE_CURRENT_SOURCE_DIR}/passes" -I "${_compiler_generated}"
        -I "${BEAM_SUPPORT_INCLUDE_DIRECTORY}" -o "${_refresh_out}" -- ${_refresh_sources}
    DEPENDS beam_seed_compiler beam_compiler_opcode_tables ${_refresh_sources}
    COMMENT "Generating reviewable BEAM compiler seed candidates" VERBATIM)
install(FILES ${BEAM_COMPILER_BEAM_FILES} DESTINATION lib/beam/tools/compiler/ebin)
install(FILES "${CMAKE_BINARY_DIR}/lib/beam/tools/compiler/modules.txt" DESTINATION lib/beam/tools/compiler)
set(BEAM_COMPILER_NATIVE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/beam_compiler_host.c" PARENT_SCOPE)
set(BEAM_COMPILER_INCLUDE_DIRECTORY "${_compiler_include}" PARENT_SCOPE)
set(BEAM_COMPILER_SEED_ROOT "${BEAM_COMPILER_SEED_ROOT}" PARENT_SCOPE)
set(BEAM_COMPILER_MODULES "${BEAM_COMPILER_MODULES}" PARENT_SCOPE)
set(BEAM_COMPILER_BEAM_FILES "${BEAM_COMPILER_BEAM_FILES}" PARENT_SCOPE)
