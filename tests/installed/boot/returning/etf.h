/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ACCEPTANCE_ETF_H
#define ACCEPTANCE_ETF_H
#include <stddef.h>
#include <stdint.h>

/* A narrow reader for the shapes returned by acceptance fixtures. */
typedef struct etf_reader {
    const unsigned char *data;
    size_t size, offset;
    int valid;
} etf_reader;

int etf_start(etf_reader *reader, const unsigned char *data, size_t size);
int etf_tuple(etf_reader *reader, uint32_t arity);
int etf_list(etf_reader *reader, uint32_t length);
int etf_nil(etf_reader *reader);
int etf_integer(etf_reader *reader, int *value);
int etf_binary(etf_reader *reader, const unsigned char **data, size_t *size);
int etf_atom(etf_reader *reader, const char *expected);
/* Consume a bounded proper stacktrace with file/line frame locations. */
int etf_stacktrace(etf_reader *reader);
int etf_done(const etf_reader *reader);
#endif
