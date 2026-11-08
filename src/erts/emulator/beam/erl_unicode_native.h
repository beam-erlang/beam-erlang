/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 2008-2025. All Rights Reserved.
 */
/* Resumable contracts over the shared Unicode decoders. Included by erl_unicode.c. */

typedef struct {
    Eterm original, encoding, current, frames, binary, tail, error_rest;
    byte *output;
    Uint used, capacity, characters, offset;
    byte pending[4];
    unsigned pending_size, missing;
    int utf32, little, list_result, binary_head, in_binary, unwinding;
} UnicodeNativeContext;

static int unicode_native_dtor(Binary *bin)
{
    UnicodeNativeContext *ctx = ERTS_MAGIC_BIN_DATA(bin);
    if (ctx->output) {
        free_restart(ctx->output);
        ctx->output = NULL;
    }
    return 1;
}

static Eterm unicode_native_cons(Process *p, Eterm head, Eterm tail)
{
    Eterm *hp = HAlloc(p, 2);
    return CONS(hp, head, tail);
}

static int unicode_native_encoding(Eterm enc, int *utf32, int *little)
{
    *little = 0;
    if (is_tuple(enc)) {
        Eterm *tuple = tuple_val(enc);
        if (arityval(tuple[0]) != 2 ||
            (tuple[2] != am_big && tuple[2] != am_little)) {
            return 0;
        }
        *little = tuple[2] == am_little;
        enc = tuple[1];
    }
    if (enc != am_utf16 && enc != am_utf32) {
        return 0;
    }
    *utf32 = enc == am_utf32;
    return 1;
}

/* Match the ordinary converter's cbv incomplete-prefix classification. */
static unsigned unicode_native_missing(UnicodeNativeContext *ctx, byte *b, Uint n)
{
    if (!ctx->utf32) {
        if (ctx->little) {
            if (n == 1) return 1;
            if (n == 2 && (b[1] >> 2) == 54) return 2;
            if (n == 3 && (b[1] >> 2) == 54) return 1;
        } else {
            if (n == 1 && (b[0] <= 215 || b[0] >= 224)) return 1;
            if (n == 1 && (b[0] >> 2) == 54) return 3;
            if (n == 2 && (b[0] >> 2) == 54) return 2;
            if (n == 3 && (b[0] >> 2) == 54 && (b[2] >> 2) == 55) return 1;
        }
    } else if (ctx->little) {
        if (n == 1) return 3;
        if (n == 2) return 2;
        if (n == 3 && (b[0] == 254 || b[0] == 255) && b[1] == 255 && b[2] == 0)
            return 0;
        if (n == 3 && b[2] <= 16 && (b[2] > 0 || b[1] <= 215 || b[1] >= 224))
            return 1;
    } else {
        if (n == 1 && b[0] == 0) return 3;
        if (n == 2 && b[0] == 0 && b[1] <= 16) return 2;
        if (n == 3 && b[0] == 0 && b[1] <= 16 &&
            (b[1] > 0 || b[2] <= 215 || b[2] >= 224)) return 1;
    }
    return 0;
}

static int unicode_native_emit(Process *p, UnicodeNativeContext *ctx, Uint cp)
{
    byte bytes[4];
    Uint n = unicode_encode_scalar(bytes, cp);
    if (!n) return 0;
    if (ctx->used > ERTS_UWORD_MAX / CHAR_BIT - n) return -1;
    if (ctx->used + n > ctx->capacity) {
        Uint capacity = ctx->capacity ? ctx->capacity : 256;
        while (capacity < ctx->used + n) {
            if (capacity > (ERTS_UWORD_MAX / CHAR_BIT) / 2) {
                capacity = ctx->used + n;
                break;
            }
            capacity *= 2;
        }
        ctx->output = ctx->output ?
            erts_realloc(ERTS_ALC_T_UNICODE_BUFFER, ctx->output, capacity) : alloc_restart(capacity);
        cost_to_proc(p, ctx->used);
        ctx->capacity = capacity;
    }
    sys_memcpy(ctx->output + ctx->used, bytes, n);
    ctx->used += n;
    ctx->characters++;
    return 1;
}

static void unicode_native_read(Eterm bin, Uint offset, byte *out, Uint count)
{
    byte *base;
    Uint start, size;
    ERTS_GET_BITSTRING(bin, base, start, size);
    erts_copy_bits_fwd(base, start + offset * 8, out, 0, count * 8);
}

/* 0 yields, 1 succeeds, 2/3 return error/incomplete, -1/-2 raise badarg/system_limit. */
static int unicode_native_step(Process *p, UnicodeNativeContext *ctx, Eterm *rest)
{
    Sint budget = allowed_iterations(p), initial = budget;
    int result = 1;
    while (!ctx->unwinding && budget-- > 0) {
        if (ctx->in_binary) {
            Uint size = bitstring_size(ctx->binary) / 8;
            byte b[4];
            Uint n, cp, consumed;
            int emitted;
            if (ctx->offset == size) {
                ctx->in_binary = 0;
                ctx->current = ctx->binary_head ? ctx->tail : NIL;
                continue;
            }
            n = MIN((Uint)4, size - ctx->offset);
            unicode_native_read(ctx->binary, ctx->offset, b, n);
            consumed = ctx->utf32 ? 4 : 2;
            cp = 0x110000;
            if (ctx->utf32 && n == 4) {
                cp = ctx->little ? ((Uint)b[0] | ((Uint)b[1] << 8) |
                    ((Uint)b[2] << 16) | ((Uint)b[3] << 24)) :
                    (((Uint)b[0] << 24) | ((Uint)b[1] << 16) | ((Uint)b[2] << 8) | b[3]);
            } else if (!ctx->utf32) {
                ErlSubBits match;
                Eterm storage[heap_bits_size(32)];
                Eterm packed = HEAP_BITSTRING(storage, b, 0, n * 8);
                Eterm scalar;
                erl_sub_bits_init(&match, ERL_SUB_BITS_FLAGS_MATCH_CONTEXT, packed,
                                  ((ErlHeapBits *)storage)->data, 0, n * 8);
                scalar = erts_bs_get_utf16(&match, ctx->little ? BSF_LITTLE : 0);
                if (is_value(scalar)) {
                    cp = unsigned_val(scalar);
                    consumed = cp >= 0x10000 ? 4 : 2;
                }
            }
            if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
                unsigned missing = unicode_native_missing(ctx, b, n);
                Eterm suffix = erts_make_sub_binary(p, ctx->binary, ctx->offset, size - ctx->offset);
                if (missing) {
                    ctx->pending_size = n;
                    ctx->missing = missing;
                    sys_memcpy(ctx->pending, b, n);
                    ctx->in_binary = 0;
                    ctx->current = ctx->binary_head ? ctx->tail : NIL;
                    continue;
                }
                *rest = ctx->binary_head ? unicode_native_cons(p, suffix, ctx->tail) : suffix;
                result = 2;
                break;
            }
            emitted = unicode_native_emit(p, ctx, cp);
            if (emitted < 0) { result = -2; break; }
            ctx->offset += consumed;
            continue;
        }
        if (is_nil(ctx->current)) {
            if (is_list(ctx->frames)) {
                ctx->current = CAR(list_val(ctx->frames));
                ctx->frames = CDR(list_val(ctx->frames));
                continue;
            }
            if (ctx->pending_size) {
                *rest = erts_new_binary_from_data(p, ctx->pending_size, ctx->pending);
                result = 3;
            }
            break;
        }
        {
            Eterm part, tail;
            int head = is_list(ctx->current);
            if (head) {
                part = CAR(list_val(ctx->current));
                tail = CDR(list_val(ctx->current));
                if (is_integer(part)) {
                    if (ctx->pending_size) {
                        Eterm pending = erts_new_binary_from_data(p, ctx->pending_size, ctx->pending);
                        *rest = unicode_native_cons(p, pending, ctx->current);
                        result = 2;
                        break;
                    }
                    if (!is_small(part) || signed_val(part) < 0 ||
                        (signed_val(part) >= 0xd800 && signed_val(part) <= 0xdfff) ||
                        signed_val(part) > 0x10ffff) {
                        Eterm bad = unicode_native_cons(p, part, NIL);
                        *rest = unicode_native_cons(p, bad, tail);
                        result = 2;
                        break;
                    }
                    if (unicode_native_emit(p, ctx, signed_val(part)) < 0) { result = -2; break; }
                    ctx->current = tail;
                    continue;
                }
                if (is_list(part) || is_nil(part)) {
                    ctx->frames = unicode_native_cons(p, tail, ctx->frames);
                    ctx->current = part;
                    continue;
                }
            } else { part = ctx->current; tail = NIL; }
            if (!is_bitstring(part) || TAIL_BITS(bitstring_size(part))) {
                result = -1;
                break;
            }
            {
                Uint size = bitstring_size(part) / 8;
                if (ctx->pending_size) {
                    Uint take = MIN(size, ctx->missing);
                    unicode_native_read(part, 0, ctx->pending + ctx->pending_size, take);
                    ctx->pending_size += take;
                    ctx->missing -= take;
                    if (ctx->missing) {
                        ctx->current = head ? tail : NIL;
                    } else {
                        Eterm combined = erts_new_binary_from_data(p, ctx->pending_size, ctx->pending);
                        Eterm trailing = erts_make_sub_binary(p, part, take, size - take);
                        ctx->current = unicode_native_cons(p, combined,
                            unicode_native_cons(p, trailing, head ? tail : NIL));
                        ctx->pending_size = 0;
                    }
                    continue;
                }
                if (size > 8192) {
                    Eterm first = erts_make_sub_binary(p, part, 0, 8192);
                    Eterm next = erts_make_sub_binary(p, part, 8192, size - 8192);
                    ctx->current = unicode_native_cons(p, first,
                        unicode_native_cons(p, next, head ? tail : NIL));
                    continue;
                }
                ctx->binary = part;
                ctx->tail = tail;
                ctx->binary_head = head;
                ctx->offset = 0;
                ctx->in_binary = 1;
            }
        }
    }
    if (result == 2) {
        ctx->error_rest = *rest;
        ctx->unwinding = 1;
    }
    /* Reconstructing nested error remainders is work too. Keep its cursor in
     * the same GC-owned context so deep nesting cannot bypass the budget. */
    if (ctx->unwinding) {
        while (budget > 0 && is_list(ctx->frames)) {
            Eterm tail = CAR(list_val(ctx->frames));
            ctx->error_rest = unicode_native_cons(p, ctx->error_rest, tail);
            ctx->frames = CDR(list_val(ctx->frames));
            budget--;
        }
        *rest = ctx->error_rest;
        result = 2;
    }
    cost_to_proc(p, initial - MAX(budget, 0));
    if (budget < 0 || (ctx->unwinding && is_list(ctx->frames))) return 0;
    return result;
}

static Eterm unicode_native_run(Process *p, UnicodeNativeContext *ctx, Eterm ref)
{
    Eterm rest = NIL, converted;
    Eterm *hp;
    int result = unicode_native_step(p, ctx, &rest);
    Export *entry = BIF_TRAP_EXPORT(ctx->list_result ? BIF_unicode_characters_to_list_2 :
                                                        BIF_unicode_characters_to_binary_2);
    if (!result) {
        BUMP_ALL_REDS(p);
        BIF_TRAP1(&unicode_native_continue_export, p, ref);
    }
    erts_set_gc_state(p, 1);
    if (result < 0) {
        ERTS_BIF_ERROR_TRAPPED2(p, result == -2 ? SYSTEM_LIMIT : BADARG, entry,
                              ctx->original, ctx->encoding);
    }
    if (ctx->list_result) {
        byte *output = ctx->output;
        ctx->output = NULL;
        if (!output) output = alloc_restart(1);
        return finalize_list_to_list(p, output, rest, 0, ctx->used, ctx->characters,
            result == 2 ? ERTS_UTF8_ERROR : result == 3 ? ERTS_UTF8_INCOMPLETE : ERTS_UTF8_OK,
            allowed_iterations(p), NIL);
    }
    converted = erts_new_binary_from_data(p, ctx->used, ctx->output);
    cost_to_proc(p, ctx->used);
    if (ctx->output) { free_restart(ctx->output); ctx->output = NULL; }
    if (result == 1) return converted;
    hp = HAlloc(p, 4);
    return TUPLE3(hp, result == 2 ? am_error : am_incomplete, converted, rest);
}

static BIF_RETTYPE unicode_native_continue(BIF_ALIST_1)
{
    Binary *bin = erts_magic_ref2bin(BIF_ARG_1);
    ASSERT(ERTS_MAGIC_BIN_DESTRUCTOR(bin) == unicode_native_dtor);
    ASSERT(BIF_P->flags & F_DISABLE_GC);
    return unicode_native_run(BIF_P, ERTS_MAGIC_BIN_DATA(bin), BIF_ARG_1);
}

static Eterm unicode_native_start(Process *p, Eterm data, Eterm encoding, int list_result)
{
    int utf32, little;
    Binary *bin;
    UnicodeNativeContext *ctx;
    Eterm *hp, ref;
    if (!unicode_native_encoding(encoding, &utf32, &little)) {
        BIF_ERROR(p, BADARG);
    }
    bin = erts_create_magic_binary(sizeof(*ctx), unicode_native_dtor);
    ctx = ERTS_MAGIC_BIN_DATA(bin);
    sys_memset(ctx, 0, sizeof(*ctx));
    ctx->original = ctx->current = data;
    ctx->encoding = encoding;
    ctx->frames = NIL;
    ctx->utf32 = utf32; ctx->little = little; ctx->list_result = list_result;
    hp = HAlloc(p, ERTS_MAGIC_REF_THING_SIZE);
    ref = erts_mk_magic_ref(&hp, &MSO(p), bin);
    erts_set_gc_state(p, 0);
    return unicode_native_run(p, ctx, ref);
}

typedef struct { Eterm original; Uint offset; } TextValidationContext;
static int text_validation_dtor(Binary *bin) { return 1; }

BIF_RETTYPE text_from_utf8_modern_1(BIF_ALIST_1)
{
    TextValidationContext local, *ctx = &local;
    int resumed = 0, valid = 1, left = allowed_iterations(BIF_P);
    Sint initial = left;
    Uint size;
    Eterm *hp, result;
    if (is_internal_magic_ref(BIF_ARG_1)) {
        Binary *bin = erts_magic_ref2bin(BIF_ARG_1);
        if (ERTS_MAGIC_BIN_DESTRUCTOR(bin) != text_validation_dtor ||
            !(BIF_P->flags & F_DISABLE_GC)) { BIF_ERROR(BIF_P, BADARG); }
        ctx = ERTS_MAGIC_BIN_DATA(bin); resumed = 1;
    } else {
        if (!is_bitstring(BIF_ARG_1) || TAIL_BITS(bitstring_size(BIF_ARG_1))) {
            BIF_ERROR(BIF_P, BADARG);
        }
        ctx->original = BIF_ARG_1; ctx->offset = 0;
    }
    size = bitstring_size(ctx->original) / 8;
    while (ctx->offset < size && left > 0) {
        byte scratch[4099];
        const byte *end;
        Uint chars, before = ctx->offset;
        Uint count = MIN((Uint)sizeof(scratch), size - before);
        int status;
        unicode_native_read(ctx->original, ctx->offset, scratch, count);
        status = erts_analyze_utf8(scratch, count, &end, &chars, &left);
        ctx->offset += end - scratch;
        if (status == ERTS_UTF8_ERROR ||
            (status == ERTS_UTF8_INCOMPLETE && before + count == size)) {
            valid = 0; break;
        }
    }
    cost_to_proc(BIF_P, initial - left);
    if (valid && ctx->offset < size) {
        Eterm ref = BIF_ARG_1;
        if (!resumed) {
            Binary *bin = erts_create_magic_binary(sizeof(*ctx), text_validation_dtor);
            *(TextValidationContext *)ERTS_MAGIC_BIN_DATA(bin) = *ctx;
            hp = HAlloc(BIF_P, ERTS_MAGIC_REF_THING_SIZE);
            ref = erts_mk_magic_ref(&hp, &MSO(BIF_P), bin);
            erts_set_gc_state(BIF_P, 0);
        }
        BUMP_ALL_REDS(BIF_P);
        BIF_TRAP1(BIF_TRAP_EXPORT(BIF_text_from_utf8_modern_1), BIF_P, ref);
    }
    if (resumed) erts_set_gc_state(BIF_P, 1);
    if (valid) {
        hp = HAlloc(BIF_P, 3);
        BIF_RET(TUPLE2(hp, am_ok, ctx->original));
    }
    hp = HAlloc(BIF_P, MAP1_SZ + 3);
    result = MAP1(hp, am_kind, am_invalid_data); hp += MAP1_SZ;
    BIF_RET(TUPLE2(hp, am_error, result));
}
