# Copy SDK-owned assets into a consumer distribution using ordinary install
# rules. Relative destinations honor --prefix, DESTDIR and subsequent relocation.
function(_beam_install_asset_directory asset_dir)
    cmake_parse_arguments(PARSE_ARGV 1 asset "" "DESTINATION" "")
    if(NOT asset_DESTINATION OR asset_UNPARSED_ARGUMENTS OR asset_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "Expected DESTINATION <directory>")
    endif()
    install(DIRECTORY "${asset_dir}/" DESTINATION "${asset_DESTINATION}" USE_SOURCE_PERMISSIONS)
endfunction()

function(beam_install_preloaded_modules)
    _beam_install_asset_directory("${BEAM_SDK_PRELOAD_EBIN_DIR}" ${ARGV})
endfunction()

function(beam_install_preloaded_sources)
    _beam_install_asset_directory("${BEAM_SDK_PRELOAD_SOURCE_DIR}" ${ARGV})
endfunction()

function(beam_install_native_headers)
    _beam_install_asset_directory("${BEAM_SDK_NATIVE_INCLUDE_DIR}" ${ARGV})
endfunction()

function(beam_install_child_setup)
    if(NOT TARGET BEAM::ChildSetup)
        message(FATAL_ERROR "The Windows BEAM SDK has no child-setup helper")
    endif()
    cmake_parse_arguments(PARSE_ARGV 0 helper "" "DESTINATION" "")
    if(NOT helper_DESTINATION OR helper_UNPARSED_ARGUMENTS OR helper_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "Expected DESTINATION <directory>")
    endif()
    install(PROGRAMS "$<TARGET_FILE:BEAM::ChildSetup>" DESTINATION "${helper_DESTINATION}")
    if(BEAM_SDK_BUILD_TYPE STREQUAL "Debug")
        # Resolve the consumer's prefix only at installation time. The alias
        # itself is relative and remains valid after a staged tree is moved.
        set(_alias_code [=[
set(_beam_helper_destination [==[@helper_DESTINATION@]==])
if(NOT IS_ABSOLUTE "${_beam_helper_destination}")
    set(_beam_helper_destination "${CMAKE_INSTALL_PREFIX}/${_beam_helper_destination}")
endif()
set(_beam_helper_alias "$ENV{DESTDIR}${_beam_helper_destination}/erl_child_setup.debug")
file(REMOVE "${_beam_helper_alias}")
file(CREATE_LINK erl_child_setup "${_beam_helper_alias}" SYMBOLIC)
list(APPEND CMAKE_INSTALL_MANIFEST_FILES "${_beam_helper_destination}/erl_child_setup.debug")
]=])
        string(CONFIGURE "${_alias_code}" _alias_code @ONLY)
        install(CODE "${_alias_code}")
    endif()
endfunction()
