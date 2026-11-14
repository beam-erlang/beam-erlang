# Production SDK inspection assets are generated on the candidate VM using the
# retained bootstrap compiler. The embedded make_preload execution seeds stay
# separate from these augmented files and from the private full-test image.
set(BEAM_PRELOAD_INSPECTION_DIR "${CMAKE_BINARY_DIR}/sdk/preloaded/ebin")
file(GLOB _beam_inspection_seeds CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/erts/preloaded/ebin/*.beam")
set(_beam_inspection_outputs)
set(_beam_inspection_modules)
foreach(_beam_seed IN LISTS _beam_inspection_seeds)
    get_filename_component(_beam_module "${_beam_seed}" NAME_WE)
    list(APPEND _beam_inspection_modules "${_beam_module}")
    list(APPEND _beam_inspection_outputs "${BEAM_PRELOAD_INSPECTION_DIR}/${_beam_module}.beam")
endforeach()
file(GLOB_RECURSE _beam_inspection_inputs CONFIGURE_DEPENDS LIST_DIRECTORIES FALSE
    "${CMAKE_SOURCE_DIR}/erts/preloaded/src/*"
    "${CMAKE_SOURCE_DIR}/lib/kernel/src/*.hrl"
    "${CMAKE_SOURCE_DIR}/bootstrap/*")
set(_beam_inspection_driver "${CMAKE_CURRENT_LIST_DIR}/preload_inspection.erl")
string(REPLACE "\\" "\\\\" _beam_inspection_driver_erl "${_beam_inspection_driver}")
string(REPLACE "\"" "\\\"" _beam_inspection_driver_erl "${_beam_inspection_driver_erl}")
set(_beam_inspection_eval "try {ok,preload_inspection,B}=compile:noenv_file(\"${_beam_inspection_driver_erl}\",[binary,deterministic]),{module,preload_inspection}=code:load_binary(preload_inspection,\"preload_inspection.erl\",B),ok=preload_inspection:main(init:get_plain_arguments()),halt(0) catch C:R:S -> io:format(standard_error,\"preload inspection failed: ~tp~n\",[{C,R,S}]),halt(1) end.")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/sdk")
file(WRITE "${CMAKE_BINARY_DIR}/sdk/inspection.inetrc" "{lookup,[file]}.\n{domain,\"beam.test\"}.\n")
set(_beam_inspection_helper_dependencies)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
    list(APPEND _beam_inspection_helper_dependencies erl_child_setup)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND _beam_inspection_helper_dependencies "${CMAKE_BINARY_DIR}/bin/erl_child_setup.debug")
    endif()
endif()
add_custom_command(OUTPUT ${_beam_inspection_outputs}
    COMMAND "${CMAKE_COMMAND}" -E env "BINDIR=$<TARGET_FILE_DIR:beam>"
        "ERL_INETRC=${CMAKE_BINARY_DIR}/sdk/inspection.inetrc"
        "ERL_CRASH_DUMP=${CMAKE_BINARY_DIR}/sdk/inspection.crash.dump"
        --unset=ERL_FLAGS --unset=ERL_AFLAGS --unset=ERL_ZFLAGS --unset=ERL_LIBS
        --unset=ERL_COMPILER_OPTIONS
        "$<TARGET_FILE:beam>" -S 2:2 -SDcpu 1 -SDio 1 --
        -root "${CMAKE_SOURCE_DIR}/bootstrap" -bindir "$<TARGET_FILE_DIR:beam>"
        -progname beam -boot "${CMAKE_SOURCE_DIR}/bootstrap/bin/start_clean"
        -noshell -noinput -no_dot_erlang -eval "${_beam_inspection_eval}"
        -extra "${CMAKE_SOURCE_DIR}" "${BEAM_PRELOAD_INSPECTION_DIR}" ${_beam_inspection_modules}
    DEPENDS beam ${_beam_inspection_helper_dependencies} ${_beam_inspection_seeds}
        ${_beam_inspection_inputs} "${_beam_inspection_driver}"
    COMMENT "Generating complete SDK preload inspection metadata" VERBATIM)
add_custom_target(beam_preload_inspection ALL DEPENDS ${_beam_inspection_outputs})
