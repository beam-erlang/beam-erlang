/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RETURNING_ACCEPTANCE_SUPPORT_H
#define RETURNING_ACCEPTANCE_SUPPORT_H
#include <beam.h>
#include "etf.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define ETF_VERSION 131
#define ETF_SMALL_INTEGER 97
#define ETF_SMALL_TUPLE 104
#define ETF_NIL 106
#define ETF_LIST 108
#define ETF_BINARY 109
#define ETF_SMALL_ATOM_UTF8 119

typedef struct byte_buffer {
    unsigned char *data;
    size_t size;
    size_t capacity;
} byte_buffer;

typedef struct sink {
    unsigned char *data;
    size_t size;
    size_t capacity;
    const unsigned char *code;
    size_t code_size;
    const unsigned char *bad;
    size_t bad_size;
    int fail_next;
} sink;

void add_program(beam_return_module *modules, size_t *count, const char *fixture);
void check(int condition, const char *message);
void check_fds(const int *before, int before_count);
void code_mode(const char *root, const char *fixture);
void completion_mode(const char *root, const char *fixture);
void components_mode(const char *root, const char *fixture);
void counter_mode(const char *root, const char *fixture);
void echo_mode(const char *root, const char *fixture);
void expect_one_binary(const beam_outcome *outcome, const char *label, const char *expected);
void expect_return_text(const beam_outcome *outcome, const char *text);
void fail(const char *message);
void expect_exception_tag(const beam_outcome *outcome, const char *tag, const char *detail);
void file_mode(const char *root, const char *fixture, const char *work);
void greeting_mode(const char *root, const char *fixture);
void library_mode(const char *root, const char *fixture);
beam_bytes list_of_binaries(const unsigned char *const *items, const size_t *sizes, size_t count);
beam_return_module module_from_file(const char *path, const char *name, int service);
beam_code_status on_code(void *context, beam_bytes source, beam_bytes module,
                                const unsigned char **bytes, size_t *size);
beam_output_status on_output(void *context, beam_bytes destination, beam_bytes bytes);
void output_mode(const char *root, const char *fixture);
void pin_mode(const char *root, const char *fixture);
unsigned char *read_file(const char *path, size_t *size);
etf_reader read_term(beam_buffer term, unsigned tuple_arity);
void record_mode(const char *root, const char *fixture, const char *work);
void run_entry(const char *root, const char *function, beam_bytes arguments,
                      unsigned services, beam_return_module *modules, size_t count,
                      sink *output, beam_outcome *outcome);
void snapshot_fds(int *fds, int capacity, int *count);
void start_system(void);
void stop_system(void);
void zlib_mode(const char *root, const char *fixture);
extern beam_system *acceptance_system;
#endif
