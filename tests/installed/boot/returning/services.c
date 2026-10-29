/* SPDX-License-Identifier: Apache-2.0 */
#include "support.h"
#include "native_counts.h"
#include <unistd.h>
#ifdef __APPLE__
#include <sys/resource.h>
#include <signal.h>
#endif

void file_mode(const char *root, const char *fixture, const char *work)
{
    beam_return_module modules[2];
    size_t count = 0;
    sink output = {0};
    beam_outcome outcome = {0};
    char input[4096], report[4096];
    unsigned char *report_bytes;
    size_t report_size;
    const unsigned char *items[2];
    size_t sizes[2];
    beam_bytes arguments;
    int before = beam_file_descriptor_count();
    snprintf(input, sizeof(input), "%s/input.txt", work);
    snprintf(report, sizeof(report), "%s/report.txt", work);
    {
        FILE *file = fopen(input, "wb");
        check(file && fwrite("alpha\nbeta\n", 1, 11, file) == 11, "input");
        fclose(file);
    }
    items[0] = (const unsigned char *)input;
    items[1] = (const unsigned char *)report;
    sizes[0] = strlen(input);
    sizes[1] = strlen(report);
    arguments = list_of_binaries(items, sizes, 2);
    add_program(modules, &count, fixture);
    start_system();
    run_entry(root, "file_report", arguments, TEST_FILE, modules, count, &output, &outcome);
    {
        int lines = 0, bytes = 0;
        etf_reader cursor = read_term(outcome.term, 2);
        check(outcome.kind == BEAM_OUTCOME_RETURN, "file kind");
        check(etf_integer(&cursor, &lines) && etf_integer(&cursor, &bytes),
              "file counts");
        check(lines == 2 && bytes == 11 && etf_done(&cursor), "file count values");
        report_bytes = read_file(report, &report_size);
        check(report_bytes && report_size == strlen("lines=2 bytes=11\n") &&
              memcmp(report_bytes, "lines=2 bytes=11\n", report_size) == 0, "report bytes");
        check(beam_file_descriptor_count() == before, "descriptor closed");
        printf("file-bytes %.*s", (int)report_size, report_bytes);
        printf("file-counts %d %d\n", lines, bytes);
        printf("file-descriptors %d\n", beam_file_descriptor_count() - before);
    }
    free(report_bytes);
    beam_outcome_release(&outcome);
    {
        char missing[4096];
        const unsigned char *missing_item;
        size_t missing_size;
        beam_bytes missing_args;
        snprintf(missing, sizeof(missing), "%s/missing-input", work);
        missing_item = (const unsigned char *)missing;
        missing_size = strlen(missing);
        missing_args = list_of_binaries(&missing_item, &missing_size, 1);
        const char *full = "/dev/full";
#ifdef __APPLE__
        /* Darwin has no /dev/full. A zero file-size limit gives a real failed
         * write without depending on a particular mounted filesystem. */
        char full_path[4096];
        snprintf(full_path, sizeof(full_path), "%s/full-output", work);
        full = full_path;
        struct rlimit saved_limit, limited;
        struct sigaction saved_signal, ignored_signal = {0};
        check(!getrlimit(RLIMIT_FSIZE, &saved_limit), "file-size limit");
        limited = saved_limit; limited.rlim_cur = 0;
        ignored_signal.sa_handler = SIG_IGN;
        sigemptyset(&ignored_signal.sa_mask);
        check(!sigaction(SIGXFSZ, &ignored_signal, &saved_signal), "file-size signal");
        check(!setrlimit(RLIMIT_FSIZE, &limited), "limit failed-write fixture");
#endif
        const unsigned char *edge_items[] = {missing_item, (const unsigned char *)full};
        size_t edge_sizes[] = {missing_size, strlen(full)};
        beam_bytes edge_args = list_of_binaries(edge_items, edge_sizes, 2);
        run_entry(root, "file_edges", edge_args, TEST_FILE, modules, count, &output, &outcome);
        free((void *)edge_args.data);
#ifdef __APPLE__
        check(!setrlimit(RLIMIT_FSIZE, &saved_limit), "restore file-size limit");
        check(!sigaction(SIGXFSZ, &saved_signal, NULL), "restore file-size signal");
        check(!unlink(full), "remove failed-write fixture");
#endif
        {
            const char *expected_kinds[] = {"not_found", "invalid_data", "io", "not_owner"};
            const char *labels[] = {"file-missing", "file-invalid", "file-write", "file-non-owner"};
            size_t index, text_size = 0;
            etf_reader cursor = read_term(outcome.term, 4);
            const unsigned char *text = NULL;
            check(outcome.kind == BEAM_OUTCOME_RETURN, "file edges kind");
            for (index = 0; index < 4; ++index) {
                check(etf_binary(&cursor, &text, &text_size), labels[index]);
                check(text_size == strlen(expected_kinds[index]) &&
                      memcmp(text, expected_kinds[index], text_size) == 0, labels[index]);
                printf("%s %.*s\n", labels[index], (int)text_size, text);
            }
            check(etf_done(&cursor), "file errors trailing bytes");
        }
        beam_outcome_release(&outcome);
        run_entry(root, "file_omitted", missing_args, 0, modules, count, &output, &outcome);
        {
            size_t text_size = 0;
            etf_reader cursor = read_term(outcome.term, 0);
            const unsigned char *text = NULL;
            check(outcome.kind == BEAM_OUTCOME_RETURN, "file omitted kind");
            check(etf_binary(&cursor, &text, &text_size), "file omitted binary");
            check(text_size == strlen("undef") && memcmp(text, "undef", text_size) == 0,
                  "file omitted value");
            check(etf_done(&cursor), "file omitted trailing bytes");
            printf("file-omitted %.*s\n", (int)text_size, text);
        }
        beam_outcome_release(&outcome);
        run_entry(root, "file_owner_death", missing_args, TEST_FILE, modules, count, &output, &outcome);
        check(outcome.kind == BEAM_OUTCOME_FAILURE, "file owner death");
        check(strcmp(outcome.origin, "file") == 0, "file owner service");
        printf("file-owner-death %s\n", outcome.origin);
        beam_outcome_release(&outcome);
        free((void *)missing_args.data);
    }
    {
        char rejected[4096];
        const unsigned char dummy_text[] = "_";
        const unsigned char *dummy_items[] = {dummy_text};
        size_t dummy_sizes[] = {1};
        beam_bytes dummy_args = list_of_binaries(dummy_items, dummy_sizes, 1);
        snprintf(rejected, sizeof(rejected), "%s/onload_rejected.beam", fixture);
        modules[count++] = module_from_file(rejected, "onload_rejected", 1);
        run_entry(root, "ret", dummy_args, TEST_FILE, modules, count, &output, &outcome);
        check(outcome.kind == BEAM_OUTCOME_FAILURE, "file activation");
        check(strcmp(outcome.origin, "onload_rejected") == 0, "file activation service");
        check(strcmp(outcome.origin, "still_loaded") != 0, "file activation committed");
        printf("file-activation %s\n", outcome.origin);
        beam_outcome_release(&outcome);
        free((void *)modules[count - 1].bytes.data);
        count--;
        free((void *)dummy_args.data);
    }
    stop_system();
    free((void *)arguments.data);
    free((void *)modules[0].bytes.data);
    printf("file ok\n");
}

void echo_mode(const char *root, const char *fixture)
{
    beam_return_module modules[2];
    size_t count = 0;
    sink output = {0};
    beam_outcome outcome = {0};
    const unsigned char dummy_text[] = "_";
    const unsigned char *dummy_items[] = {dummy_text};
    size_t dummy_sizes[] = {1};
    beam_bytes arguments = list_of_binaries(dummy_items, dummy_sizes, 1);
    int before = beam_socket_count();
    add_program(modules, &count, fixture);
    start_system();
    run_entry(root, "echo", arguments, TEST_SOCKET, modules, count, &output, &outcome);
    if (outcome.kind != BEAM_OUTCOME_RETURN) {
        fprintf(stderr, "echo kind %d service %s\n", (int)outcome.kind, outcome.origin);
        if (outcome.term.data)
            fwrite(outcome.term.data, 1, outcome.term.size, stderr);
        fputc('\n', stderr);
        fail("echo kind");
    }
    {
        int frames = 0, total = 0;
        const unsigned char *frame = NULL;
        size_t frame_size = 0, index;
        etf_reader cursor = read_term(outcome.term, 3);
        const char *expected_frames[] = {"one", "two", "three"};
        check(etf_integer(&cursor, &frames) && etf_integer(&cursor, &total),
              "echo counts");
        check(frames == 3 && total == 11, "echo count values");
        check(etf_list(&cursor, 3), "echo frame list");
        printf("echo-frames");
        for (index = 0; index < 3; ++index) {
            check(etf_binary(&cursor, &frame, &frame_size), "echo frame");
            check(frame_size == strlen(expected_frames[index]) &&
                  memcmp(frame, expected_frames[index], frame_size) == 0, "echo frame bytes");
            printf(" %.*s", (int)frame_size, frame);
        }
        check(etf_nil(&cursor) && etf_done(&cursor), "echo proper list and trailing bytes");
        printf("\necho-counts %d %d\n", frames, total);
    }
    check(beam_socket_count() == before, "socket closed");
    beam_outcome_release(&outcome);
    run_entry(root, "echo_partial", arguments, TEST_SOCKET, modules, count, &output, &outcome);
    expect_one_binary(&outcome, "echo-partial", "part");
    check(beam_socket_count() == before, "partial socket closed");
    beam_outcome_release(&outcome);
    run_entry(root, "echo_peer", arguments, TEST_SOCKET, modules, count, &output, &outcome);
    expect_one_binary(&outcome, "echo-peer", "PEER_EOF");
    check(beam_socket_count() == before, "peer socket closed");
    beam_outcome_release(&outcome);
    const char *faults[] = {"header", "body", "empty-body", "oversized", "stall", "crash"};
    const char *reasons[] = {"truncated_header", "truncated_body", "truncated_body",
                             "oversized_frame", "socket_timeout", "client_failed"};
    for (size_t i = 0; i < sizeof faults / sizeof faults[0]; ++i) {
        const unsigned char *item = (const unsigned char *)faults[i];
        size_t size = strlen(faults[i]);
        beam_bytes fault = list_of_binaries(&item, &size, 1);
        run_entry(root, "echo_fault", fault, TEST_SOCKET, modules, count, &output, &outcome);
        expect_exception_tag(&outcome, reasons[i],
                             i == 5 ? "deliberate_client_failure" : NULL);
        check(beam_socket_count() == before, "fault socket cleanup");
        beam_outcome_release(&outcome);
        free((void *)fault.data);
        run_entry(root, "echo_partial", arguments, TEST_SOCKET, modules, count, &output, &outcome);
        expect_one_binary(&outcome, "echo-recovery", "part");
        check(!outcome.has_cleanup_error && beam_socket_count() == before, "fresh echo cleanup");
        beam_outcome_release(&outcome);
        printf("echo-%s recovered\n", faults[i]);
    }
    run_entry(root, "echo_abandon", arguments, TEST_SOCKET, modules, count, &output, &outcome);
    {
        int port = 0;
        etf_reader reader = read_term(outcome.term, 0);
        check(outcome.kind == BEAM_OUTCOME_RETURN && !outcome.has_cleanup_error, "abandoned listener outcome");
        check(etf_integer(&reader, &port) && port > 0 && port <= 65535 && etf_done(&reader),
              "abandoned listener bound port");
        printf("echo-abandoned-port %d\n", port);
    }
    check(beam_socket_count() == before, "abandoned listener cleanup");
    beam_outcome_release(&outcome);
    run_entry(root, "echo_partial", arguments, TEST_SOCKET, modules, count, &output, &outcome);
    expect_one_binary(&outcome, "echo-abandoned-recovery", "part");
    check(!outcome.has_cleanup_error && beam_socket_count() == before, "fresh abandoned listener cleanup");
    beam_outcome_release(&outcome);
    run_entry(root, "echo_owner", arguments, TEST_SOCKET, modules, count, &output, &outcome);
    check(outcome.kind == BEAM_OUTCOME_FAILURE, "socket owner death");
    check(strcmp(outcome.origin, "socket") == 0, "socket owner service");
    printf("echo-owner-death %s\n", outcome.origin);
    check(beam_socket_count() == before, "owner socket closed");
    beam_outcome_release(&outcome);
    stop_system();
    free((void *)arguments.data);
    free((void *)modules[0].bytes.data);
    printf("echo ok\n");
}

static void check_record_result(const beam_outcome *outcome)
{
    {
        const unsigned char *text = NULL;
        size_t text_size = 0;
        etf_reader cursor = read_term(outcome->term, 6);
        int records = 0, restarts = 0;
        check(outcome->kind == BEAM_OUTCOME_RETURN, "record kind");
        check(etf_binary(&cursor, &text, &text_size) && text_size == 5 &&
              memcmp(text, "alpha", 5) == 0, "record alpha");
        printf("record-alpha %.*s\n", (int)text_size, text);
        check(etf_binary(&cursor, &text, &text_size) && text_size == 5 &&
              memcmp(text, "alpha", 5) == 0, "reconstructed alpha");
        check(etf_binary(&cursor, &text, &text_size) && text_size == 4 &&
              memcmp(text, "beta", 4) == 0, "record beta");
        printf("record-beta %.*s\n", (int)text_size, text);
        check(etf_integer(&cursor, &records) && records == 2, "record count");
        check(etf_integer(&cursor, &restarts) && restarts == 1, "restart count");
        printf("record-counts %d %d\n", records, restarts);
        check(etf_atom(&cursor, "interrupted") && etf_done(&cursor), "interrupted request");
    }
    check(!outcome->has_cleanup_error, "record cleanup");
}

void record_mode(const char *root, const char *fixture, const char *work)
{
    beam_return_module modules[2];
    size_t count = 0;
    sink output = {0};
    beam_outcome outcome = {0};
    char path[4096];
    const unsigned char *items[1];
    size_t sizes[1];
    beam_bytes arguments;
    int before = beam_file_descriptor_count();
    snprintf(path, sizeof(path), "%s/records.txt", work);
    check(unlink(path) == 0 || errno == ENOENT, "remove prior records");
    items[0] = (const unsigned char *)path;
    sizes[0] = strlen(path);
    arguments = list_of_binaries(items, sizes, 1);
    add_program(modules, &count, fixture);
    start_system();
    run_entry(root, "record_service", arguments, TEST_FILE, modules, count, &output, &outcome);
    check_record_result(&outcome);
    beam_outcome_release(&outcome);
    check(beam_file_descriptor_count() == before, "record descriptors closed");
    const char *faults[] = {"crash", "stall"};
    for (size_t i = 0; i < 2; ++i) {
        const unsigned char *item = (const unsigned char *)faults[i];
        size_t size = strlen(faults[i]);
        beam_bytes fault = list_of_binaries(&item, &size, 1);
        run_entry(root, "record_fault", fault, TEST_FILE, modules, count, &output, &outcome);
        expect_exception_tag(&outcome, i ? "worker_timeout" : "worker_failed",
                             i ? NULL : "deliberate_worker_failure");
        check(beam_file_descriptor_count() == before, "failed record descriptors closed");
        beam_outcome_release(&outcome);
        free((void *)fault.data);
    }
    const char sentinel[] = "preserve unreadable record bytes\n";
    FILE *file = fopen(path, "wb");
    check(file && fwrite(sentinel, 1, sizeof sentinel - 1, file) == sizeof sentinel - 1, "record sentinel");
    check(fclose(file) == 0 && chmod(path, 0200) == 0, "make records write-only");
    check(access(path, W_OK) == 0, "records remain writable");
    file = fopen(path, "rb");
    check(file == NULL && (errno == EACCES || errno == EPERM),
          "record permission check requires an unprivileged user");
    run_entry(root, "record_unreadable", arguments, TEST_FILE, modules, count, &output, &outcome);
    expect_exception_tag(&outcome, "record_permission_denied", NULL);
    check(beam_file_descriptor_count() == before, "unreadable record descriptors closed");
    beam_outcome_release(&outcome);
    check(chmod(path, 0600) == 0, "restore record permissions");
    size_t preserved_size = 0;
    unsigned char *preserved = read_file(path, &preserved_size);
    check(preserved_size == sizeof sentinel - 1 &&
          memcmp(preserved, sentinel, preserved_size) == 0, "unreadable records preserved");
    free(preserved);
    check(unlink(path) == 0, "remove sentinel");
    run_entry(root, "record_service", arguments, TEST_FILE, modules, count, &output, &outcome);
    check_record_result(&outcome);
    check(beam_file_descriptor_count() == before, "fresh record descriptors closed");
    beam_outcome_release(&outcome);
    printf("record-unreadable preserved and recovered\n");
    stop_system();
    free((void *)arguments.data);
    free((void *)modules[0].bytes.data);
    printf("record ok\n");
}

void zlib_mode(const char *root, const char *fixture)
{
    beam_return_module modules[2];
    size_t count = 0;
    sink output = {0};
    beam_outcome outcome = {0};
    const unsigned char dummy_text[] = "_";
    const unsigned char *dummy_items[] = {dummy_text};
    size_t dummy_sizes[] = {1};
    beam_bytes arguments = list_of_binaries(dummy_items, dummy_sizes, 1);
    int before = beam_zlib_stream_count();
    add_program(modules, &count, fixture);
    start_system();
    run_entry(root, "zlib_explicit", arguments, TEST_ZLIB, modules, count, &output, &outcome);
    check(outcome.kind == BEAM_OUTCOME_RETURN, "zlib explicit");
    check(beam_zlib_stream_count() == before, "explicit stream closed");
    beam_outcome_release(&outcome);
    run_entry(root, "zlib_abandon", arguments, TEST_ZLIB, modules, count, &output, &outcome);
    check(outcome.kind == BEAM_OUTCOME_RETURN, "zlib abandon");
    check(beam_zlib_stream_count() == before, "abandoned stream closed");
    beam_outcome_release(&outcome);
    run_entry(root, "zlib_persist", arguments, TEST_ZLIB, modules, count, &output, &outcome);
    check(outcome.kind == BEAM_OUTCOME_RETURN, "zlib persist");
    check(beam_zlib_stream_count() == before, "persistent stream closed");
    beam_outcome_release(&outcome);
    run_entry(root, "zlib_owner", arguments, TEST_ZLIB, modules, count, &output, &outcome);
    check(outcome.kind == BEAM_OUTCOME_FAILURE, "zlib owner death");
    check(strcmp(outcome.origin, "zlib") == 0, "zlib owner service");
    printf("zlib-owner-death %s\n", outcome.origin);
    beam_outcome_release(&outcome);
    {
        char rejected[4096];
        snprintf(rejected, sizeof(rejected), "%s/onload_rejected.beam", fixture);
        modules[count++] = module_from_file(rejected, "onload_rejected", 1);
        run_entry(root, "ret", arguments, 0, modules, count, &output, &outcome);
        check(outcome.kind == BEAM_OUTCOME_FAILURE, "onload failure");
        check(strcmp(outcome.origin, "onload_rejected") == 0, "onload rolled back");
        check(strcmp(outcome.origin, "still_loaded") != 0, "onload left committed");
        printf("onload-rollback %s\n", outcome.origin);
        printf("onload-not-committed %s\n", outcome.origin);
        beam_outcome_release(&outcome);
        free((void *)modules[count - 1].bytes.data);
    }
    stop_system();
    free((void *)arguments.data);
    free((void *)modules[0].bytes.data);
    printf("zlib ok\n");
}
