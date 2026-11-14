# Erlang runtime: generated headers and the emulator.

add_library(beam_erts_headers INTERFACE)
add_library(beam-erlang::erts_headers ALIAS beam_erts_headers)

# Match upstream TYPE=debug for all consumers of internal runtime layouts,
# including ethread and native test fixtures. Do not leak DEBUG to vendored
# libraries or the yielding-C host generator.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(beam_erts_headers INTERFACE DEBUG)
    if(MSVC)
        # Upstream TYPE=debug is -g -Og on Unix. MSVC has no -Og.
        target_compile_options(beam_erts_headers INTERFACE
            "$<$<COMPILE_LANGUAGE:C,CXX>:/Od;/Zi;/UNDEBUG>")
    else()
        target_compile_options(beam_erts_headers INTERFACE
            "$<$<COMPILE_LANGUAGE:C,CXX>:-g;-Og;-UNDEBUG>")
    endif()
endif()

target_include_directories(beam_erts_headers INTERFACE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/include/internal
)

add_subdirectory(emulator)
