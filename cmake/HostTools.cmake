# Generation always uses native, source-attested executables.
include_guard(GLOBAL)
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
if((CMAKE_CROSSCOMPILING OR BEAM_HOST_TOOLS_ROOT) AND NOT BEAM_HOST_TOOLS_ONLY)
    set(_host_suffix "")
    if(CMAKE_HOST_WIN32)
        set(_host_suffix .exe)
    endif()
    if(BEAM_HOST_TOOLS_ROOT)
        execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/host_tools.py"
            verify --source "${CMAKE_SOURCE_DIR}" --build "${BEAM_HOST_TOOLS_ROOT}"
            COMMAND_ERROR_IS_FATAL ANY)
        set(_host_root "${BEAM_HOST_TOOLS_ROOT}")
        add_custom_target(beam_native_host_tools
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/host_tools.py"
                verify --source "${CMAKE_SOURCE_DIR}" --build "${_host_root}" VERBATIM)
    else()
        include(ExternalProject)
        set(_host_root "${CMAKE_BINARY_DIR}/host-tools")
        if(CMAKE_HOST_WIN32)
            find_program(BEAM_HOST_C_COMPILER cl REQUIRED NO_CMAKE_FIND_ROOT_PATH)
            set(BEAM_HOST_CXX_COMPILER "${BEAM_HOST_C_COMPILER}")
        else()
            find_program(BEAM_HOST_C_COMPILER NAMES cc clang gcc REQUIRED NO_CMAKE_FIND_ROOT_PATH)
            find_program(BEAM_HOST_CXX_COMPILER NAMES c++ clang++ g++ REQUIRED NO_CMAKE_FIND_ROOT_PATH)
        endif()
        ExternalProject_Add(beam_native_host_tools
            SOURCE_DIR "${CMAKE_SOURCE_DIR}" BINARY_DIR "${_host_root}"
            DOWNLOAD_COMMAND "" UPDATE_COMMAND "" INSTALL_COMMAND ""
            CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env --unset=CC --unset=CXX --unset=CFLAGS
                --unset=CXXFLAGS --unset=LDFLAGS --unset=SDKROOT --unset=MACOSX_DEPLOYMENT_TARGET
                ${CMAKE_COMMAND} -S "${CMAKE_SOURCE_DIR}" -B "${_host_root}" -G Ninja
                -DCMAKE_BUILD_TYPE=Release -DERTS_BUILD_FLAVOR=emu
                -DBEAM_HOST_TOOLS_ONLY=ON -DBUILD_TESTING=OFF
                -DBEAM_BUNDLE_PACKAGE_TOOL=OFF
                "-DPython3_EXECUTABLE=${Python3_EXECUTABLE}"
                "-DCMAKE_C_COMPILER=${BEAM_HOST_C_COMPILER}"
                "-DCMAKE_CXX_COMPILER=${BEAM_HOST_CXX_COMPILER}"
                "-DBEAM_PACKAGE_SOURCE_DIR=${BEAM_PACKAGE_SOURCE_DIR}"
            BUILD_COMMAND ${CMAKE_COMMAND} --build "${_host_root}" --target beam_host_tools
            BUILD_ALWAYS TRUE
            BUILD_BYPRODUCTS "${_host_root}/bin/beam_bootstrap${_host_suffix}"
                "${_host_root}/bin/beam_seed_compiler${_host_suffix}"
                "${_host_root}/bin/yielding_c_fun_host${_host_suffix}")
    endif()
    foreach(_name IN ITEMS beam_seed_compiler beam_bootstrap yielding_c_fun_host)
        add_executable(${_name} IMPORTED GLOBAL)
        set_target_properties(${_name} PROPERTIES IMPORTED_LOCATION "${_host_root}/bin/${_name}${_host_suffix}")
        add_dependencies(${_name} beam_native_host_tools)
    endforeach()
endif()
function(beam_register_host_tools_manifest)
    if(NOT CMAKE_CROSSCOMPILING AND NOT BEAM_HOST_TOOLS_ROOT)
        add_custom_target(beam_host_tools
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/cmake/host_tools.py"
                create --source "${CMAKE_SOURCE_DIR}" --build "${CMAKE_BINARY_DIR}"
            DEPENDS beam_bootstrap beam_seed_compiler yielding_c_fun_host beam_verify_compiler_seeds VERBATIM)
    endif()
endfunction()
