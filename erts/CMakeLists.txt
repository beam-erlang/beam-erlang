# Erlang runtime: generated headers and the emulator.

add_library(beam_erts_headers INTERFACE)
add_library(beam-erlang::erts_headers ALIAS beam_erts_headers)

target_include_directories(beam_erts_headers INTERFACE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/include/internal
)

add_subdirectory(emulator)
