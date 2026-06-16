/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_CLI_ETF_H
#define BEAM_CLI_ETF_H
#include <beam/beam.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define ETF_VERSION 131
#define ETF_SMALL_TUPLE 104
#define ETF_INTEGER 98
#define ETF_NIL 106
#define ETF_STRING 107
#define ETF_LIST 108
#define ETF_BINARY 109
#define ETF_ATOM_UTF8 118
#define ETF_SMALL_ATOM_UTF8 119

typedef struct byte_buffer {
    unsigned char *data;
    size_t size;
    size_t capacity;
} byte_buffer;

static int buffer_reserve(byte_buffer *buffer, size_t extra)
{
    unsigned char *grown;
    size_t needed = buffer->size + extra;
    size_t capacity = buffer->capacity;
    if (extra > (size_t)-1 - buffer->size)
        return 0;
    if (needed <= buffer->capacity)
        return 1;
    if (!capacity)
        capacity = 64;
    while (capacity < needed) {
        if (capacity > (size_t)-1 / 2)
            return 0;
        capacity *= 2;
    }
    grown = realloc(buffer->data, capacity);
    if (!grown)
        return 0;
    buffer->data = grown;
    buffer->capacity = capacity;
    return 1;
}

static int buffer_bytes(byte_buffer *buffer, const void *bytes, size_t size)
{
    if (!buffer_reserve(buffer, size))
        return 0;
    if (size)
        memcpy(buffer->data + buffer->size, bytes, size);
    buffer->size += size;
    return 1;
}

static int buffer_u8(byte_buffer *buffer, unsigned value)
{
    unsigned char byte = (unsigned char)value;
    return buffer_bytes(buffer, &byte, 1);
}

static int buffer_u16(byte_buffer *buffer, unsigned value)
{
    unsigned char bytes[2] = {(unsigned char)(value >> 8), (unsigned char)value};
    return buffer_bytes(buffer, bytes, 2);
}

static int buffer_u32(byte_buffer *buffer, unsigned value)
{
    unsigned char bytes[4] = {
        (unsigned char)(value >> 24), (unsigned char)(value >> 16),
        (unsigned char)(value >> 8), (unsigned char)value
    };
    return buffer_bytes(buffer, bytes, 4);
}

static int buffer_atom(byte_buffer *buffer, const char *name)
{
    size_t length = strlen(name);
    if (length < 256)
        return buffer_u8(buffer, ETF_SMALL_ATOM_UTF8) &&
               buffer_u8(buffer, (unsigned)length) && buffer_bytes(buffer, name, length);
    return length <= 1020 && buffer_u8(buffer, ETF_ATOM_UTF8) &&
           buffer_u16(buffer, (unsigned)length) && buffer_bytes(buffer, name, length);
}

static int buffer_binary(byte_buffer *buffer, const char *text)
{
    size_t length = strlen(text);
    return buffer_u8(buffer, ETF_BINARY) && buffer_u32(buffer, (unsigned)length) &&
           buffer_bytes(buffer, text, length);
}

static int buffer_charlist(byte_buffer *buffer, const char *text)
{
    size_t length = strlen(text);
    if (length > 65535)
        return 0;
    return buffer_u8(buffer, ETF_STRING) && buffer_u16(buffer, (unsigned)length) &&
           buffer_bytes(buffer, text, length);
}

typedef struct etf_cursor { const unsigned char *p, *end; } etf_cursor;
static int etf_tag(etf_cursor *c, unsigned tag)
{ return c->p < c->end && *c->p++ == tag; }
static int etf_u32(etf_cursor *c, size_t *n)
{
    if (c->end - c->p < 4) return 0;
    *n = ((size_t)c->p[0] << 24) | ((size_t)c->p[1] << 16) | ((size_t)c->p[2] << 8) | c->p[3];
    c->p += 4; return 1;
}
static int etf_tuple(etf_cursor *c, unsigned n)
{ return etf_tag(c, ETF_SMALL_TUPLE) && etf_tag(c, n); }
static int etf_list(etf_cursor *c, size_t *n)
{
    if (c->p < c->end && *c->p == ETF_NIL) { ++c->p; *n = 0; return 1; }
    /* Canonical empty lists use NIL_EXT. Reject an alternate zero-length
     * LIST_EXT so the caller can consume proper tails without ambiguity. */
    return etf_tag(c, ETF_LIST) && etf_u32(c, n) && *n != 0;
}
static int etf_binary(etf_cursor *c, beam_bytes *b)
{
    if (!etf_tag(c, ETF_BINARY) || !etf_u32(c, &b->size) || b->size > (size_t)(c->end-c->p)) return 0;
    b->data = c->p; c->p += b->size; return 1;
}
static char *etf_string(etf_cursor *c)
{
    beam_bytes b; char *s;
    if (!etf_binary(c, &b) || !b.size || b.size > 16384 || memchr(b.data, 0, b.size)) return NULL;
    s = malloc(b.size + 1);
    if (s) { memcpy(s, b.data, b.size); s[b.size] = 0; }
    return s;
}
static int etf_atom(etf_cursor *c, char *s, size_t capacity)
{
    size_t n;
    if (c->p == c->end) return 0;
    if (*c->p++ == ETF_SMALL_ATOM_UTF8) {
        if (c->p == c->end) return 0;
        n = *c->p++;
    } else {
        --c->p;
        if (!etf_tag(c, ETF_ATOM_UTF8) || c->end-c->p < 2) return 0;
        n = ((size_t)c->p[0] << 8) | c->p[1]; c->p += 2;
    }
    if (!n || n >= capacity || n > (size_t)(c->end-c->p) || memchr(c->p, 0, n)) return 0;
    memcpy(s, c->p, n); s[n] = 0; c->p += n; return 1;
}

#endif
