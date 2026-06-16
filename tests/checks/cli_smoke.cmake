file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}/source with spaces")
set(_source "${TEST_DIR}/source with spaces/hello_world.erl")
file(WRITE "${_source}" [=[
-module(hello_world).
-export([main/1]).
main([]) -> io:format("Hello, World!~n"), erlang:halt(0);
main(Args) ->
    ["-eval", "erlang:halt(42).", "--", "\\--", "", "two words", "a'b", "+S", "日本語"] = Args,
    io:format("Arguments survived~n").
]=])

function(check expected_status expected_output)
    execute_process(COMMAND "${BEAM}" ${ARGN}
        WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE _status OUTPUT_VARIABLE _out ERROR_VARIABLE _err
        TIMEOUT 20)
    if(NOT _status STREQUAL "${expected_status}" OR
       NOT _out STREQUAL "${expected_output}")
        message(FATAL_ERROR "beam ${ARGN}: status=${_status}\nstdout=${_out}\nstderr=${_err}")
    endif()
endfunction()

# The command is exclusively modern; obsolete selectors fail explicitly.
foreach(_flag IN ITEMS --runtime=otp --runtime=beam --runtime)
    execute_process(COMMAND "${BEAM}" "${_flag}" eval "halt()."
        RESULT_VARIABLE _status OUTPUT_VARIABLE _out ERROR_VARIABLE _err TIMEOUT 20)
    if(NOT _status STREQUAL "1" OR NOT _err MATCHES "runtime selection is obsolete")
        message(FATAL_ERROR "obsolete selector did not fail clearly: ${_flag}: ${_err}")
    endif()
endforeach()
    check(0 "Hello, World!\n" run "${_source}")
    # Keep the empty argument explicit; expanding a CMake list would drop it.
    execute_process(COMMAND "${BEAM}" run "${_source}" --
        -eval "erlang:halt(42)." -- "\\--" "" "two words" "a'b" +S "日本語"
        RESULT_VARIABLE _status OUTPUT_VARIABLE _out ERROR_VARIABLE _err TIMEOUT 20)
    if(NOT _status STREQUAL "0" OR NOT _out STREQUAL "Arguments survived\n")
        message(FATAL_ERROR "argument preservation: status=${_status}\n${_out}\n${_err}")
    endif()
    check(0 "Eval works\n" eval "io:format(\"Eval works~n\").")
    check(7 "" eval "erlang:halt(7).")
    check(1 "" eval "erlang:error(expected_failure).")
    check(1 "" run)
    check(1 "" run "${_source}" unseparated)
    set(_missing_diagnostic "${TEST_DIR}/missing.erl")
    if(WIN32 AND _missing_diagnostic MATCHES "^[A-Z]:/")
        # The retained compiler's filename module canonicalizes drive letters.
        string(SUBSTRING "${_missing_diagnostic}" 0 1 _drive)
        string(TOLOWER "${_drive}" _drive)
        string(SUBSTRING "${_missing_diagnostic}" 1 -1 _tail)
        set(_missing_diagnostic "${_drive}${_tail}")
    endif()
    check(1 "${_missing_diagnostic}: no such file or directory\n"
        run "${TEST_DIR}/missing.erl")
    file(WRITE "${TEST_DIR}/no_main.erl" "-module(no_main).\n-export([f/0]).\nf() -> ok.\n")
    check(1 "" run "${TEST_DIR}/no_main.erl")
    file(WRITE "${TEST_DIR}/bad.erl" "-module(bad).\nthis is not Erlang.\n")
    execute_process(COMMAND "${BEAM}" run "${TEST_DIR}/bad.erl"
        RESULT_VARIABLE _status OUTPUT_QUIET ERROR_QUIET TIMEOUT 20)
    if(NOT _status STREQUAL "1")
        message(FATAL_ERROR "invalid source returned ${_status}")
    endif()
    file(GLOB_RECURSE _beams "${TEST_DIR}/*.beam")
    if(_beams)
        message(FATAL_ERROR "beam run wrote compiled modules: ${_beams}")
    endif()
