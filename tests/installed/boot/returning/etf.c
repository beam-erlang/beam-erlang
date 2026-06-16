/* SPDX-License-Identifier: Apache-2.0 */
#include "etf.h"
#include <limits.h>
#include <string.h>

static int take(etf_reader *reader, size_t count, const unsigned char **bytes)
{
    if (!reader->valid || count > reader->size - reader->offset) {
        reader->valid = 0;
        return 0;
    }
    *bytes = reader->data + reader->offset;
    reader->offset += count;
    return 1;
}

static int require(etf_reader *reader, int condition)
{
    reader->valid = reader->valid && condition;
    return reader->valid;
}

static uint32_t u32(const unsigned char *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

int etf_start(etf_reader *reader, const unsigned char *data, size_t size)
{
    *reader = (etf_reader){data, size, 0, data != NULL && size > 0};
    const unsigned char *byte;
    return take(reader, 1, &byte) && require(reader, *byte == 131);
}

int etf_tuple(etf_reader *reader, uint32_t arity)
{
    const unsigned char *tag, *bytes;
    if (!take(reader, 1, &tag)) return 0;
    if (*tag == 104)
        return take(reader, 1, &bytes) && require(reader, *bytes == arity);
    return require(reader, *tag == 105) && take(reader, 4, &bytes) &&
           require(reader, u32(bytes) == arity);
}

int etf_list(etf_reader *reader, uint32_t length)
{
    const unsigned char *bytes;
    return take(reader, 5, &bytes) &&
           require(reader, bytes[0] == 108 && u32(bytes + 1) == length);
}

int etf_nil(etf_reader *reader)
{
    const unsigned char *byte;
    return take(reader, 1, &byte) && require(reader, *byte == 106);
}

int etf_integer(etf_reader *reader, int *value)
{
    const unsigned char *tag, *bytes;
    int64_t decoded;
    if (!take(reader, 1, &tag)) return 0;
    if (*tag == 97) {
        if (!take(reader, 1, &bytes)) return 0;
        decoded = *bytes;
    } else {
        if (!require(reader, *tag == 98) || !take(reader, 4, &bytes)) return 0;
        uint32_t encoded = u32(bytes);
        decoded = encoded <= INT32_MAX ? (int64_t)encoded : (int64_t)encoded - INT64_C(4294967296);
    }
    if (!require(reader, decoded >= INT_MIN && decoded <= INT_MAX)) return 0;
    *value = (int)decoded;
    return 1;
}

int etf_binary(etf_reader *reader, const unsigned char **data, size_t *size)
{
    const unsigned char *header, *bytes;
    if (!take(reader, 5, &header) || !require(reader, header[0] == 109)) return 0;
    size_t length = u32(header + 1);
    if (!take(reader, length, &bytes)) return 0;
    *data = bytes;
    *size = length;
    return 1;
}

int etf_atom(etf_reader *reader, const char *expected)
{
    const unsigned char *tag, *bytes;
    size_t length;
    if (!take(reader, 1, &tag)) return 0;
    if (*tag == 119) {
        if (!take(reader, 1, &bytes)) return 0;
        length = *bytes;
    } else {
        if (!require(reader, *tag == 118) || !take(reader, 2, &bytes)) return 0;
        length = ((size_t)bytes[0] << 8) | bytes[1];
    }
    return take(reader, length, &bytes) && require(reader,
           strlen(expected) == length && memcmp(bytes, expected, length) == 0);
}

static int skip_atom(etf_reader *reader)
{
    const unsigned char *tag, *bytes;
    size_t length;
    if (!take(reader, 1, &tag)) return 0;
    if (*tag == 119) {
        if (!take(reader, 1, &bytes)) return 0;
        length = *bytes;
    } else {
        if (!require(reader, *tag == 118) || !take(reader, 2, &bytes)) return 0;
        length = ((size_t)bytes[0] << 8) | bytes[1];
    }
    return take(reader, length, &bytes);
}

static int proper_list_size(etf_reader *reader, uint32_t maximum, uint32_t *length)
{
    const unsigned char *tag, *bytes;
    if (!take(reader, 1, &tag)) return 0;
    if (*tag == 106) {
        *length = 0;
        return 1;
    }
    if (!require(reader, *tag == 108) || !take(reader, 4, &bytes)) return 0;
    *length = u32(bytes);
    return require(reader, *length > 0 && *length <= maximum);
}

static int skip_filename(etf_reader *reader)
{
    const unsigned char *bytes;
    if (!reader->valid || reader->offset >= reader->size) return require(reader, 0);
    if (reader->data[reader->offset] == 107) {
        if (!take(reader, 3, &bytes)) return 0;
        size_t length = ((size_t)bytes[1] << 8) | bytes[2];
        return require(reader, length <= 4096) && take(reader, length, &bytes);
    }
    uint32_t length;
    if (!proper_list_size(reader, 4096, &length)) return 0;
    for (uint32_t i = 0; i < length; ++i) {
        int codepoint;
        if (!etf_integer(reader, &codepoint) ||
            !require(reader, codepoint >= 0 && codepoint <= 0x10ffff &&
                     !(codepoint >= 0xd800 && codepoint <= 0xdfff))) return 0;
    }
    return length == 0 || etf_nil(reader);
}

int etf_stacktrace(etf_reader *reader)
{
    uint32_t frames;
    if (!proper_list_size(reader, 64, &frames)) return 0;
    for (uint32_t i = 0; i < frames; ++i) {
        int arity;
        uint32_t locations;
        if (!etf_tuple(reader, 4) || !skip_atom(reader) || !skip_atom(reader) ||
            !etf_integer(reader, &arity) || !require(reader, arity >= 0) ||
            !proper_list_size(reader, 16, &locations)) return 0;
        for (uint32_t j = 0; j < locations; ++j) {
            if (!etf_tuple(reader, 2)) return 0;
            etf_reader key = *reader;
            if (etf_atom(&key, "file")) {
                *reader = key;
                if (!skip_filename(reader)) return 0;
            } else {
                int line;
                if (!etf_atom(reader, "line") || !etf_integer(reader, &line) ||
                    !require(reader, line > 0)) return 0;
            }
        }
        if (locations && !etf_nil(reader)) return 0;
    }
    return frames == 0 || etf_nil(reader);
}

int etf_done(const etf_reader *reader)
{
    return reader->valid && reader->offset == reader->size;
}
