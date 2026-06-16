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
        if(name STREQUAL "BEAM")
            foreach(_header IN ITEMS beam.h runtime.h boot.h native.h sdk/bytecode.h sdk/inventories.h)
                set(_source "${CMAKE_SOURCE_DIR}/include/beam/${_header}")
                get_filename_component(_directory "${_header}" DIRECTORY)
                target_sources(${target} PRIVATE "${_source}")
                set_source_files_properties("${_source}" PROPERTIES
                    MACOSX_PACKAGE_LOCATION "Headers/beam/${_directory}")
            endforeach()
            file(WRITE "${CMAKE_BINARY_DIR}/module.modulemap"
                "framework module BEAM { header \"beam/beam.h\" export * }\n")
            target_sources(${target} PRIVATE "${CMAKE_BINARY_DIR}/module.modulemap")
            set_source_files_properties("${CMAKE_BINARY_DIR}/module.modulemap" PROPERTIES
                MACOSX_PACKAGE_LOCATION Modules)
        endif()
    elseif(APPLE)
        set_target_properties(${target} PROPERTIES INSTALL_NAME_DIR "@rpath")
    endif()
endfunction()
