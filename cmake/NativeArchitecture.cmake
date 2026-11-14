# Check the actual C and C++ targets, including configuration-specific flags.
# Presets name an expected native architecture; direct builds select the host.
set(BEAM_NATIVE_ARCHITECTURE "" CACHE STRING
    "Required native architecture: x86_64, aarch64 or arm64; empty selects the host")
set_property(CACHE BEAM_NATIVE_ARCHITECTURE PROPERTY STRINGS "" x86_64 aarch64 arm64)

function(beam_check_native_architecture)
    if(CMAKE_CROSSCOMPILING OR NOT CMAKE_HOST_SYSTEM_NAME STREQUAL CMAKE_SYSTEM_NAME)
        message(FATAL_ERROR "BEAM requires a native host and target")
    endif()
    set(_required "${BEAM_NATIVE_ARCHITECTURE}")
    if(_required STREQUAL "")
        set(_required "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    endif()
    string(TOLOWER "${_required}" _required)
    if(_required MATCHES "^(x86_64|amd64)$")
        set(_required x86_64)
        set(_compiler_arch "defined(__x86_64__) || defined(_M_X64)")
    elseif(_required MATCHES "^(aarch64|arm64)$")
        set(_required arm64)
        set(_compiler_arch "defined(__aarch64__) || defined(_M_ARM64)")
    else()
        message(FATAL_ERROR "Unsupported native architecture '${_required}'")
    endif()
    foreach(_processor CMAKE_HOST_SYSTEM_PROCESSOR CMAKE_SYSTEM_PROCESSOR)
        string(TOLOWER "${${_processor}}" _actual)
        if(_actual MATCHES "^(x86_64|amd64)$")
            set(_actual x86_64)
        elseif(_actual MATCHES "^(aarch64|arm64)$")
            set(_actual arm64)
        endif()
        if(NOT _actual STREQUAL _required)
            message(FATAL_ERROR
                "Selected native architecture ${_required} does not match ${_processor} '${${_processor}}'")
        endif()
    endforeach()
    set(_probe "#if !(${_compiler_arch})\n#error Incorrect native compiler target\n#endif\ntypedef char native_pointer_width[(sizeof(void *) == 8) ? 1 : -1];\nint native_architecture_probe(void) { return 0; }\n")
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    set(CMAKE_TRY_COMPILE_CONFIGURATION "${CMAKE_BUILD_TYPE}")
    try_compile(_matches
        SOURCE_FROM_CONTENT native_architecture.c "${_probe}"
        SOURCE_FROM_CONTENT native_architecture.cpp "${_probe}"
        NO_CACHE OUTPUT_VARIABLE _output)
    if(NOT _matches)
        message(FATAL_ERROR
            "BEAM requires native 64-bit ${_required} C and C++ compiler targets; check compiler flags.\n${_output}")
    endif()
endfunction()

beam_check_native_architecture()
