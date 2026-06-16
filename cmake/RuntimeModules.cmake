# Shared embedded core and independently composed modern BEAM modules.
include_guard(GLOBAL)
include(${CMAKE_CURRENT_LIST_DIR}/RuntimeCoreModules.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/RuntimeModulesBEAM.cmake)

set(BEAM_CORE_MODULE_INITIALIZERS "")
foreach(_module IN LISTS BEAM_CORE_MODULES)
    string(APPEND BEAM_CORE_MODULE_INITIALIZERS "    \"${_module}\",\n")
endforeach()
configure_file("${CMAKE_CURRENT_LIST_DIR}/beam_core_modules.h.in"
    "${CMAKE_BINARY_DIR}/erts/beam_core_modules.h" @ONLY)
