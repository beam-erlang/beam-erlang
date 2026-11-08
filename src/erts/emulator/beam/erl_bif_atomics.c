/*
 * %CopyrightBegin%
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright Ericsson AB 2018-2025. All Rights Reserved.
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

/*
 * Purpose:  High performance atomics.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stddef.h> /* offsetof */

#include "sys.h"
#include "export.h"
#include "bif.h"
#include "erl_threads.h"
#include "big.h"
#include "erl_binary.h"
#include "erl_bif_unique.h"
#include "erl_map.h"
#include "erl_bif_shared_data.h"

typedef struct
{
    int is_signed;
    UWord vlen;
    erts_atomic64_t v[1];
}AtomicsRef;

static int atomics_destructor(Binary *unused)
{
    return 1;
}

#define OPT_SIGNED (1 << 0)

Eterm erts_atomic_array_new(Process *process, Eterm arg1, Eterm arg2)
{
    AtomicsRef* p;
    Binary* mbin;
    UWord i, cnt, opts;
    Uint bytes;
    Eterm* hp;

    if (!term_to_UWord(arg1, &cnt)) {
        BIF_ERROR(process, cnt);
    }
    if (cnt == 0) {
        BIF_ERROR(process, BADARG);
    }
    if (!term_to_UWord(arg2, &opts)) {
        BIF_ERROR(process, opts);
    }

    if (cnt > ((ERTS_UWORD_MAX - ERTS_MAGIC_BIN_SIZE(0) -
                offsetof(AtomicsRef, v)) / sizeof(p->v[0])))
        BIF_ERROR(process, SYSTEM_LIMIT);

    bytes = offsetof(AtomicsRef, v) + cnt*sizeof(p->v[0]);
    mbin = erts_create_magic_binary_x(bytes,
                                      atomics_destructor,
                                      ERTS_ALC_T_ATOMICS,
                                      0);
    p = ERTS_MAGIC_BIN_DATA(mbin);
    p->is_signed = opts & OPT_SIGNED;
    p->vlen = cnt;
    for (i=0; i < cnt; i++)
        erts_atomic64_init_nob(&p->v[i], 0);
    hp = HAlloc(process, ERTS_MAGIC_REF_THING_SIZE);
    return erts_mk_magic_ref(&hp, &MSO(process), mbin);
}

BIF_RETTYPE erts_internal_atomics_new_2(BIF_ALIST_2)
{
    return erts_atomic_array_new(BIF_P, BIF_ARG_1, BIF_ARG_2);
}

static ERTS_INLINE int get_ref(Eterm ref, AtomicsRef** pp)
{
    Binary* mbin;
    if (!is_internal_magic_ref(ref))
        return 0;

    mbin = erts_magic_ref2bin(ref);
    if (ERTS_MAGIC_BIN_DESTRUCTOR(mbin) != atomics_destructor)
        return 0;
    *pp = ERTS_MAGIC_BIN_DATA(mbin);
    return 1;
}

static ERTS_INLINE int get_ref_ix(Eterm ref, Eterm ix,
                                  AtomicsRef** pp, UWord* ixp)
{
    return (get_ref(ref, pp)
            && term_to_UWord(ix, ixp)
            && --(*ixp) < (*pp)->vlen);
}

static ERTS_INLINE int get_value(AtomicsRef* p, Eterm term, erts_aint64_t *valp)
{
    return (p->is_signed ?
            term_to_Sint64(term, (Sint64*)valp) :
            term_to_Uint64(term, (Uint64*)valp));
}

static ERTS_INLINE int get_incr(AtomicsRef* p, Eterm term, erts_aint64_t *valp)
{
    return (term_to_Sint64(term, (Sint64*)valp)
            || term_to_Uint64(term, (Uint64*)valp));
}

static ERTS_INLINE Eterm bld_atomic(Process* proc, AtomicsRef* p,
                                    erts_aint64_t val)
{
    if (p->is_signed) {
        if (IS_SSMALL(val))
            return make_small((Sint) val);
        else {
            Uint hsz = ERTS_SINT64_HEAP_SIZE(val);
            Eterm* hp = HAlloc(proc, hsz);
            return erts_sint64_to_big(val, &hp);
        }
    }
    else {
        if ((Uint64)val <= MAX_SMALL)
            return make_small((Sint) val);
        else {
            Uint hsz = ERTS_UINT64_HEAP_SIZE((Uint64)val);
            Eterm* hp = HAlloc(proc, hsz);
            return erts_uint64_to_big(val, &hp);
        }
    }
}

Eterm erts_atomic_array_put(Process *process, Eterm arg1, Eterm arg2, Eterm arg3)
{
    AtomicsRef* p;
    UWord ix;
    erts_aint64_t val;

    if (!get_ref_ix(arg1, arg2, &p, &ix)
        || !get_value(p, arg3, &val)) {
        BIF_ERROR(process, BADARG);
    }
    erts_atomic64_set_mb(&p->v[ix], val);
    return am_ok;
}

BIF_RETTYPE atomics_put_3(BIF_ALIST_3)
{
    return erts_atomic_array_put(BIF_P, BIF_ARG_1, BIF_ARG_2, BIF_ARG_3);
}

Eterm erts_atomic_array_get(Process *process, Eterm arg1, Eterm arg2)
{
    AtomicsRef* p;
    UWord ix;

    if (!get_ref_ix(arg1, arg2, &p, &ix)) {
        BIF_ERROR(process, BADARG);
    }
    return bld_atomic(process, p, erts_atomic64_read_mb(&p->v[ix]));
}

BIF_RETTYPE atomics_get_2(BIF_ALIST_2)
{
    return erts_atomic_array_get(BIF_P, BIF_ARG_1, BIF_ARG_2);
}

Eterm erts_atomic_array_add(Process *process, Eterm arg1, Eterm arg2, Eterm arg3)
{
    AtomicsRef* p;
    UWord ix;
    erts_aint64_t incr;

    if (!get_ref_ix(arg1, arg2, &p, &ix)
        || !get_incr(p, arg3, &incr)) {
        BIF_ERROR(process, BADARG);
    }
    erts_atomic64_add_mb(&p->v[ix], incr);
    return am_ok;
}

BIF_RETTYPE atomics_add_3(BIF_ALIST_3)
{
    return erts_atomic_array_add(BIF_P, BIF_ARG_1, BIF_ARG_2, BIF_ARG_3);
}

Eterm erts_atomic_array_add_get(Process *process, Eterm arg1, Eterm arg2, Eterm arg3)
{
    AtomicsRef* p;
    UWord ix;
    erts_aint64_t incr;

    if (!get_ref_ix(arg1, arg2, &p, &ix)
        || !get_incr(p, arg3, &incr)) {
        BIF_ERROR(process, BADARG);
    }
    return bld_atomic(process, p, erts_atomic64_add_read_mb(&p->v[ix], incr));
}

BIF_RETTYPE atomics_add_get_3(BIF_ALIST_3)
{
    return erts_atomic_array_add_get(BIF_P, BIF_ARG_1, BIF_ARG_2, BIF_ARG_3);
}

Eterm erts_atomic_array_exchange(Process *process, Eterm arg1, Eterm arg2, Eterm arg3)
{
    AtomicsRef* p;
    UWord ix;
    erts_aint64_t desired, was;

    if (!get_ref_ix(arg1, arg2, &p, &ix)
        || !get_value(p, arg3, &desired)) {
        BIF_ERROR(process, BADARG);
    }
    was = erts_atomic64_xchg_mb(&p->v[ix], desired);
    return bld_atomic(process, p, was);
}

BIF_RETTYPE atomics_exchange_3(BIF_ALIST_3)
{
    return erts_atomic_array_exchange(BIF_P, BIF_ARG_1, BIF_ARG_2, BIF_ARG_3);
}

Eterm erts_atomic_array_compare_exchange(Process *process, Eterm arg1, Eterm arg2, Eterm arg3, Eterm arg4)
{
    AtomicsRef* p;
    UWord ix;
    erts_aint64_t expected, desired, was;

    if (!get_ref_ix(arg1, arg2, &p, &ix)
        || !get_value(p, arg3, &expected)
        || !get_value(p, arg4, &desired)) {
        BIF_ERROR(process, BADARG);
    }
    was = erts_atomic64_cmpxchg_mb(&p->v[ix], desired, expected);
    return was == expected ? am_ok : bld_atomic(process, p, was);
}

BIF_RETTYPE atomics_compare_exchange_4(BIF_ALIST_4)
{
    return erts_atomic_array_compare_exchange(BIF_P, BIF_ARG_1, BIF_ARG_2, BIF_ARG_3, BIF_ARG_4);
}

Eterm erts_atomic_array_info(Process *process, Eterm arg1)
{
    AtomicsRef* p;
    Uint hsz = 0;
    Eterm *hp;
    Uint64 max;
    Sint64 min;
    UWord memory;
    Eterm keys[4], values[4], res;
    ErtsHeapFactory factory;

    if (!get_ref(arg1, &p))
        BIF_ERROR(process, BADARG);

    erts_factory_proc_init(&factory, process);

    max = p->is_signed ? ERTS_SINT64_MAX : ERTS_UINT64_MAX;
    min = p->is_signed ? ERTS_SINT64_MIN : 0;
    memory = erts_magic_ref2bin(arg1)->orig_size;

    erts_bld_uint64(NULL, &hsz, max);
    erts_bld_sint64(NULL, &hsz, min);
    erts_bld_uword(NULL, &hsz, p->vlen);
    erts_bld_uword(NULL, &hsz, memory);

    hp = erts_produce_heap(&factory, hsz, MAP4_SZ);
    keys[0] = am_max;
    values[0] = erts_bld_uint64(&hp, NULL, max);
    keys[1] = am_min;
    values[1] = erts_bld_sint64(&hp, NULL, min);
    keys[2] = am_size;
    values[2]  = erts_bld_uword(&hp, NULL, p->vlen);
    keys[3] = am_memory;
    values[3] = erts_bld_uword(&hp, NULL, memory);

    res = erts_map_from_ks_and_vs(&factory, keys, values, 4);
    ASSERT(is_map(res));
    erts_factory_close(&factory);
    return res;
}

BIF_RETTYPE atomics_info_1(BIF_ALIST_1)
{
    return erts_atomic_array_info(BIF_P, BIF_ARG_1);
}

/* Parsing was an ordinary Erlang traversal. Preserve its validation order and
 * scheduling while the public constructor calls the same allocation kernel. */
typedef struct {
    Eterm size, options, cursor;
    Uint encoded;
} AtomicOptionsContext;

static int atomic_options_dtor(Binary *bin) { return 1; }

BIF_RETTYPE atomic_array_new_list_modern_2(BIF_ALIST_2)
{
    AtomicOptionsContext local, *ctx = &local;
    Export *entry = BIF_TRAP_EXPORT(BIF_atomic_array_new_list_modern_2);
    int resumed = 0, invalid = 0;
    Sint initial = ERTS_BIF_REDS_LEFT(BIF_P) * 16, left = initial;
    Eterm result;
    if (is_internal_magic_ref(BIF_ARG_1) && BIF_ARG_2 == NIL &&
        (BIF_P->flags & F_DISABLE_GC) &&
        ERTS_MAGIC_BIN_DESTRUCTOR(erts_magic_ref2bin(BIF_ARG_1)) == atomic_options_dtor) {
        ctx = ERTS_MAGIC_BIN_DATA(erts_magic_ref2bin(BIF_ARG_1));
        resumed = 1;
    } else {
        local.size = BIF_ARG_1; local.options = local.cursor = BIF_ARG_2;
        local.encoded = OPT_SIGNED;
    }
    while (is_list(ctx->cursor) && left > 0) {
        Eterm option = CAR(list_val(ctx->cursor));
        Eterm *tuple;
        if (!is_tuple(option) || arityval((tuple = tuple_val(option))[0]) != 2 ||
            tuple[1] != am_signed || (tuple[2] != am_true && tuple[2] != am_false)) {
            invalid = 1; break;
        }
        ctx->encoded = tuple[2] == am_true ? OPT_SIGNED : 0;
        ctx->cursor = CDR(list_val(ctx->cursor));
        left--;
    }
    BUMP_REDS(BIF_P, (initial-left)/16);
    if (!invalid && is_list(ctx->cursor)) {
        Eterm ref = BIF_ARG_1;
        if (!resumed) {
            Binary *bin = erts_create_magic_binary(sizeof(*ctx), atomic_options_dtor);
            Eterm *hp;
            *(AtomicOptionsContext *)ERTS_MAGIC_BIN_DATA(bin) = *ctx;
            hp = HAlloc(BIF_P, ERTS_MAGIC_REF_THING_SIZE);
            ref = erts_mk_magic_ref(&hp, &MSO(BIF_P), bin);
            erts_set_gc_state(BIF_P, 0);
        }
        BUMP_ALL_REDS(BIF_P);
        BIF_TRAP2(entry, BIF_P, ref, NIL);
    }
    if (resumed) erts_set_gc_state(BIF_P, 1);
    if (invalid || !is_nil(ctx->cursor)) {
        BIF_P->fvalue = am_badopt;
        if (resumed) {
            ERTS_BIF_ERROR_TRAPPED2(BIF_P, BADARG | EXF_HAS_EXT_INFO, entry, ctx->size, ctx->options);
        }
        BIF_ERROR(BIF_P, BADARG | EXF_HAS_EXT_INFO);
    }
    result = erts_atomic_array_new(BIF_P, ctx->size, make_small(ctx->encoded));
    if (is_non_value(result) && resumed) {
        ERTS_BIF_ERROR_TRAPPED2(BIF_P, BIF_P->freason, entry, ctx->size, ctx->options);
    }
    return result;
}
