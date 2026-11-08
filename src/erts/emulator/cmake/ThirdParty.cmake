# Vendored ryu and zlib linked into the VM.

function(erts_add_emulator_third_party_libraries)
    add_library(ryu OBJECT ryu/d2s.c)
    set_target_properties(ryu PROPERTIES POSITION_INDEPENDENT_CODE ON)

    add_library(zlib OBJECT
        zlib/adler32.c
        zlib/compress.c
        zlib/crc32.c
        zlib/uncompr.c
        zlib/deflate.c
        zlib/trees.c
        zlib/zutil.c
        zlib/inflate.c
        zlib/inftrees.c
        zlib/inffast.c
    )
    # One private codec backs VM internals and the public namespaced interface.
    # Never interpose on an independently built host or NIF system dependency.
    target_include_directories(zlib PRIVATE "${CMAKE_SOURCE_DIR}/include")
    set_target_properties(zlib PROPERTIES POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET hidden)

    # Visibility isolates shared objects. Distinct names also isolate archives
    # from unrelated objects linked into the same executable.
    foreach(_target IN ITEMS beam_emulator zlib)
        if(MSVC)
            target_compile_options(${_target} PRIVATE
                "/FI${CMAKE_CURRENT_SOURCE_DIR}/beam_private_vendor_symbols.h")
        else()
            target_compile_options(${_target} PRIVATE
                "-include" "${CMAKE_CURRENT_SOURCE_DIR}/beam_private_vendor_symbols.h")
        endif()
    endforeach()

endfunction()
