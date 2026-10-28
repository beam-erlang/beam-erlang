set(_answer_dir "${CMAKE_BINARY_DIR}/target-acceptance")
add_custom_command(OUTPUT "${_answer_dir}/answer.beam"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_answer_dir}"
    COMMAND $<TARGET_FILE:beam_bootstrap> --deterministic -o "${_answer_dir}" --
        "${CMAKE_SOURCE_DIR}/tests/installed/sdk/consumer/first-call/answer.erl"
    DEPENDS beam_bootstrap "${CMAKE_SOURCE_DIR}/tests/installed/sdk/consumer/first-call/answer.erl" VERBATIM)
add_custom_command(OUTPUT "${_answer_dir}/answer_beam.h"
    COMMAND ${CMAKE_COMMAND} "-DINPUT=${_answer_dir}/answer.beam" "-DOUTPUT=${_answer_dir}/answer_beam.h"
        -P "${CMAKE_SOURCE_DIR}/tests/installed/sdk/consumer/first-call/EmbedBytes.cmake"
    DEPENDS "${_answer_dir}/answer.beam" VERBATIM)
add_custom_command(OUTPUT "${_answer_dir}/entry.beam"
    COMMAND $<TARGET_FILE:beam_bootstrap> --deterministic -o "${_answer_dir}" --
        "${CMAKE_SOURCE_DIR}/tests/installed/sdk/deployment/entry.erl"
    DEPENDS beam_bootstrap "${CMAKE_SOURCE_DIR}/tests/installed/sdk/deployment/entry.erl" VERBATIM)
add_custom_command(OUTPUT "${_answer_dir}/entry.h"
    COMMAND ${CMAKE_COMMAND} "-DINPUT=${_answer_dir}/entry.beam" "-DOUTPUT=${_answer_dir}/entry.h"
        -P "${CMAKE_SOURCE_DIR}/tests/installed/sdk/deployment/Embed.cmake"
    DEPENDS "${_answer_dir}/entry.beam" VERBATIM)
add_custom_target(beam_target_acceptance_inputs ALL
    DEPENDS "${_answer_dir}/answer_beam.h" "${_answer_dir}/entry.h")
add_test(NAME target-sdk-layout COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tests/target/layout.py" --build "${CMAKE_BINARY_DIR}")
set_tests_properties(target-sdk-layout PROPERTIES LABELS "host;target-build")
