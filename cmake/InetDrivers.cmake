# Each library exposes one ordinary driver entry. The owning component
# supplies its source; driver names remain tcp_inet, udp_inet and sctp_inet.
function(beam_add_inet_drivers family source)
    file(STRINGS "${PROJECT_BINARY_DIR}/erts/config.h" _inet_sctp_header
        REGEX "^[ \t]*#[ \t]*define[ \t]+HAVE_SCTP_H([ \t]|$)")
    set(_inet_protocols tcp udp)
    if(_inet_sctp_header)
        list(APPEND _inet_protocols sctp)
    endif()

    string(TOUPPER "${family}" _family_upper)
    set(_driver_names)
    set(_driver_targets)
    foreach(_protocol IN LISTS _inet_protocols)
        string(TOUPPER "${_protocol}" _protocol_upper)
        set(_target "${family}_${_protocol}_inet_driver")
        set(_name "${_protocol}_inet")
        add_library(${_target} MODULE "${source}")
        beam_configure_native_component(${_target})
        target_compile_definitions(${_target} PRIVATE
            ERTS_CONFIG_SKIP_ATOM_TABLE
            "BEAM_INET_DRIVER_${_protocol_upper}=1")
        # Packet parsing and runtime diagnostics use the matching build's private
        # ERTS interface; socket ownership and callbacks use the driver API.
        target_link_libraries(${_target} PRIVATE libbeam)
        target_link_options(${_target} PRIVATE "LINKER:-z,defs")
        set_target_properties(${_target} PROPERTIES
            OUTPUT_NAME "${_name}"
            PREFIX ""
            C_VISIBILITY_PRESET hidden
            LIBRARY_OUTPUT_DIRECTORY
                "${CMAKE_BINARY_DIR}/lib/beam/runtime/${family}/priv"
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "$ORIGIN/../../../..")
        list(APPEND _driver_names "${_name}")
        list(APPEND _driver_targets "${_target}")
    endforeach()
    set(${_family_upper}_DRIVER_NAMES "${_driver_names}" PARENT_SCOPE)
    set(${_family_upper}_DRIVER_TARGETS "${_driver_targets}" PARENT_SCOPE)
endfunction()
