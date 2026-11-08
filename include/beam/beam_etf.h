/*
 * %CopyrightBegin%
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright Ericsson AB 1998-2025. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * %CopyrightEnd%
 */
/* Host-memory ETF codec. Experimental exact-SDK API.
 * No VM/system initialization is required. Raw encoders require sufficient
 * output storage; decoders require complete, trusted readable ETF input.
 * Cursors are byte offsets, not bounds. A NULL encoder buffer measures output.
 * char/string operations represent bytes/byte lists, not Unicode charlists.
 * Bitstring views borrow the input. Decoded funs own allocations and may hold
 * pointers into their own module array: do not shallow-copy them; free them
 * with beam_etf_free_fun. Buffers and funs must use their matching release API.
 */
#ifndef BEAM_ETF_H
#define BEAM_ETF_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef long long BeamEtfLongLong;
typedef unsigned long long BeamEtfULongLong;
#define BEAM_ETF_MAXATOMLEN 256
#define BEAM_ETF_MAXATOMLEN_UTF8 1021
#define BEAM_ETF_VERSION_MAGIC 131
#define BEAM_ETF_SMALL_INTEGER_EXT 'a'
#define BEAM_ETF_INTEGER_EXT 'b'
#define BEAM_ETF_FLOAT_EXT 'c'
#define BEAM_ETF_NEW_FLOAT_EXT 'F'
#define BEAM_ETF_ATOM_EXT 'd'
#define BEAM_ETF_SMALL_ATOM_EXT 's'
#define BEAM_ETF_ATOM_UTF8_EXT 'v'
#define BEAM_ETF_SMALL_ATOM_UTF8_EXT 'w'
#define BEAM_ETF_REFERENCE_EXT 'e'
#define BEAM_ETF_NEW_REFERENCE_EXT 'r'
#define BEAM_ETF_NEWER_REFERENCE_EXT 'Z'
#define BEAM_ETF_PORT_EXT 'f'
#define BEAM_ETF_NEW_PORT_EXT 'Y'
#define BEAM_ETF_PID_EXT 'g'
#define BEAM_ETF_NEW_PID_EXT 'X'
#define BEAM_ETF_SMALL_TUPLE_EXT 'h'
#define BEAM_ETF_LARGE_TUPLE_EXT 'i'
#define BEAM_ETF_NIL_EXT 'j'
#define BEAM_ETF_STRING_EXT 'k'
#define BEAM_ETF_LIST_EXT 'l'
#define BEAM_ETF_BINARY_EXT 'm'
#define BEAM_ETF_BIT_BINARY_EXT 'M'
#define BEAM_ETF_SMALL_BIG_EXT 'n'
#define BEAM_ETF_LARGE_BIG_EXT 'o'
#define BEAM_ETF_NEW_FUN_EXT 'p'
#define BEAM_ETF_MAP_EXT 't'
#define BEAM_ETF_FUN_EXT 'u'
#define BEAM_ETF_EXPORT_EXT 'q'
#define BEAM_ETF_V4_PORT_EXT 'x'

/* a pid */
typedef struct {
  char node[BEAM_ETF_MAXATOMLEN_UTF8];
  unsigned int num;
  unsigned int serial;
  unsigned int creation;
} BeamEtfPid;

/* a port */
typedef struct {
  char node[BEAM_ETF_MAXATOMLEN_UTF8];
  BeamEtfULongLong id;
  unsigned int creation;
} BeamEtfPort;

/* a ref */
typedef struct {
  char node[BEAM_ETF_MAXATOMLEN_UTF8];
  int len;
  unsigned int n[5];
  unsigned int creation;
} BeamEtfRef;


/* a fun */
typedef struct {
    long arity;
    char module[BEAM_ETF_MAXATOMLEN_UTF8];
    enum { BEAM_ETF_FUN_CLOSURE, BEAM_ETF_FUN_EXPORT } type;
    union {
        struct {
            char md5[16];
            long index;
            long old_index;
            long uniq;
            long n_free_vars;
            BeamEtfPid pid;
            long free_var_len;
            char* free_vars;
        } closure;
        struct {
            char* func;
            int func_allocated;
        } exprt;
    } u;
} BeamEtfFun;


typedef struct BeamEtfBufferTag {
    char* buff;
    int buffsz;
    int index;
} BeamEtfBuffer;


int beam_etf_encode_version(char *buf, int *index);
int beam_etf_x_encode_version(BeamEtfBuffer* x);
int beam_etf_encode_long(char *buf, int *index, long p);
int beam_etf_x_encode_long(BeamEtfBuffer* x, long n);
int beam_etf_encode_ulong(char *buf, int *index, unsigned long p);
int beam_etf_x_encode_ulong(BeamEtfBuffer* x, unsigned long n);
int beam_etf_encode_double(char *buf, int *index, double p);
int beam_etf_x_encode_double(BeamEtfBuffer* x, double dbl);
int beam_etf_encode_boolean(char *buf, int *index, int p);
int beam_etf_x_encode_boolean(BeamEtfBuffer* x, int p);
int beam_etf_encode_char(char *buf, int *index, char p);
int beam_etf_x_encode_char(BeamEtfBuffer* x, char p);
int beam_etf_encode_string(char *buf, int *index, const char *p);
int beam_etf_encode_string_len(char *buf, int *index, const char *p, int len);
int beam_etf_x_encode_string(BeamEtfBuffer* x, const char* s);
int beam_etf_x_encode_string_len(BeamEtfBuffer* x, const char* s, int len);
/* Atoms use UTF-8. Encoding rejects overlong names rather than truncating. */
int beam_etf_encode_atom(char *buf, int *index, const char *p);
int beam_etf_encode_atom_len(char *buf, int *index, const char *p, int len);
int beam_etf_x_encode_atom(BeamEtfBuffer* x, const char* s);
int beam_etf_x_encode_atom_len(BeamEtfBuffer* x, const char* s, int len);
int beam_etf_encode_binary(char *buf, int *index, const void *p, long len);
int beam_etf_encode_bitstring(char *buf, int *index, const char *p, size_t bitoffs, size_t bits);
int beam_etf_x_encode_binary(BeamEtfBuffer* x, const void* s, int len);
int beam_etf_x_encode_bitstring(BeamEtfBuffer* x, const char* p, size_t bitoffs, size_t bits);
int beam_etf_encode_pid(char *buf, int *index, const BeamEtfPid *p);
int beam_etf_x_encode_pid(BeamEtfBuffer* x, const BeamEtfPid* pid);
int beam_etf_encode_fun(char* buf, int* index, const BeamEtfFun* p);
int beam_etf_x_encode_fun(BeamEtfBuffer* x, const BeamEtfFun* fun);
int beam_etf_encode_port(char *buf, int *index, const BeamEtfPort *p);
int beam_etf_x_encode_port(BeamEtfBuffer* x, const BeamEtfPort *p);
int beam_etf_encode_ref(char *buf, int *index, const BeamEtfRef *p);
int beam_etf_x_encode_ref(BeamEtfBuffer* x, const BeamEtfRef *p);
int beam_etf_encode_tuple_header(char *buf, int *index, int arity);
int beam_etf_x_encode_tuple_header(BeamEtfBuffer* x, long n);
int beam_etf_encode_list_header(char *buf, int *index, int arity);
int beam_etf_x_encode_list_header(BeamEtfBuffer* x, long n);
int beam_etf_x_encode_empty_list(BeamEtfBuffer* x);
int beam_etf_encode_map_header(char *buf, int *index, int arity);
int beam_etf_x_encode_map_header(BeamEtfBuffer* x, long n);
int beam_etf_get_type(const char *buf, const int *index, int *type, int *size);
int beam_etf_decode_version(const char *buf, int *index, int *version);
int beam_etf_decode_long(const char *buf, int *index, long *p);
int beam_etf_decode_ulong(const char *buf, int *index, unsigned long *p);
int beam_etf_decode_double(const char *buf, int *index, double *p);
int beam_etf_decode_boolean(const char *buf, int *index, int *p);
int beam_etf_decode_char(const char *buf, int *index, char *p);
int beam_etf_decode_string(const char *buf, int *index, char *p);
int beam_etf_decode_atom(const char *buf, int *index, char *p, int capacity);
int beam_etf_decode_binary(const char *buf, int *index, void *p, long *len);
int beam_etf_decode_bitstring(const char *buf, int *index, const char** pp,
                        unsigned int* bitoffsp, size_t *nbitsp);
int beam_etf_decode_fun(const char* buf, int* index, BeamEtfFun* p);
void beam_etf_free_fun(BeamEtfFun* f);
int beam_etf_decode_pid(const char *buf, int *index, BeamEtfPid *p);
int beam_etf_decode_port(const char *buf, int *index, BeamEtfPort *p);
int beam_etf_decode_ref(const char *buf, int *index, BeamEtfRef *p);
int beam_etf_decode_tuple_header(const char *buf, int *index, int *arity);
int beam_etf_decode_list_header(const char *buf, int *index, int *arity);
int beam_etf_decode_map_header(const char *buf, int *index, int *arity);
int beam_etf_decode_iodata(const char *buf, int* index, int *szp, char *out_buf);
int beam_etf_x_new(BeamEtfBuffer* x);
int beam_etf_x_new_with_version(BeamEtfBuffer* x);
int beam_etf_x_free(BeamEtfBuffer* x);
int beam_etf_x_append(BeamEtfBuffer* x, const BeamEtfBuffer* x2);
int beam_etf_x_append_buf(BeamEtfBuffer* x, const char* buf, int len);
int beam_etf_skip_term(const char* buf, int* index);
int beam_etf_cmp_refs(BeamEtfRef *a, BeamEtfRef *b);
int beam_etf_cmp_pids(BeamEtfPid *a, BeamEtfPid *b);
int beam_etf_cmp_ports(BeamEtfPort *a, BeamEtfPort *b);
int beam_etf_decode_longlong(const char *buf, int *index, BeamEtfLongLong *p);
int beam_etf_decode_ulonglong(const char *buf, int *index, BeamEtfULongLong *p);
int beam_etf_encode_longlong(char *buf, int *index, BeamEtfLongLong p);
int beam_etf_encode_ulonglong(char *buf, int *index, BeamEtfULongLong p);
int beam_etf_x_encode_longlong(BeamEtfBuffer* x, BeamEtfLongLong n);
int beam_etf_x_encode_ulonglong(BeamEtfBuffer* x, BeamEtfULongLong n);
#define beam_etf_encode_empty_list(buf, index) beam_etf_encode_list_header(buf, index, 0)
#ifdef __cplusplus
}
#endif
#endif
