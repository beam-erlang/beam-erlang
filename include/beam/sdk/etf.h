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
/* Exact-SDK codec incorporation interface. Link only with BEAM::ETFObjects.
 * This is compatibility support, not the modern runtime API. No ei.h dependency.
 */
#ifndef BEAM_SDK_ETF_H
#define BEAM_SDK_ETF_H
#include <beam/beam_etf.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BEAM_ETF_SDK_API_VERSION 1
typedef enum {
    BEAM_ETF_COMPAT_ASCII = 1, BEAM_ETF_COMPAT_LATIN1 = 2,
    BEAM_ETF_COMPAT_UTF8 = 4
} BeamEtfCompatCharEncoding;
typedef struct {
    unsigned int arity;
    int is_neg;
    void *digits;
} BeamEtfCompatBig;

int beam_etf_embed_encode_version(char *buf, int *index);
int beam_etf_embed_x_encode_version(BeamEtfBuffer* x);
int beam_etf_embed_encode_long(char *buf, int *index, long p);
int beam_etf_embed_x_encode_long(BeamEtfBuffer* x, long n);
int beam_etf_embed_encode_ulong(char *buf, int *index, unsigned long p);
int beam_etf_embed_x_encode_ulong(BeamEtfBuffer* x, unsigned long n);
int beam_etf_embed_encode_double(char *buf, int *index, double p);
int beam_etf_embed_x_encode_double(BeamEtfBuffer* x, double dbl);
int beam_etf_embed_encode_boolean(char *buf, int *index, int p);
int beam_etf_embed_x_encode_boolean(BeamEtfBuffer* x, int p);
int beam_etf_embed_encode_char(char *buf, int *index, char p);
int beam_etf_embed_x_encode_char(BeamEtfBuffer* x, char p);
int beam_etf_embed_encode_string(char *buf, int *index, const char *p);
int beam_etf_embed_encode_string_len(char *buf, int *index, const char *p, int len);
int beam_etf_embed_x_encode_string(BeamEtfBuffer* x, const char* s);
int beam_etf_embed_x_encode_string_len(BeamEtfBuffer* x, const char* s, int len);
int beam_etf_embed_encode_atom(char *buf, int *index, const char *p);
int beam_etf_embed_encode_atom_len(char *buf, int *index, const char *p, int len);
int beam_etf_embed_x_encode_atom(BeamEtfBuffer* x, const char* s);
int beam_etf_embed_x_encode_atom_len(BeamEtfBuffer* x, const char* s, int len);
int beam_etf_embed_encode_binary(char *buf, int *index, const void *p, long len);
int beam_etf_embed_encode_bitstring(char *buf, int *index, const char *p, size_t bitoffs, size_t bits);
int beam_etf_embed_x_encode_binary(BeamEtfBuffer* x, const void* s, int len);
int beam_etf_embed_x_encode_bitstring(BeamEtfBuffer* x, const char* p, size_t bitoffs, size_t bits);
int beam_etf_embed_encode_pid(char *buf, int *index, const BeamEtfPid *p);
int beam_etf_embed_x_encode_pid(BeamEtfBuffer* x, const BeamEtfPid* pid);
int beam_etf_embed_encode_fun(char* buf, int* index, const BeamEtfFun* p);
int beam_etf_embed_x_encode_fun(BeamEtfBuffer* x, const BeamEtfFun* fun);
int beam_etf_embed_encode_port(char *buf, int *index, const BeamEtfPort *p);
int beam_etf_embed_x_encode_port(BeamEtfBuffer* x, const BeamEtfPort *p);
int beam_etf_embed_encode_ref(char *buf, int *index, const BeamEtfRef *p);
int beam_etf_embed_x_encode_ref(BeamEtfBuffer* x, const BeamEtfRef *p);
int beam_etf_embed_encode_tuple_header(char *buf, int *index, int arity);
int beam_etf_embed_x_encode_tuple_header(BeamEtfBuffer* x, long n);
int beam_etf_embed_encode_list_header(char *buf, int *index, int arity);
int beam_etf_embed_x_encode_list_header(BeamEtfBuffer* x, long n);
int beam_etf_embed_x_encode_empty_list(BeamEtfBuffer* x);
int beam_etf_embed_encode_map_header(char *buf, int *index, int arity);
int beam_etf_embed_x_encode_map_header(BeamEtfBuffer* x, long n);
int beam_etf_embed_get_type(const char *buf, const int *index, int *type, int *size);
int beam_etf_embed_decode_version(const char *buf, int *index, int *version);
int beam_etf_embed_decode_long(const char *buf, int *index, long *p);
int beam_etf_embed_decode_ulong(const char *buf, int *index, unsigned long *p);
int beam_etf_embed_decode_double(const char *buf, int *index, double *p);
int beam_etf_embed_decode_boolean(const char *buf, int *index, int *p);
int beam_etf_embed_decode_char(const char *buf, int *index, char *p);
int beam_etf_embed_decode_string(const char *buf, int *index, char *p);
int beam_etf_embed_decode_atom(const char *buf, int *index, char *p, int capacity);
int beam_etf_embed_decode_binary(const char *buf, int *index, void *p, long *len);
int beam_etf_embed_decode_bitstring(const char *buf, int *index, const char** pp,
                        unsigned int* bitoffsp, size_t *nbitsp);
int beam_etf_embed_decode_fun(const char* buf, int* index, BeamEtfFun* p);
void beam_etf_embed_free_fun(BeamEtfFun* f);
int beam_etf_embed_decode_pid(const char *buf, int *index, BeamEtfPid *p);
int beam_etf_embed_decode_port(const char *buf, int *index, BeamEtfPort *p);
int beam_etf_embed_decode_ref(const char *buf, int *index, BeamEtfRef *p);
int beam_etf_embed_decode_tuple_header(const char *buf, int *index, int *arity);
int beam_etf_embed_decode_list_header(const char *buf, int *index, int *arity);
int beam_etf_embed_decode_map_header(const char *buf, int *index, int *arity);
int beam_etf_embed_decode_iodata(const char *buf, int* index, int *szp, char *out_buf);
int beam_etf_embed_x_new(BeamEtfBuffer* x);
int beam_etf_embed_x_new_with_version(BeamEtfBuffer* x);
int beam_etf_embed_x_free(BeamEtfBuffer* x);
int beam_etf_embed_x_append(BeamEtfBuffer* x, const BeamEtfBuffer* x2);
int beam_etf_embed_x_append_buf(BeamEtfBuffer* x, const char* buf, int len);
int beam_etf_embed_skip_term(const char* buf, int* index);
int beam_etf_embed_cmp_refs(BeamEtfRef *a, BeamEtfRef *b);
int beam_etf_embed_cmp_pids(BeamEtfPid *a, BeamEtfPid *b);
int beam_etf_embed_cmp_ports(BeamEtfPort *a, BeamEtfPort *b);
int beam_etf_embed_decode_longlong(const char *buf, int *index, BeamEtfLongLong *p);
int beam_etf_embed_decode_ulonglong(const char *buf, int *index, BeamEtfULongLong *p);
int beam_etf_embed_encode_longlong(char *buf, int *index, BeamEtfLongLong p);
int beam_etf_embed_encode_ulonglong(char *buf, int *index, BeamEtfULongLong p);
int beam_etf_embed_x_encode_longlong(BeamEtfBuffer* x, BeamEtfLongLong n);
int beam_etf_embed_x_encode_ulonglong(BeamEtfBuffer* x, BeamEtfULongLong n);
int beam_etf_compat_encode_atom_len_as(char *buf, int *index, const char *p, int len,
    BeamEtfCompatCharEncoding from, BeamEtfCompatCharEncoding to);
int beam_etf_compat_decode_atom_as(const char *buf, int *index, char *p, int capacity,
    BeamEtfCompatCharEncoding want, BeamEtfCompatCharEncoding *was, BeamEtfCompatCharEncoding *result);
int beam_etf_compat_encode_big(char *buf, int *index, BeamEtfCompatBig *big);
int beam_etf_compat_decode_big(const char *buf, int *index, BeamEtfCompatBig *big);
int beam_etf_compat_x_fix_buff(BeamEtfBuffer *buffer, int needed, int extra);
#ifdef __cplusplus
}
#endif
#endif
