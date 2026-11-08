# Direct ordinary sources are compiled once with verified modern tooling.
file(MAKE_DIRECTORY "${OUTPUT_DIR}" "${ATTESTATION_DIR}")
set(_attest_args --root "${SOURCE_DIR}" --compiler "${BOOTSTRAP}" --sdk "${SDK}"
    --beam-directory "${OUTPUT_DIR}" --import-policy stdlib
    --resources ${RESOURCES} --direct-sources ${SOURCES})
set(_snapshot "${ATTESTATION_DIR}/beam_stdlib_modules.inputs.json")
execute_process(COMMAND "${PYTHON}" "${ATTEST}" ${_attest_args}
    --output "${_snapshot}" --capture-inputs RESULT_VARIABLE _status)
if(NOT _status EQUAL 0)
    message(FATAL_ERROR "Failed to capture standard-library inputs: ${_status}")
endif()
execute_process(COMMAND "${BOOTSTRAP}" --deterministic --modern-target
    -o "${OUTPUT_DIR}" -- ${SOURCES} RESULT_VARIABLE _status)
if(NOT _status EQUAL 0)
    message(FATAL_ERROR "Failed to compile standard-library modules: ${_status}")
endif()
execute_process(COMMAND "${PYTHON}" "${ATTEST}" ${_attest_args}
    --output "${ATTESTATION_DIR}/beam_stdlib_modules.json"
    --input-snapshot "${_snapshot}" RESULT_VARIABLE _status)
if(NOT _status EQUAL 0)
    message(FATAL_ERROR "Failed to attest standard-library bytecode: ${_status}")
endif()
file(REMOVE "${_snapshot}")
