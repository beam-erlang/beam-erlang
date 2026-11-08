/*
 * %CopyrightBegin%
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright Ericsson AB 2001-2025. All Rights Reserved.
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

#include <string.h>
#include <stdlib.h>

int beam_etf_decode_fun(const char *buf, int *index, BeamEtfFun *p)
{
    const char *s = buf + *index;
    const char *s0 = s;
    int i, ix, ix0, n;
    BeamEtfPid* p_pid;
    char* p_module;
    long* p_index;
    long* p_uniq;
    long* p_old_index;

    if (p != NULL) {
	p_pid = &p->u.closure.pid;
	p_module = &p->module[0];
	p_index = &p->u.closure.index;
	p_uniq = &p->u.closure.uniq;
	p_old_index = &p->u.closure.old_index;
    }
    else {
	p_index = NULL; p_uniq = NULL; p_old_index = NULL;
	p_pid = NULL; p_module = NULL;
    }

    switch (get8(s)) {
    case BEAM_ETF_FUN_EXT:
	/* mark as old (R7 and older) external fun */
	if (p != NULL) {
            p->type = BEAM_ETF_FUN_CLOSURE;
            p->arity = -1;
        }
	/* first number of free vars (environment) */
	n = get32be(s);
	/* then the pid */
	ix = 0;
	if (beam_etf_decode_pid(s, &ix, p_pid) < 0)
	    return -1;
	/* then the module (atom) */
	if (beam_etf_internal_decode_atom_as(s, &ix, p_module, BEAM_ETF_MAXATOMLEN_UTF8, BEAM_ETF_COMPAT_UTF8,
                              NULL, NULL) < 0)
	    return -1;
	/* then the index */
	if (beam_etf_decode_long(s, &ix, p_index) < 0)
	    return -1;
	/* then the uniq */
	if (beam_etf_decode_long(s, &ix, p_uniq) < 0)
	    return -1;
	/* finally the free vars */
	ix0 = ix;
	for (i = 0; i < n; ++i) {
	    if (beam_etf_skip_term(s, &ix) < 0)
		return -1;
	}
	if (p != NULL) {
	    p->u.closure.n_free_vars = n;
            p->u.closure.free_var_len = ix - ix0;
            if (p->u.closure.free_var_len > 0) {
                p->u.closure.free_vars = malloc(p->u.closure.free_var_len);
                if (!(p->u.closure.free_vars)) return -1;
                memcpy(p->u.closure.free_vars, s + ix0, p->u.closure.free_var_len);
            }
	}
	s += ix;
	*index += s-s0;
        return 0;
	break;
    case BEAM_ETF_NEW_FUN_EXT:
	/* first total size */
	n = get32be(s);
	/* then the arity */
	i = get8(s);
	if (p != NULL) {
            p->type = BEAM_ETF_FUN_CLOSURE;
            p->arity = i;
            /* then md5 */
            memcpy(p->u.closure.md5, s, 16);
        }
	s += 16;
	/* then index */
	i = get32be(s);
	if (p != NULL) p->u.closure.index = i;
	/* then the number of free vars (environment) */
	i = get32be(s);
	if (p != NULL) p->u.closure.n_free_vars = i;
	/* then the module (atom) */
	ix = 0;
	if (beam_etf_internal_decode_atom_as(s, &ix, p_module, BEAM_ETF_MAXATOMLEN_UTF8, BEAM_ETF_COMPAT_UTF8,
                              NULL, NULL) < 0)
	    return -1;
	/* then the old_index */
	if (beam_etf_decode_long(s, &ix, p_old_index) < 0)
	    return -1;
	/* then the old_uniq */
	if (beam_etf_decode_long(s, &ix, p_uniq) < 0)
	    return -1;
	/* the pid */
	if (beam_etf_decode_pid(s, &ix, p_pid) < 0)
	    return -1;
	/* finally the free vars */
	s += ix;
	n = n - (s - s0) + 1;
	if (n < 0) return -1;
	if (p != NULL) {
	    p->u.closure.free_var_len = n;
	    if (n > 0) {
		p->u.closure.free_vars = malloc(n);
		if (!(p->u.closure.free_vars)) return -1;
		memcpy(p->u.closure.free_vars, s, n);
	    }
	}
	s += n;
	*index += s-s0;
        return 0;
	break;
    case BEAM_ETF_EXPORT_EXT: {
        char* p_func;
        long* p_arity;
        int used;

        if (p) {
            p->type = BEAM_ETF_FUN_EXPORT;
            p_arity = &p->arity;
        }
        else {
            p_arity = NULL;
        }
	ix = 0;
        if (beam_etf_internal_decode_atom_as(s, &ix, p_module, BEAM_ETF_MAXATOMLEN_UTF8, BEAM_ETF_COMPAT_UTF8,
                              NULL, NULL) < 0)
            return -1;
        if (p) {
            /* try use module buffer for function name */
            used = strlen(p->module) + 1;
            p_func = p->module + used;
            p->u.exprt.func = p_func;
            p->u.exprt.func_allocated = 0;
        }
        else {
            used = 0;
            p_func = NULL;
        }
        while (beam_etf_internal_decode_atom_as(s, &ix, p_func, BEAM_ETF_MAXATOMLEN_UTF8-used,
                                 BEAM_ETF_COMPAT_UTF8, NULL, NULL) < 0) {
            if (!used)
                return -1;
            p_func = malloc(BEAM_ETF_MAXATOMLEN_UTF8);
            p->u.exprt.func = p_func;
            p->u.exprt.func_allocated = 1;
            used = 0;
        }
        if (beam_etf_decode_long(s, &ix, p_arity) < 0)
            return -1;
	s += ix;
	*index += s - s0;
        return 0;
    }
    default:
	return -1;
    }
}

void beam_etf_free_fun(BeamEtfFun* f)
{
    switch (f->type) {
    case BEAM_ETF_FUN_CLOSURE:
        if (f->u.closure.free_var_len > 0)
            free(f->u.closure.free_vars);
        break;
    case BEAM_ETF_FUN_EXPORT:
        if (f->u.exprt.func_allocated)
            free(f->u.exprt.func);
        break;
    }
}
