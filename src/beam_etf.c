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
#include "etf/etf_internal.h"

int beam_etf_x_new(BeamEtfBuffer* x)
{
    x->buff = malloc(100);
    x->buffsz = 100;
    x->index = 0;
    return x->buff != NULL ? 0 : -1;
}

int beam_etf_x_new_with_version(BeamEtfBuffer* x)
{
    if (beam_etf_x_new(x) < 0)
	return -1;
    return beam_etf_encode_version(x->buff, &x->index);
}

int beam_etf_x_free(BeamEtfBuffer* x)
{
    if (x->buff == NULL)
	return -1;
    free(x->buff);
    x->buff = NULL;
    return 0;
}

int beam_etf_internal_x_fix_buff(BeamEtfBuffer* x, int szneeded, int extra)
{
    int sz = szneeded + extra;
    if (sz > x->buffsz) {
	sz += extra;	/* to avoid reallocating each and every time */
	x->buffsz = sz;
	x->buff = realloc(x->buff, sz);
    }
    return x->buff != NULL;
}

int beam_etf_x_append(BeamEtfBuffer* x, const BeamEtfBuffer* x2)
{
    return beam_etf_x_append_buf(x, x2->buff, x2->index);
}

int beam_etf_x_append_buf(BeamEtfBuffer* x, const char* buf, int len)
{
    if (!beam_etf_internal_x_fix_buff(x, x->index+len, 100))
	return -1;
    memcpy(&x->buff[x->index], buf, len);
    x->index += len;
    return 0;
}

int beam_etf_x_encode_string(BeamEtfBuffer* x, const char* s)
{
    return beam_etf_x_encode_string_len(x, s, strlen(s));
}

int beam_etf_x_encode_string_len(BeamEtfBuffer* x, const char* s, int len)
{
    int i = x->index;
    if (beam_etf_encode_string_len(NULL, &i, s, len) == -1)
      return -1;
    if (!beam_etf_internal_x_fix_buff(x, i, 100))
	return -1;
    return beam_etf_encode_string_len(x->buff, &x->index, s, len);
}

int beam_etf_x_encode_binary(BeamEtfBuffer* x, const void* p, int len)
{
    int i = x->index;
    if (beam_etf_encode_binary(NULL, &i, p, len) == -1)
      return -1;
    if (!beam_etf_internal_x_fix_buff(x, i, 100))
	return -1;
    return beam_etf_encode_binary(x->buff, &x->index, p, len);
}

int beam_etf_x_encode_bitstring(BeamEtfBuffer* x, const char* p, size_t bitoffs, size_t bits)
{
    int i = x->index;
    if (beam_etf_encode_bitstring(NULL, &i, p, bitoffs, bits) == -1)
      return -1;
    if (!beam_etf_internal_x_fix_buff(x, i, 100))
	return -1;
    return beam_etf_encode_bitstring(x->buff, &x->index, p, bitoffs, bits);
}

int beam_etf_x_encode_long(BeamEtfBuffer* x, long n)
{
    int i = x->index;
    if (beam_etf_encode_long(NULL, &i, n) == -1)
      return -1;
    if (!beam_etf_internal_x_fix_buff(x, i, 100))
	return -1;
    return beam_etf_encode_long(x->buff, &x->index, n);
}

int beam_etf_x_encode_ulong(BeamEtfBuffer* x, unsigned long n)
{
    int i = x->index;
    if (beam_etf_encode_ulong(NULL, &i, n) == -1)
      return -1;
    if (!beam_etf_internal_x_fix_buff(x, i, 100))
	return -1;
    return beam_etf_encode_ulong(x->buff, &x->index, n);
}

int beam_etf_x_encode_char(BeamEtfBuffer* x, char p)
{
    int i = x->index;
    if (beam_etf_encode_char(NULL, &i, p) == -1)
      return -1;
    if (!beam_etf_internal_x_fix_buff(x, i, 100))
	return -1;
    return beam_etf_encode_char(x->buff, &x->index, p);
}

int beam_etf_x_encode_boolean(BeamEtfBuffer* x, int p)
{
    int i = x->index;
    if (beam_etf_encode_boolean(NULL, &i, p) == -1)
      return -1;
    if (!beam_etf_internal_x_fix_buff(x, i, 100))
	return -1;
    return beam_etf_encode_boolean(x->buff, &x->index, p);
}

int beam_etf_x_encode_double(BeamEtfBuffer* x, double dbl)
{
    int i = x->index;
    if (beam_etf_encode_double(NULL, &i, dbl) == -1)
      return -1;
    if (!beam_etf_internal_x_fix_buff(x, i, 100))
	return -1;
    return beam_etf_encode_double(x->buff, &x->index, dbl);
}

int beam_etf_x_encode_list_header(BeamEtfBuffer* x, long n)
{
    int i = x->index;
    if (beam_etf_encode_list_header(NULL, &i, n) == -1)
      return -1;
    if (!beam_etf_internal_x_fix_buff(x, i, 100))
	return -1;
    return beam_etf_encode_list_header(x->buff, &x->index, n);
}

int beam_etf_x_encode_empty_list(BeamEtfBuffer* x)
{
    int i = x->index;
    if (beam_etf_encode_empty_list(NULL, &i) == -1)
      return -1;
    if (!beam_etf_internal_x_fix_buff(x, i, 100))
	return -1;
    return beam_etf_encode_empty_list(x->buff, &x->index);
}

int beam_etf_x_encode_version(BeamEtfBuffer* x)
{
    int i = x->index;
    if (beam_etf_encode_version(NULL, &i) == -1)
      return -1;
    if (!beam_etf_internal_x_fix_buff(x, i, 100))
	return -1;
    return beam_etf_encode_version(x->buff, &x->index);
}

int beam_etf_x_encode_tuple_header(BeamEtfBuffer* x, long n)
{
    int i = x->index;
    if (beam_etf_encode_tuple_header(NULL, &i, n) == -1)
      return -1;
    if (!beam_etf_internal_x_fix_buff(x, i, 100))
	return -1;
    return beam_etf_encode_tuple_header(x->buff, &x->index, n);
}

int beam_etf_x_encode_map_header(BeamEtfBuffer* x, long n)
{
    int i = x->index;
    if (beam_etf_encode_map_header(NULL, &i, n) == -1)
      return -1;
    if (!beam_etf_internal_x_fix_buff(x, i, 100))
	return -1;
    return beam_etf_encode_map_header(x->buff, &x->index, n);
}

int beam_etf_x_encode_pid(BeamEtfBuffer* x, const BeamEtfPid* pid)
{
    int i = x->index;
    if (beam_etf_encode_pid(NULL, &i, pid) == -1)
      return -1;
    if (!beam_etf_internal_x_fix_buff(x, i, 100))
	return -1;
    return beam_etf_encode_pid(x->buff, &x->index, pid);
}

int beam_etf_x_encode_fun(BeamEtfBuffer* x, const BeamEtfFun* fun)
{
    int i = x->index;
    if (beam_etf_encode_fun(NULL, &i, fun) == -1)
      return -1;
    if (!beam_etf_internal_x_fix_buff(x, i, 100))
	return -1;
    return beam_etf_encode_fun(x->buff, &x->index, fun);
}

int beam_etf_x_encode_ref(BeamEtfBuffer* x, const BeamEtfRef* ref)
{
    int i = x->index;
    if (beam_etf_encode_ref(NULL, &i, ref) == -1)
      return -1;
    if (!beam_etf_internal_x_fix_buff(x, i, 100))
	return -1;
    return beam_etf_encode_ref(x->buff, &x->index, ref);
}

int beam_etf_x_encode_port(BeamEtfBuffer* x, const BeamEtfPort* port)
{
    int i = x->index;
    if (beam_etf_encode_port(NULL, &i, port) == -1)
      return -1;
    if (!beam_etf_internal_x_fix_buff(x, i, 100))
	return -1;
    return beam_etf_encode_port(x->buff, &x->index, port);
}
int beam_etf_encode_atom(char *buf, int *index, const char *p)
{ return beam_etf_encode_atom_len(buf, index, p, (int)strlen(p)); }
int beam_etf_encode_atom_len(char *buf, int *index, const char *p, int len)
{ return beam_etf_internal_encode_atom_len_as(buf, index, p, len, BEAM_ETF_COMPAT_UTF8, BEAM_ETF_COMPAT_UTF8); }
int beam_etf_decode_atom(const char *buf, int *index, char *p, int capacity)
{ return beam_etf_internal_decode_atom_as(buf, index, p, capacity, BEAM_ETF_COMPAT_UTF8, NULL, NULL); }
int beam_etf_x_encode_atom(BeamEtfBuffer *x, const char *p)
{ return beam_etf_x_encode_atom_len(x, p, (int)strlen(p)); }
int beam_etf_x_encode_atom_len(BeamEtfBuffer *x, const char *p, int len)
{
    int i = x->index;
    if (beam_etf_encode_atom_len(NULL, &i, p, len) < 0 ||
        !beam_etf_internal_x_fix_buff(x, i, 100)) return -1;
    return beam_etf_encode_atom_len(x->buff, &x->index, p, len);
}
