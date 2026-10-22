function(beam_platform_library target name)
    set_target_properties(${target} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(BEAM_APPLE_FRAMEWORKS)
        set_target_properties(${target} PROPERTIES FRAMEWORK TRUE OUTPUT_NAME "${name}"
            MACOSX_FRAMEWORK_IDENTIFIER "org.erlangsters.beam.${name}"
            MACOSX_FRAMEWORK_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_FRAMEWORK_BUNDLE_VERSION "${PROJECT_VERSION}"
            INSTALL_NAME_DIR "@rpath" BUILD_WITH_INSTALL_NAME_DIR TRUE)
    elseif(APPLE)
        set_target_properties(${target} PROPERTIES INSTALL_NAME_DIR "@rpath")
    endif()
endfunction()
