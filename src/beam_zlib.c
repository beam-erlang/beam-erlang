/* SPDX-License-Identifier: Apache-2.0 */
/* Public forwarding interface to the runtime's sole private codec. */
#include <beam/zlib.h>
#include "erts/emulator/zlib/zlib.h"
#include <limits.h>

_Static_assert(BEAM_ZLIB_NO_FLUSH == Z_NO_FLUSH, "zlib NO_FLUSH value");
_Static_assert(BEAM_ZLIB_PARTIAL_FLUSH == Z_PARTIAL_FLUSH, "zlib PARTIAL_FLUSH value");
_Static_assert(BEAM_ZLIB_SYNC_FLUSH == Z_SYNC_FLUSH, "zlib SYNC_FLUSH value");
_Static_assert(BEAM_ZLIB_FULL_FLUSH == Z_FULL_FLUSH, "zlib FULL_FLUSH value");
_Static_assert(BEAM_ZLIB_FINISH == Z_FINISH, "zlib FINISH value");
_Static_assert(BEAM_ZLIB_BLOCK == Z_BLOCK, "zlib BLOCK value");
_Static_assert(BEAM_ZLIB_TREES == Z_TREES, "zlib TREES value");
_Static_assert(BEAM_ZLIB_OK == Z_OK, "zlib OK value");
_Static_assert(BEAM_ZLIB_STREAM_END == Z_STREAM_END, "zlib STREAM_END value");
_Static_assert(BEAM_ZLIB_NEED_DICT == Z_NEED_DICT, "zlib NEED_DICT value");
_Static_assert(BEAM_ZLIB_ERRNO == Z_ERRNO, "zlib ERRNO value");
_Static_assert(BEAM_ZLIB_STREAM_ERROR == Z_STREAM_ERROR, "zlib STREAM_ERROR value");
_Static_assert(BEAM_ZLIB_DATA_ERROR == Z_DATA_ERROR, "zlib DATA_ERROR value");
_Static_assert(BEAM_ZLIB_MEM_ERROR == Z_MEM_ERROR, "zlib MEM_ERROR value");
_Static_assert(BEAM_ZLIB_BUF_ERROR == Z_BUF_ERROR, "zlib BUF_ERROR value");
_Static_assert(BEAM_ZLIB_VERSION_ERROR == Z_VERSION_ERROR, "zlib VERSION_ERROR value");
_Static_assert(BEAM_ZLIB_NO_COMPRESSION == Z_NO_COMPRESSION, "zlib NO_COMPRESSION value");
_Static_assert(BEAM_ZLIB_BEST_SPEED == Z_BEST_SPEED, "zlib BEST_SPEED value");
_Static_assert(BEAM_ZLIB_BEST_COMPRESSION == Z_BEST_COMPRESSION, "zlib BEST_COMPRESSION value");
_Static_assert(BEAM_ZLIB_DEFAULT_COMPRESSION == Z_DEFAULT_COMPRESSION, "zlib DEFAULT_COMPRESSION value");
_Static_assert(BEAM_ZLIB_FILTERED == Z_FILTERED, "zlib FILTERED value");
_Static_assert(BEAM_ZLIB_HUFFMAN_ONLY == Z_HUFFMAN_ONLY, "zlib HUFFMAN_ONLY value");
_Static_assert(BEAM_ZLIB_RLE == Z_RLE, "zlib RLE value");
_Static_assert(BEAM_ZLIB_FIXED == Z_FIXED, "zlib FIXED value");
_Static_assert(BEAM_ZLIB_DEFAULT_STRATEGY == Z_DEFAULT_STRATEGY, "zlib DEFAULT_STRATEGY value");
_Static_assert(BEAM_ZLIB_BINARY == Z_BINARY, "zlib BINARY value");
_Static_assert(BEAM_ZLIB_TEXT == Z_TEXT, "zlib TEXT value");
_Static_assert(BEAM_ZLIB_ASCII == Z_ASCII, "zlib ASCII value");
_Static_assert(BEAM_ZLIB_UNKNOWN == Z_UNKNOWN, "zlib UNKNOWN value");
_Static_assert(BEAM_ZLIB_DEFLATED == Z_DEFLATED, "zlib DEFLATED value");
_Static_assert(BEAM_ZLIB_NULL == Z_NULL, "zlib NULL value");
_Static_assert((z_off64_t)-1 < 0, "signed checksum lengths");
_Static_assert(sizeof(z_off64_t) >= sizeof(int64_t), "64-bit checksum lengths");
_Static_assert(sizeof(uInt) == sizeof(beam_zlib_uint), "zlib unsigned width");
_Static_assert(sizeof(uLong) == sizeof(beam_zlib_ulong), "zlib total width");

const char * beam_zlib_version(void)
{
    return zlibVersion();
}

int beam_zlib_deflate(beam_zlib_stream * strm, int flush)
{
    return deflate(strm, flush);
}

int beam_zlib_deflate_end(beam_zlib_stream * strm)
{
    return deflateEnd(strm);
}

int beam_zlib_inflate(beam_zlib_stream * strm, int flush)
{
    return inflate(strm, flush);
}

int beam_zlib_inflate_end(beam_zlib_stream * strm)
{
    return inflateEnd(strm);
}

int beam_zlib_deflate_set_dictionary(beam_zlib_stream * strm, const beam_zlib_byte *dictionary, beam_zlib_uint dictLength)
{
    return deflateSetDictionary(strm, dictionary, dictLength);
}

int beam_zlib_deflate_get_dictionary(beam_zlib_stream * strm, beam_zlib_byte *dictionary, beam_zlib_uint *dictLength)
{
    return deflateGetDictionary(strm, dictionary, dictLength);
}

int beam_zlib_deflate_copy(beam_zlib_stream * dest, beam_zlib_stream * source)
{
    return deflateCopy(dest, source);
}

int beam_zlib_deflate_reset(beam_zlib_stream * strm)
{
    return deflateReset(strm);
}

int beam_zlib_deflate_params(beam_zlib_stream * strm, int level, int strategy)
{
    return deflateParams(strm, level, strategy);
}

int beam_zlib_deflate_tune(beam_zlib_stream * strm, int good_length, int max_lazy, int nice_length, int max_chain)
{
    return deflateTune(strm, good_length, max_lazy, nice_length, max_chain);
}

beam_zlib_ulong beam_zlib_deflate_bound(beam_zlib_stream * strm, beam_zlib_ulong sourceLen)
{
    return deflateBound(strm, sourceLen);
}

size_t beam_zlib_deflate_bound_z(beam_zlib_stream * strm, size_t sourceLen)
{
    return deflateBound_z(strm, sourceLen);
}

int beam_zlib_deflate_pending(beam_zlib_stream * strm, unsigned *pending, int *bits)
{
    return deflatePending(strm, pending, bits);
}

int beam_zlib_deflate_used(beam_zlib_stream * strm, int *bits)
{
    return deflateUsed(strm, bits);
}

int beam_zlib_deflate_prime(beam_zlib_stream * strm, int bits, int value)
{
    return deflatePrime(strm, bits, value);
}

int beam_zlib_deflate_set_header(beam_zlib_stream * strm, beam_zlib_gzip_header * head)
{
    return deflateSetHeader(strm, head);
}

int beam_zlib_inflate_set_dictionary(beam_zlib_stream * strm, const beam_zlib_byte *dictionary, beam_zlib_uint dictLength)
{
    return inflateSetDictionary(strm, dictionary, dictLength);
}

int beam_zlib_inflate_get_dictionary(beam_zlib_stream * strm, beam_zlib_byte *dictionary, beam_zlib_uint *dictLength)
{
    return inflateGetDictionary(strm, dictionary, dictLength);
}

int beam_zlib_inflate_sync(beam_zlib_stream * strm)
{
    return inflateSync(strm);
}

int beam_zlib_inflate_copy(beam_zlib_stream * dest, beam_zlib_stream * source)
{
    return inflateCopy(dest, source);
}

int beam_zlib_inflate_reset(beam_zlib_stream * strm)
{
    return inflateReset(strm);
}

int beam_zlib_inflate_reset2(beam_zlib_stream * strm, int windowBits)
{
    return inflateReset2(strm, windowBits);
}

int beam_zlib_inflate_prime(beam_zlib_stream * strm, int bits, int value)
{
    return inflatePrime(strm, bits, value);
}

long beam_zlib_inflate_mark(beam_zlib_stream * strm)
{
    return inflateMark(strm);
}

int beam_zlib_inflate_get_header(beam_zlib_stream * strm, beam_zlib_gzip_header * head)
{
    return inflateGetHeader(strm, head);
}

beam_zlib_ulong beam_zlib_compile_flags(void)
{
    return zlibCompileFlags();
}

int beam_zlib_compress(beam_zlib_byte *dest, beam_zlib_ulong *destLen, const beam_zlib_byte *source, beam_zlib_ulong sourceLen)
{
    return compress(dest, destLen, source, sourceLen);
}

int beam_zlib_compress_z(beam_zlib_byte *dest, size_t *destLen, const beam_zlib_byte *source, size_t sourceLen)
{
    return compress_z(dest, destLen, source, sourceLen);
}

int beam_zlib_compress2(beam_zlib_byte *dest, beam_zlib_ulong *destLen, const beam_zlib_byte *source, beam_zlib_ulong sourceLen, int level)
{
    return compress2(dest, destLen, source, sourceLen, level);
}

int beam_zlib_compress2_z(beam_zlib_byte *dest, size_t *destLen, const beam_zlib_byte *source, size_t sourceLen, int level)
{
    return compress2_z(dest, destLen, source, sourceLen, level);
}

beam_zlib_ulong beam_zlib_compress_bound(beam_zlib_ulong sourceLen)
{
    return compressBound(sourceLen);
}

size_t beam_zlib_compress_bound_z(size_t sourceLen)
{
    return compressBound_z(sourceLen);
}

int beam_zlib_uncompress(beam_zlib_byte *dest, beam_zlib_ulong *destLen, const beam_zlib_byte *source, beam_zlib_ulong sourceLen)
{
    return uncompress(dest, destLen, source, sourceLen);
}

int beam_zlib_uncompress_z(beam_zlib_byte *dest, size_t *destLen, const beam_zlib_byte *source, size_t sourceLen)
{
    return uncompress_z(dest, destLen, source, sourceLen);
}

int beam_zlib_uncompress2(beam_zlib_byte *dest, beam_zlib_ulong *destLen, const beam_zlib_byte *source, beam_zlib_ulong *sourceLen)
{
    return uncompress2(dest, destLen, source, sourceLen);
}

int beam_zlib_uncompress2_z(beam_zlib_byte *dest, size_t *destLen, const beam_zlib_byte *source, size_t *sourceLen)
{
    return uncompress2_z(dest, destLen, source, sourceLen);
}

beam_zlib_ulong beam_zlib_adler32(beam_zlib_ulong adler, const beam_zlib_byte *buf, beam_zlib_uint len)
{
    return adler32(adler, buf, len);
}

beam_zlib_ulong beam_zlib_adler32_z(beam_zlib_ulong adler, const beam_zlib_byte *buf, size_t len)
{
    return adler32_z(adler, buf, len);
}

beam_zlib_ulong beam_zlib_crc32(beam_zlib_ulong crc, const beam_zlib_byte *buf, beam_zlib_uint len)
{
    return crc32(crc, buf, len);
}

beam_zlib_ulong beam_zlib_crc32_z(beam_zlib_ulong crc, const beam_zlib_byte *buf, size_t len)
{
    return crc32_z(crc, buf, len);
}

beam_zlib_ulong beam_zlib_crc32_combine_op(beam_zlib_ulong crc1, beam_zlib_ulong crc2, beam_zlib_ulong op)
{
    return crc32_combine_op(crc1, crc2, op);
}

int beam_zlib_deflate_init_(beam_zlib_stream * strm, int level, unsigned api_version, size_t stream_size)
{
    if (api_version != BEAM_ZLIB_API_VERSION || stream_size != sizeof(beam_zlib_stream))
        return BEAM_ZLIB_VERSION_ERROR;
    return deflateInit_(strm, level, ZLIB_VERSION, (int)sizeof(z_stream));
}

int beam_zlib_inflate_init_(beam_zlib_stream * strm, unsigned api_version, size_t stream_size)
{
    if (api_version != BEAM_ZLIB_API_VERSION || stream_size != sizeof(beam_zlib_stream))
        return BEAM_ZLIB_VERSION_ERROR;
    return inflateInit_(strm, ZLIB_VERSION, (int)sizeof(z_stream));
}

int beam_zlib_deflate_init2_(beam_zlib_stream * strm, int level, int method, int windowBits, int memLevel, int strategy, unsigned api_version, size_t stream_size)
{
    if (api_version != BEAM_ZLIB_API_VERSION || stream_size != sizeof(beam_zlib_stream))
        return BEAM_ZLIB_VERSION_ERROR;
    return deflateInit2_(strm, level, method, windowBits, memLevel, strategy, ZLIB_VERSION, (int)sizeof(z_stream));
}

int beam_zlib_inflate_init2_(beam_zlib_stream * strm, int windowBits, unsigned api_version, size_t stream_size)
{
    if (api_version != BEAM_ZLIB_API_VERSION || stream_size != sizeof(beam_zlib_stream))
        return BEAM_ZLIB_VERSION_ERROR;
    return inflateInit2_(strm, windowBits, ZLIB_VERSION, (int)sizeof(z_stream));
}

beam_zlib_ulong beam_zlib_adler32_combine64(beam_zlib_ulong first, beam_zlib_ulong second, int64_t length)
{
    return adler32_combine64(first, second, length);
}

beam_zlib_ulong beam_zlib_crc32_combine64(beam_zlib_ulong first, beam_zlib_ulong second, int64_t length)
{
    return crc32_combine64(first, second, length);
}

beam_zlib_ulong beam_zlib_crc32_combine_gen64(int64_t length)
{
    return crc32_combine_gen64(length);
}

beam_zlib_ulong beam_zlib_adler32_combine(beam_zlib_ulong first, beam_zlib_ulong second, int64_t length)
{
    return adler32_combine64(first, second, length);
}

beam_zlib_ulong beam_zlib_crc32_combine(beam_zlib_ulong first, beam_zlib_ulong second, int64_t length)
{
    return crc32_combine64(first, second, length);
}

beam_zlib_ulong beam_zlib_crc32_combine_gen(int64_t length)
{
    return crc32_combine_gen64(length);
}

