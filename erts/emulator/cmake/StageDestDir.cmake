# Match CMake file(INSTALL) DESTDIR handling.
# Unix prefixes the absolute destination with DESTDIR.
# Windows removes the drive letter from the destination, then prefixes DESTDIR.
function(erts_staged_path destdir absolute_path out_var)
    if(destdir STREQUAL "")
        set(${out_var} "${absolute_path}" PARENT_SCOPE)
        return()
    endif()
    file(TO_CMAKE_PATH "${destdir}" _dest)
    set(_rest "${absolute_path}")
    if(_rest MATCHES "^[A-Za-z]:(.*)$")
        set(_rest "${CMAKE_MATCH_1}")
    endif()
    if(NOT _rest MATCHES "^/")
        set(_rest "/${_rest}")
    endif()
    cmake_path(SET _staged NORMALIZE "${_dest}${_rest}")
    set(${out_var} "${_staged}" PARENT_SCOPE)
endfunction()

# file(INSTALL) records manifest paths without DESTDIR. When DESTDIR is set on
# Windows, it also drops the destination drive letter. Flavor copies must use
# that same form or the manifest mixes the two.
function(erts_install_manifest_path absolute_path out_var)
    set(_path "${absolute_path}")
    if(NOT "$ENV{DESTDIR}" STREQUAL "" AND _path MATCHES "^[A-Za-z]:(.*)$")
        set(_path "${CMAKE_MATCH_1}")
    endif()
    set(${out_var} "${_path}" PARENT_SCOPE)
endfunction()
