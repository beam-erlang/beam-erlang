/*
 * %CopyrightBegin%
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright Ericsson AB 2002-2025. All Rights Reserved.
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
#include "etf_internal.h"
#include "putget.h"

int beam_etf_skip_term(const char* buf, int* index)
{
    int i, n, ty;

    /* ASSERT(ep != NULL); */

    beam_etf_get_type(buf, index, &ty, &n);
    switch (ty) {
    case BEAM_ETF_ATOM_EXT:
	/* FIXME: what if some weird locale is in use? */
	if (beam_etf_internal_decode_atom_as(buf, index, NULL, BEAM_ETF_MAXATOMLEN_UTF8, (BEAM_ETF_COMPAT_LATIN1|BEAM_ETF_COMPAT_UTF8),
			      NULL, NULL) < 0) return -1;
	break;
    case BEAM_ETF_PID_EXT:
    case BEAM_ETF_NEW_PID_EXT:
	if (beam_etf_decode_pid(buf, index, NULL) < 0) return -1;
	break;
    case BEAM_ETF_PORT_EXT:
    case BEAM_ETF_NEW_PORT_EXT:
	if (beam_etf_decode_port(buf, index, NULL) < 0) return -1;
	break;
    case BEAM_ETF_NEW_REFERENCE_EXT:
    case BEAM_ETF_NEWER_REFERENCE_EXT:
    case BEAM_ETF_REFERENCE_EXT:
	if (beam_etf_decode_ref(buf, index, NULL) < 0) return -1;
	break;
    case BEAM_ETF_NIL_EXT:
	if (beam_etf_decode_list_header(buf, index, &n) < 0) return -1;
	break;
    case BEAM_ETF_LIST_EXT:
	if (beam_etf_decode_list_header(buf, index, &n) < 0) return -1;
	for (i = 0; i < n; ++i)
	    beam_etf_skip_term(buf, index);
	if (beam_etf_get_type(buf, index, &ty, &n) < 0) return -1;
	if (ty != BEAM_ETF_NIL_EXT)
	    beam_etf_skip_term(buf, index);
	else
	    if (beam_etf_decode_list_header(buf, index, &n) < 0) return -1;
	break;
    case BEAM_ETF_STRING_EXT:
	if (beam_etf_decode_string(buf, index, NULL) < 0) return -1;
	break;
    case BEAM_ETF_SMALL_TUPLE_EXT:
    case BEAM_ETF_LARGE_TUPLE_EXT:
	if (beam_etf_decode_tuple_header(buf, index, &n) < 0) return -1;
	for (i = 0; i < n; ++i)
	    beam_etf_skip_term(buf, index);
	break;
    case BEAM_ETF_MAP_EXT:
	if (beam_etf_decode_map_header(buf, index, &n) < 0) return -1;
	n *= 2;
	for (i = 0; i < n; ++i)
	    beam_etf_skip_term(buf, index);
	break;
    case BEAM_ETF_BINARY_EXT:
	if (beam_etf_decode_binary(buf, index, NULL, NULL) < 0)
	    return -1;
	break;
    case BEAM_ETF_BIT_BINARY_EXT:
        if (beam_etf_decode_bitstring(buf, index, NULL, NULL, NULL) < 0)
            return -1;
        break;
    case BEAM_ETF_SMALL_INTEGER_EXT:
    case BEAM_ETF_INTEGER_EXT:
	if (beam_etf_decode_long(buf, index, NULL) < 0) return -1;
	break;
    case BEAM_ETF_SMALL_BIG_EXT:
    case BEAM_ETF_LARGE_BIG_EXT:
	if (beam_etf_internal_decode_big(buf, index, NULL) < 0) return -1;
	break;
    case BEAM_ETF_FLOAT_EXT:
    case BEAM_ETF_NEW_FLOAT_EXT:
	if (beam_etf_decode_double(buf, index, NULL) < 0) return -1;
	break;
    case BEAM_ETF_FUN_EXT:
    case BEAM_ETF_NEW_FUN_EXT:
    case BEAM_ETF_EXPORT_EXT:
	if (beam_etf_decode_fun(buf, index, NULL) < 0) return -1;
	break;
    default:
	return -1;
    }
    return 0;
}

