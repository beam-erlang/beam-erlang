/*
 * %CopyrightBegin%
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright Ericsson AB 2009-2025. All Rights Reserved.
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

/* A sparse wrapper around zlib with erts memory allocation.
 *
 * erl_zlib_compress2 and erl_zlib_uncompress are erts-adapted versions
 * of the original compress2 and uncompress from zlib-1.2.3. 
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "erl_zlib.h"

#include "sys.h"
#include "erl_alloc.h"
#include "global.h"
#include "bif.h"
#include "erl_binary.h"
#include "erl_bits.h"

voidpf erl_zlib_zalloc_callback (voidpf opaque, unsigned items, unsigned size)
{
    (void) opaque; /* make compiler happy */
    return erts_alloc_fnf(ERTS_ALC_T_ZLIB, items * size);
}

void erl_zlib_zfree_callback (voidpf opaque, voidpf ptr)
{
    (void) opaque; /* make compiler happy */
    erts_free(ERTS_ALC_T_ZLIB, ptr);
}

/*
 * Initialize a z_stream with a source, to later *chunk* data into a destination 
 * Returns Z_OK or Error.
 */
int ZEXPORT erl_zlib_deflate_start(z_stream *streamp, const Bytef* source, 
				   uLong sourceLen, int level) 
{
    streamp->next_in = (Bytef*)source;
    streamp->avail_in = (uInt)sourceLen;
    streamp->total_out = streamp->avail_out = 0;
    streamp->next_out = NULL;
    erl_zlib_alloc_init(streamp);
    return deflateInit(streamp, level);
}
/* 
 * Deflate a chunk, The destination length is the limit.
 * Returns Z_OK if more to process, Z_STREAM_END if we are done.
 */
int ZEXPORT erl_zlib_deflate_chunk(z_stream *streamp, Bytef* dest, uLongf* destLen) 
{
    int err;
    uLongf last_tot = streamp->total_out;

    streamp->next_out = dest;
    streamp->avail_out = (uInt)*destLen;
    
    if ((uLong)streamp->avail_out != *destLen) return Z_BUF_ERROR;
    
    err = deflate(streamp, Z_FINISH);
    *destLen = streamp->total_out - last_tot;
    return err; 
}


/*
 * When we are done, free up the deflate structure
 * Retyurns Z_OK or Error
 */
int ZEXPORT erl_zlib_deflate_finish(z_stream *streamp)
{
    return deflateEnd(streamp);
}

int ZEXPORT erl_zlib_inflate_start(z_stream *streamp, const Bytef* source,
                                   uLong sourceLen)
{
    streamp->next_in = (Bytef*)source;
    streamp->avail_in = (uInt)sourceLen;
    streamp->total_out = streamp->avail_out = 0;
    streamp->next_out = NULL;
    erl_zlib_alloc_init(streamp);
    return inflateInit(streamp);
}
/*
 * Inflate a chunk, The destination length is the limit.
 * Returns Z_OK if more to process, Z_STREAM_END if we are done.
 */
int ZEXPORT erl_zlib_inflate_chunk(z_stream *streamp, Bytef* dest, uLongf* destLen)
{
    int err;
    uLongf last_tot = streamp->total_out;

    streamp->next_out = dest;
    streamp->avail_out = (uInt)*destLen;

    if ((uLong)streamp->avail_out != *destLen) return Z_BUF_ERROR;

    err = inflate(streamp, Z_NO_FLUSH);
    ASSERT(err != Z_STREAM_ERROR);
    *destLen = streamp->total_out - last_tot;
    return err;
}

/*
 * When we are done, free up the inflate structure
 * Retyurns Z_OK or Error
 */
int ZEXPORT erl_zlib_inflate_finish(z_stream *streamp)
{
    return inflateEnd(streamp);
}


int ZEXPORT erl_zlib_compress2 (Bytef* dest, uLongf* destLen,
				const Bytef* source, uLong sourceLen,
				int level)
{
    z_stream stream;
    int err;

    stream.next_in = (Bytef*)source;
    stream.avail_in = (uInt)sourceLen;
#ifdef MAXSEG_64K
    /* Check for source > 64K on 16-bit machine: */
    if ((uLong)stream.avail_in != sourceLen) return Z_BUF_ERROR;
#endif
    stream.next_out = dest;
    stream.avail_out = (uInt)*destLen;
    if ((uLong)stream.avail_out != *destLen) return Z_BUF_ERROR;

    erl_zlib_alloc_init(&stream);

    err = deflateInit(&stream, level);
    if (err != Z_OK) return err;

    err = deflate(&stream, Z_FINISH);
    if (err != Z_STREAM_END) {
        deflateEnd(&stream);
        return err == Z_OK ? Z_BUF_ERROR : err;
    }
    *destLen = stream.total_out;

    err = deflateEnd(&stream);
    return err;
}

int ZEXPORT erl_zlib_uncompress (Bytef* dest, uLongf* destLen,
				 const Bytef* source, uLong sourceLen)
{
    z_stream stream;
    int err;

    stream.next_in = (Bytef*)source;
    stream.avail_in = (uInt)sourceLen;
    /* Check for source > 64K on 16-bit machine: */
    if ((uLong)stream.avail_in != sourceLen) return Z_BUF_ERROR;

    stream.next_out = dest;
    stream.avail_out = (uInt)*destLen;
    if ((uLong)stream.avail_out != *destLen) return Z_BUF_ERROR;

    erl_zlib_alloc_init(&stream);

    err = inflateInit(&stream);
    if (err != Z_OK) return err;

    err = inflate(&stream, Z_FINISH);
    if (err != Z_STREAM_END) {
        inflateEnd(&stream);
        if (err == Z_NEED_DICT || (err == Z_BUF_ERROR && stream.avail_in == 0))
            return Z_DATA_ERROR;
        return err;
    }
    *destLen = stream.total_out;

    err = inflateEnd(&stream);
    return err;
}


/* Gzip preparation is a VM operation: it must not acquire optional modules.
 * The continuation owns codec/input storage; output terms remain GC roots in
 * its tuple argument. Every inflate step has bounded input and output. */
typedef struct {
    z_stream stream;
    Binary *input;
    Uint offset;
    Uint total;
    int initialized;
} BeamfileGunzip;

static int beamfile_gunzip_destroy(Binary *magic)
{
    BeamfileGunzip *ctx = ERTS_MAGIC_BIN_DATA(magic);
    if (ctx->initialized) {
        inflateEnd(&ctx->stream);
        ctx->initialized = 0;
    }
    if (ctx->input) {
        erts_bin_release(ctx->input);
        ctx->input = NULL;
    }
    return 1;
}

BIF_RETTYPE erts_internal_beamfile_gunzip_1(BIF_ALIST_1)
{
    Binary *magic;
    BeamfileGunzip *ctx;
    Eterm ref, accumulated, chunk;
    Eterm *hp;
    byte output[16384];
    Uint fed, consumed, produced;
    int result;

    if (is_tuple_arity(BIF_ARG_1, 2) &&
        is_internal_magic_ref(tuple_val(BIF_ARG_1)[1])) {
        Eterm *tuple = tuple_val(BIF_ARG_1);
        ref = tuple[1];
        accumulated = tuple[2];
        magic = erts_magic_ref2bin(ref);
        if (ERTS_MAGIC_BIN_DESTRUCTOR(magic) != beamfile_gunzip_destroy)
            BIF_ERROR(BIF_P, BADARG);
        ctx = ERTS_MAGIC_BIN_DATA(magic);
        if (!ctx->initialized || !ctx->input)
            BIF_ERROR(BIF_P, BADARG);
    } else {
        const byte *temporary = NULL, *bytes;
        Uint size;
        bytes = erts_get_aligned_binary_bytes(BIF_ARG_1, &size, &temporary);
        if (!bytes)
            BIF_ERROR(BIF_P, BADARG);
        magic = erts_create_magic_binary(sizeof(*ctx), beamfile_gunzip_destroy);
        ctx = ERTS_MAGIC_BIN_DATA(magic);
        sys_memset(ctx, 0, sizeof(*ctx));
        hp = HAlloc(BIF_P, ERTS_MAGIC_REF_THING_SIZE);
        ref = erts_mk_magic_ref(&hp, &MSO(BIF_P), magic);
        accumulated = NIL;
        ctx->input = erts_bin_nrml_alloc_fnf(size);
        if (!ctx->input) {
            erts_free_aligned_binary_bytes(temporary);
            BIF_ERROR(BIF_P, SYSTEM_LIMIT);
        }
        sys_memcpy(ctx->input->orig_bytes, bytes, size);
        erts_free_aligned_binary_bytes(temporary);
        erl_zlib_alloc_init(&ctx->stream);
        result = inflateInit2(&ctx->stream, 16 + MAX_WBITS);
        if (result != Z_OK) {
            beamfile_gunzip_destroy(magic);
            BIF_ERROR(BIF_P, SYSTEM_LIMIT);
        }
        ctx->initialized = 1;
    }

    fed = MIN((Uint)8192, ctx->input->orig_size - ctx->offset);
    ctx->stream.next_in = (Bytef *)ctx->input->orig_bytes + ctx->offset;
    ctx->stream.avail_in = (uInt)fed;
    ctx->stream.next_out = output;
    ctx->stream.avail_out = sizeof(output);
    result = inflate(&ctx->stream, Z_NO_FLUSH);
    consumed = fed - ctx->stream.avail_in;
    produced = sizeof(output) - ctx->stream.avail_out;
    ctx->offset += consumed;
    BUMP_REDS(BIF_P, MAX((Uint)1, (consumed + produced) / 128));
    if (produced > ERTS_UWORD_MAX / CHAR_BIT - ctx->total) {
        beamfile_gunzip_destroy(magic);
        BIF_ERROR(BIF_P, SYSTEM_LIMIT);
    }
    ctx->total += produced;
    if (produced) {
        chunk = erts_new_binary_from_data(BIF_P, produced, output);
        hp = HAlloc(BIF_P, 4);
        /* Nested iodata preserves order without a library list reversal. */
        accumulated = CONS(hp, accumulated, CONS(hp + 2, chunk, NIL));
    }
    if (result == Z_STREAM_END) {
        if (ctx->offset == ctx->input->orig_size) {
            beamfile_gunzip_destroy(magic);
            BIF_TRAP1(BIF_TRAP_EXPORT(BIF_iolist_to_binary_1), BIF_P, accumulated);
        }
        result = inflateReset(&ctx->stream);
    } else if (result == Z_OK && !consumed && !produced) {
        result = Z_DATA_ERROR;
    }
    if (result != Z_OK) {
        beamfile_gunzip_destroy(magic);
        BIF_ERROR(BIF_P, BADARG);
    }
    hp = HAlloc(BIF_P, 3);
    chunk = TUPLE2(hp, ref, accumulated);
    BIF_TRAP1(BIF_TRAP_EXPORT(BIF_erts_internal_beamfile_gunzip_1), BIF_P, chunk);
}
