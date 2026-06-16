# Each component owns its bytecode/native deployment recipe.
add_subdirectory(support)
add_subdirectory(library)
add_subdirectory(boot)
add_subdirectory(services)
add_subdirectory(file)
add_subdirectory(network)
add_subdirectory(archive)
add_subdirectory(eval)
add_subdirectory(tracing)
add_subdirectory(zlib)
add_subdirectory(compiler)
add_subdirectory(cli)
add_subdirectory(run)
add_subdirectory(interactive)
foreach(_module IN ITEMS prim_zip prim_eval erl_tracer zlib)
    string(TOUPPER "${_module}" _name)
    set(BEAM_${_name}_BEAM "${BEAM_${_name}_BEAM}" PARENT_SCOPE)
endforeach()
foreach(_asset IN ITEMS BASE_MODULES BASE_SOURCES BASE_SEED_FILES
        ARCHIVE_MODULES ARCHIVE_SOURCES ARCHIVE_HEADERS ARCHIVE_BEAM_FILES
        ARCHIVE_REQUIRED_MODULES ARCHIVE_FILE_REQUIRED_MODULES
        EVAL_MODULES EVAL_SOURCES EVAL_BEAM_FILES
        BOOT_MODULES BOOT_SOURCES BOOT_BEAM_FILES BOOT_NATIVE_SOURCES BOOT_CLI_SOURCES HOST_BEAM
        SERVICES_MODULES SERVICES_BEAM_FILES
        FILE_COMPONENT_MODULES FILE_COMPONENT_BEAM_FILES
        NETWORK_COMPONENT_MODULES NETWORK_COMPONENT_BEAM_FILES
        COMPILER_NATIVE_SOURCES COMPILER_MODULES COMPILER_BEAM_FILES
        COMPILER_INCLUDE_DIRECTORY COMPILER_SEED_ROOT RUN_INCLUDE_DIRECTORY RUN_MODULES RUN_BEAM_FILES
        SUPPORT_INCLUDE_DIRECTORY SUPPORT_OUTPUT_DIRECTORY
        CLI_INCLUDE_DIRECTORY CLI_MODULES CLI_BEAM_FILES
        INTERACTIVE_INCLUDE_DIRECTORY INTERACTIVE_MODULES INTERACTIVE_BEAM_FILES)
    set(BEAM_${_asset} "${BEAM_${_asset}}" PARENT_SCOPE)
endforeach()
set(BEAM_COMPONENT_TARGETS beam_file_component beam_network_component beam_zlib_component PARENT_SCOPE)
set(BEAM_NATIVE_NIF_TARGETS beam_erl_tracer_nif PARENT_SCOPE)
set(BEAM_DRIVER_NAMES "" PARENT_SCOPE)
set(BEAM_DRIVER_TARGETS "" PARENT_SCOPE)
