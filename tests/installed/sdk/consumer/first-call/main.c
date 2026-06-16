/* SPDX-License-Identifier: Apache-2.0 */
#include <beam.h>
#include <stdio.h>
#include <stdlib.h>

#include "answer_beam.h"

/* Report an API failure. This helper performs no VM operations. */
static int failed(beam_status status, const char *operation,
                  const beam_error *error)
{
    if (status == BEAM_OK)
        return 0;

    fprintf(stderr, "%s: %s", operation, beam_status_message(status));
    if (error && error->message[0])
        fprintf(stderr, ": %s", error->message);
    if (error && error->native_code)
        fprintf(stderr, " (native error %d)", error->native_code);
    fputc('\n', stderr);
    return 1;
}

int main(void)
{
    beam_system *system = NULL;
    beam_vm *vm = NULL;
    beam_vm_options options;
    beam_error error;
    beam_result result = {0};
    beam_vm_state_info state;
    int halt_status = 0;
    int exit_status = EXIT_FAILURE;
    unsigned answer = 0;

    /* ETF version byte, then NIL_EXT: an empty argument list for value/0. */
    const unsigned char no_arguments_data[] = {131, 106};
    const beam_bytes no_arguments = {
        no_arguments_data, sizeof no_arguments_data
    };

    beam_vm_options_init(&options);

    if (failed(beam_init(&system, &error), "beam_init", &error))
        goto cleanup;
    if (failed(beam_vm_init(system, &options, &vm, &error),
               "beam_vm_init", &error))
        goto cleanup;

    /* The VM has no workers yet. Loading copies these module bytes. */
    if (failed(beam_vm_load(vm, answer_beam), "beam_vm_load", NULL))
        goto cleanup;
    if (failed(beam_vm_start(vm, &error), "beam_vm_start", &error))
        goto cleanup;

    if (failed(beam_vm_call(vm, "answer", "value", no_arguments, &result),
               "beam_vm_call", NULL))
        goto cleanup;

    /* BEAM_OK describes the C operation. The Erlang outcome is separate. */
    if (result.kind == BEAM_EXCEPTION) {
        fputs("answer:value/0 raised an Erlang exception\n", stderr);
        goto cleanup;
    }
    if (result.kind == BEAM_HALT) {
        fprintf(stderr, "The VM requested halt status %d\n", result.halt_status);
        goto cleanup;
    }

    /* Decode only the expected ETF SMALL_INTEGER_EXT result, not arbitrary
     * Erlang terms: version 131, tag 97, then one unsigned byte. */
    if (result.kind != BEAM_RETURN || result.value.size != 3 ||
        !result.value.data || result.value.data[0] != 131 ||
        result.value.data[1] != 97 || result.value.data[2] != 42) {
        fputs("Expected answer:value/0 to return the integer 42\n", stderr);
        goto cleanup;
    }
    answer = result.value.data[2];
    beam_result_release(&result);

    if (failed(beam_vm_stop(vm, &halt_status), "beam_vm_stop", NULL))
        goto cleanup;
    if (failed(beam_vm_get_state(vm, &state), "beam_vm_get_state", NULL))
        goto cleanup;
    if (state.state != BEAM_VM_STOPPED || halt_status != 0 ||
        state.halt_status != 0) {
        fputs("Expected a stopped VM with halt status zero\n", stderr);
        goto cleanup;
    }
    exit_status = EXIT_SUCCESS;

cleanup:
    /* Release is also safe for a zero-initialized or already released result.
     * Termination stops the VM if a preceding operation failed before stop.
     * NULL handles make the same cleanup path work after partial setup. */
    beam_result_release(&result);
    if (failed(beam_vm_terminate(&vm, NULL), "beam_vm_terminate", NULL))
        exit_status = EXIT_FAILURE;
    if (failed(beam_terminate(&system), "beam_terminate", NULL))
        exit_status = EXIT_FAILURE;

    if (exit_status == EXIT_SUCCESS && printf("%u\n", answer) < 0)
        exit_status = EXIT_FAILURE;
    return exit_status;
}
