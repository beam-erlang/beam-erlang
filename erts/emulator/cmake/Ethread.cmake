# ethread static library linked into beam. Unix uses pthreads.
# Windows uses erts/lib_src/win.

function(erts_add_ethread_library)
    if(NOT DEFINED EMULATOR_GENERATED_DIR)
        message(FATAL_ERROR "erts_add_ethread_library requires EMULATOR_GENERATED_DIR")
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(_ethread_sources
            ../lib_src/win/ethread.c
            ../lib_src/win/ethr_event.c
        )
    else()
        set(_ethread_sources
            ../lib_src/pthread/ethread.c
            ../lib_src/pthread/ethr_event.c
        )
    endif()
    add_library(ethread STATIC
        ../lib_src/common/ethr_aux.c
        ../lib_src/common/ethr_atomics.c
        ../lib_src/common/ethr_mutex.c
        ../lib_src/common/ethr_cbf.c
        ${_ethread_sources}
    )
    target_compile_definitions(ethread PRIVATE
        USE_THREADS
        _THREAD_SAFE
        _REENTRANT
        HAVE_CONFIG_H
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_compile_definitions(ethread PRIVATE
            __WIN32__
            WIN32
            WIN32_THREADS
            _CRT_SECURE_NO_DEPRECATE
            _WIN32_WINNT=0x0600
            WINVER=0x0600
            FD_SETSIZE=1024
            ERTS_MIXED_VC
        )
    else()
        target_compile_definitions(ethread PRIVATE
            POSIX_THREADS
            _POSIX_THREAD_SAFE_FUNCTIONS
        )
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_compile_definitions(ethread PRIVATE _GNU_SOURCE)
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
            target_compile_definitions(ethread PRIVATE _DARWIN_C_SOURCE=)
        endif()
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_link_libraries(ethread PRIVATE beam-erlang::erts_headers)
    else()
        target_link_libraries(ethread PRIVATE beam-erlang::erts_headers Threads::Threads)
    endif()
    add_dependencies(ethread tables_${ERTS_BUILD_FLAVOR})
    target_include_directories(ethread PRIVATE
        ${CMAKE_BINARY_DIR}/erts
        ${CMAKE_BINARY_DIR}/erts/include
        ${CMAKE_BINARY_DIR}/erts/include/internal
        ${EMULATOR_GENERATED_DIR}
        ../include
        ../include/internal
        beam
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_include_directories(ethread PRIVATE
            sys/win32
            ../include/internal/win
        )
    else()
        target_include_directories(ethread PRIVATE sys/unix)
    endif()
    set_target_properties(ethread PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()
