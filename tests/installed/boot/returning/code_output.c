/* SPDX-License-Identifier: Apache-2.0 */
#include "support.h"

void greeting_mode(const char *root, const char *fixture)
{
    beam_return_module modules[2];
    size_t count = 0;
    sink first = {0}, second = {0};
    beam_outcome outcome = {0}, again = {0};
    const unsigned char name[] = "κόσμε";
    const unsigned char other[] = "ada";
    const unsigned char *items[] = {name};
    size_t sizes[] = {sizeof(name) - 1};
    const unsigned char *second_items[] = {other};
    size_t second_sizes[] = {3};
    beam_bytes arguments = list_of_binaries(items, sizes, 1);
    beam_bytes other_arguments = list_of_binaries(second_items, second_sizes, 1);
    const char expected[] = "Hello, κόσμε!\n";
    add_program(modules, &count, fixture);
    start_system();
    run_entry(root, "greeting", arguments, TEST_OUTPUT, modules, count, &first, &outcome);
    expect_return_text(&outcome, "κόσμε");
    check(first.size == strlen(expected) && memcmp(first.data, expected, first.size) == 0, "greeting bytes");
    run_entry(root, "greeting", other_arguments, TEST_OUTPUT, modules, count, &second, &again);
    check(second.size == strlen("Hello, ada!\n") && memcmp(second.data, "Hello, ada!\n", second.size) == 0,
          "second greeting");
    check(second.size < strlen(expected) || memcmp(second.data, expected, strlen(expected)) != 0,
          "leftover first greeting");
    {
        const unsigned char *copied = NULL;
        size_t copied_size = 0;
        etf_reader cursor = read_term(outcome.term, 0);
        check(etf_binary(&cursor, &copied, &copied_size), "greeting name");
        check(copied_size == sizeof(name) - 1 && memcmp(copied, name, copied_size) == 0,
              "copied first result");
        check(etf_done(&cursor), "copied result trailing bytes");
        printf("greeting-bytes ");
        fwrite(first.data, 1, first.size, stdout);
        printf("greeting-name ");
        fwrite(copied, 1, copied_size, stdout);
        printf("\ngreeting-second ");
        fwrite(second.data, 1, second.size, stdout);
        printf("greeting-leftover 0\n");
        printf("copied-first ");
        fwrite(copied, 1, copied_size, stdout);
        printf("\n");
    }
    beam_outcome_release(&outcome);
    beam_outcome_release(&again);
    stop_system();
    free(first.data);
    free(second.data);
    free((void *)arguments.data);
    free((void *)other_arguments.data);
    free((void *)modules[0].bytes.data);
    printf("greeting ok\n");
}

static void check_counter_result(const beam_outcome *outcome, int print_values)
{
    int values[3] = {0, 0, 0};
    etf_reader cursor = read_term(outcome->term, 3);
    check(outcome->kind == BEAM_OUTCOME_RETURN && !outcome->has_cleanup_error, "counter outcome");
    for (size_t index = 0; index < 3; ++index)
        check(etf_integer(&cursor, &values[index]), "counter integer");
    check(values[0] == 8 && values[1] == 7 && values[2] == 1, "counter values");
    check(etf_done(&cursor), "counter trailing bytes");
    if (print_values) printf("counter %d %d %d\n", values[0], values[1], values[2]);
}

void counter_mode(const char *root, const char *fixture)
{
    beam_return_module modules[2];
    size_t count = 0;
    sink output = {0};
    beam_outcome outcome = {0};
    const unsigned char *item = (const unsigned char *)"_";
    size_t size = 1;
    beam_bytes arguments = list_of_binaries(&item, &size, 1);
    add_program(modules, &count, fixture);
    start_system();
    run_entry(root, "counter", arguments, 0, modules, count, &output, &outcome);
    check_counter_result(&outcome, 1);
    beam_outcome_release(&outcome);
    const char *faults[] = {"crash", "stall"};
    for (size_t i = 0; i < 2; ++i) {
        item = (const unsigned char *)faults[i]; size = strlen(faults[i]);
        beam_bytes fault = list_of_binaries(&item, &size, 1);
        run_entry(root, "counter_fault", fault, 0, modules, count, &output, &outcome);
        expect_exception_tag(&outcome, i ? "worker_timeout" : "worker_failed",
                             i ? NULL : "deliberate_worker_failure");
        beam_outcome_release(&outcome);
        free((void *)fault.data);
        run_entry(root, "counter", arguments, 0, modules, count, &output, &outcome);
        check_counter_result(&outcome, 0);
        beam_outcome_release(&outcome);
        printf("counter-%s recovered\n", faults[i]);
    }
    stop_system();
    free((void *)arguments.data);
    free((void *)modules[0].bytes.data);
    printf("counter ok\n");
}

void code_mode(const char *root, const char *fixture)
{
    beam_return_module modules[4];
    size_t count = 0;
    sink output = {0};
    beam_outcome outcome = {0};
    char path[4096];
    const unsigned char dummy_text[] = "_";
    const unsigned char *dummy_items[] = {dummy_text};
    size_t dummy_sizes[] = {1};
    beam_bytes arguments = list_of_binaries(dummy_items, dummy_sizes, 1);
    add_program(modules, &count, fixture);
    snprintf(path, sizeof(path), "%s/kept.beam", fixture);
    modules[count++] = module_from_file(path, "kept", 0);
    snprintf(path, sizeof(path), "%s/uppercase_transform.beam", fixture);
    output.code = read_file(path, &output.code_size);
    output.bad = (const unsigned char *)"not-beam";
    output.bad_size = 8;
    start_system();
    run_entry(root, "code_client", arguments, TEST_CODE, modules, count, &output, &outcome);
    {
        const unsigned char *text = NULL;
        size_t text_size = 0;
        etf_reader cursor = read_term(outcome.term, 0);
        expect_return_text(&outcome, "BEAM");
        check(etf_binary(&cursor, &text, &text_size), "code binary");
        check(text_size == 4 && memcmp(text, "BEAM", 4) == 0, "code value");
        check(etf_done(&cursor), "code trailing bytes");
        printf("code %.*s\n", (int)text_size, text);
    }
    beam_outcome_release(&outcome);
    run_entry(root, "code_missing", arguments, TEST_CODE, modules, count, &output, &outcome);
    expect_return_text(&outcome, "still");
    beam_outcome_release(&outcome);
    run_entry(root, "code_bad", arguments, TEST_CODE, modules, count, &output, &outcome);
    expect_return_text(&outcome, "still");
    beam_outcome_release(&outcome);
    stop_system();
    free((void *)output.code);
    free((void *)arguments.data);
    free((void *)modules[0].bytes.data);
    free((void *)modules[1].bytes.data);
    printf("code ok\n");
}

void output_mode(const char *root, const char *fixture)
{
    beam_return_module modules[2];
    size_t count = 0;
    sink output = {0};
    beam_outcome outcome = {0};
    const unsigned char dummy_text[] = "_";
    const unsigned char *dummy_items[] = {dummy_text};
    size_t dummy_sizes[] = {1};
    beam_bytes arguments = list_of_binaries(dummy_items, dummy_sizes, 1);
    add_program(modules, &count, fixture);
    start_system();
    run_entry(root, "output_cases", arguments, TEST_OUTPUT, modules, count, &output, &outcome);
    check(outcome.kind == BEAM_OUTCOME_RETURN, "rejected write is not service failure");
    check(output.size == 5 && memcmp(output.data, "ready", 5) == 0, "acknowledged bytes");
    beam_outcome_release(&outcome);
    output.size = 0;
    run_entry(root, "omitted", arguments, 0, modules, count, &output, &outcome);
    check(outcome.kind == BEAM_OUTCOME_RETURN || outcome.kind == BEAM_OUTCOME_EXCEPTION, "omitted returned");
    check(outcome.kind != BEAM_OUTCOME_FAILURE, "omitted is not service failure");
    beam_outcome_release(&outcome);
    stop_system();
    free(output.data);
    free((void *)arguments.data);
    free((void *)modules[0].bytes.data);
    printf("output ok\n");
}
