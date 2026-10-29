/* zlib.h -- interface of the 'zlib' general purpose compression library
  version 1.3.2, February 17th, 2026

  Copyright (C) 1995-2026 Jean-loup Gailly and Mark Adler

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  Jean-loup Gailly        Mark Adler
  jloup@gzip.org          madler@alumni.caltech.edu


  The data format used by the zlib library is described by RFCs (Request for
  Comments) 1950 to 1952 at https://datatracker.ietf.org/doc/html/rfc1950
  (zlib format), rfc1951 (deflate format) and rfc1952 (gzip format).
*/

/* BEAM namespaced adaptation of the documented in-memory zlib interface. */
#ifndef BEAM_ZLIB_H
#define BEAM_ZLIB_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define BEAM_ZLIB_API_VERSION 1u

/* Calls require no VM. Streams and borrowed gzip metadata belong to their
 * callers. Allocators must remain valid until the corresponding end call.
 * Independent streams may be used concurrently with thread-safe allocators.
 * Input bytes and error messages must not be modified through their pointers.
 * This preview interface is tied to the selected SDK; codec version is separate.
 */
typedef unsigned char beam_zlib_byte;
typedef unsigned int beam_zlib_uint;
typedef unsigned long beam_zlib_ulong;
typedef void *(*beam_zlib_alloc_fn)(void *, unsigned int, unsigned int);
typedef void (*beam_zlib_free_fn)(void *, void *);
struct beam_zlib_internal_state;
typedef struct beam_zlib_stream {
    beam_zlib_byte *next_in;
    beam_zlib_uint avail_in;
    beam_zlib_ulong total_in;
    beam_zlib_byte *next_out;
    beam_zlib_uint avail_out;
    beam_zlib_ulong total_out;
    char *msg;
    struct beam_zlib_internal_state *state;
    beam_zlib_alloc_fn zalloc;
    beam_zlib_free_fn zfree;
    void *opaque;
    int data_type;
    beam_zlib_ulong adler;
    beam_zlib_ulong reserved;
} beam_zlib_stream;

typedef struct beam_zlib_gzip_header {
    int text;
    beam_zlib_ulong time;
    int xflags;
    int os;
    beam_zlib_byte *extra;
    beam_zlib_uint extra_len;
    beam_zlib_uint extra_max;
    beam_zlib_byte *name;
    beam_zlib_uint name_max;
    beam_zlib_byte *comment;
    beam_zlib_uint comm_max;
    int hcrc;
    int done;
} beam_zlib_gzip_header;

#define BEAM_ZLIB_NO_FLUSH 0
#define BEAM_ZLIB_PARTIAL_FLUSH 1
#define BEAM_ZLIB_SYNC_FLUSH 2
#define BEAM_ZLIB_FULL_FLUSH 3
#define BEAM_ZLIB_FINISH 4
#define BEAM_ZLIB_BLOCK 5
#define BEAM_ZLIB_TREES 6
#define BEAM_ZLIB_OK 0
#define BEAM_ZLIB_STREAM_END 1
#define BEAM_ZLIB_NEED_DICT 2
#define BEAM_ZLIB_ERRNO (-1)
#define BEAM_ZLIB_STREAM_ERROR (-2)
#define BEAM_ZLIB_DATA_ERROR (-3)
#define BEAM_ZLIB_MEM_ERROR (-4)
#define BEAM_ZLIB_BUF_ERROR (-5)
#define BEAM_ZLIB_VERSION_ERROR (-6)
#define BEAM_ZLIB_NO_COMPRESSION 0
#define BEAM_ZLIB_BEST_SPEED 1
#define BEAM_ZLIB_BEST_COMPRESSION 9
#define BEAM_ZLIB_DEFAULT_COMPRESSION (-1)
#define BEAM_ZLIB_FILTERED 1
#define BEAM_ZLIB_HUFFMAN_ONLY 2
#define BEAM_ZLIB_RLE 3
#define BEAM_ZLIB_FIXED 4
#define BEAM_ZLIB_DEFAULT_STRATEGY 0
#define BEAM_ZLIB_BINARY 0
#define BEAM_ZLIB_TEXT 1
#define BEAM_ZLIB_ASCII BEAM_ZLIB_TEXT
#define BEAM_ZLIB_UNKNOWN 2
#define BEAM_ZLIB_DEFLATED 8
#define BEAM_ZLIB_NULL 0

const char * beam_zlib_version(void);
int beam_zlib_deflate(beam_zlib_stream * strm, int flush);
int beam_zlib_deflate_end(beam_zlib_stream * strm);
int beam_zlib_inflate(beam_zlib_stream * strm, int flush);
int beam_zlib_inflate_end(beam_zlib_stream * strm);
int beam_zlib_deflate_set_dictionary(beam_zlib_stream * strm, const beam_zlib_byte *dictionary, beam_zlib_uint dictLength);
int beam_zlib_deflate_get_dictionary(beam_zlib_stream * strm, beam_zlib_byte *dictionary, beam_zlib_uint *dictLength);
int beam_zlib_deflate_copy(beam_zlib_stream * dest, beam_zlib_stream * source);
int beam_zlib_deflate_reset(beam_zlib_stream * strm);
int beam_zlib_deflate_params(beam_zlib_stream * strm, int level, int strategy);
int beam_zlib_deflate_tune(beam_zlib_stream * strm, int good_length, int max_lazy, int nice_length, int max_chain);
beam_zlib_ulong beam_zlib_deflate_bound(beam_zlib_stream * strm, beam_zlib_ulong sourceLen);
size_t beam_zlib_deflate_bound_z(beam_zlib_stream * strm, size_t sourceLen);
int beam_zlib_deflate_pending(beam_zlib_stream * strm, unsigned *pending, int *bits);
int beam_zlib_deflate_used(beam_zlib_stream * strm, int *bits);
int beam_zlib_deflate_prime(beam_zlib_stream * strm, int bits, int value);
int beam_zlib_deflate_set_header(beam_zlib_stream * strm, beam_zlib_gzip_header * head);
int beam_zlib_inflate_set_dictionary(beam_zlib_stream * strm, const beam_zlib_byte *dictionary, beam_zlib_uint dictLength);
int beam_zlib_inflate_get_dictionary(beam_zlib_stream * strm, beam_zlib_byte *dictionary, beam_zlib_uint *dictLength);
int beam_zlib_inflate_sync(beam_zlib_stream * strm);
int beam_zlib_inflate_copy(beam_zlib_stream * dest, beam_zlib_stream * source);
int beam_zlib_inflate_reset(beam_zlib_stream * strm);
int beam_zlib_inflate_reset2(beam_zlib_stream * strm, int windowBits);
int beam_zlib_inflate_prime(beam_zlib_stream * strm, int bits, int value);
long beam_zlib_inflate_mark(beam_zlib_stream * strm);
int beam_zlib_inflate_get_header(beam_zlib_stream * strm, beam_zlib_gzip_header * head);
beam_zlib_ulong beam_zlib_compile_flags(void);
int beam_zlib_compress(beam_zlib_byte *dest, beam_zlib_ulong *destLen, const beam_zlib_byte *source, beam_zlib_ulong sourceLen);
int beam_zlib_compress_z(beam_zlib_byte *dest, size_t *destLen, const beam_zlib_byte *source, size_t sourceLen);
int beam_zlib_compress2(beam_zlib_byte *dest, beam_zlib_ulong *destLen, const beam_zlib_byte *source, beam_zlib_ulong sourceLen, int level);
int beam_zlib_compress2_z(beam_zlib_byte *dest, size_t *destLen, const beam_zlib_byte *source, size_t sourceLen, int level);
beam_zlib_ulong beam_zlib_compress_bound(beam_zlib_ulong sourceLen);
size_t beam_zlib_compress_bound_z(size_t sourceLen);
int beam_zlib_uncompress(beam_zlib_byte *dest, beam_zlib_ulong *destLen, const beam_zlib_byte *source, beam_zlib_ulong sourceLen);
int beam_zlib_uncompress_z(beam_zlib_byte *dest, size_t *destLen, const beam_zlib_byte *source, size_t sourceLen);
int beam_zlib_uncompress2(beam_zlib_byte *dest, beam_zlib_ulong *destLen, const beam_zlib_byte *source, beam_zlib_ulong *sourceLen);
int beam_zlib_uncompress2_z(beam_zlib_byte *dest, size_t *destLen, const beam_zlib_byte *source, size_t *sourceLen);
beam_zlib_ulong beam_zlib_adler32(beam_zlib_ulong adler, const beam_zlib_byte *buf, beam_zlib_uint len);
beam_zlib_ulong beam_zlib_adler32_z(beam_zlib_ulong adler, const beam_zlib_byte *buf, size_t len);
beam_zlib_ulong beam_zlib_crc32(beam_zlib_ulong crc, const beam_zlib_byte *buf, beam_zlib_uint len);
beam_zlib_ulong beam_zlib_crc32_z(beam_zlib_ulong crc, const beam_zlib_byte *buf, size_t len);
beam_zlib_ulong beam_zlib_crc32_combine_op(beam_zlib_ulong crc1, beam_zlib_ulong crc2, beam_zlib_ulong op);
int beam_zlib_deflate_init_(beam_zlib_stream * strm, int level, unsigned api_version, size_t stream_size);
int beam_zlib_inflate_init_(beam_zlib_stream * strm, unsigned api_version, size_t stream_size);
int beam_zlib_deflate_init2_(beam_zlib_stream * strm, int level, int method, int windowBits, int memLevel, int strategy, unsigned api_version, size_t stream_size);
int beam_zlib_inflate_init2_(beam_zlib_stream * strm, int windowBits, unsigned api_version, size_t stream_size);
beam_zlib_ulong beam_zlib_adler32_combine64(beam_zlib_ulong first, beam_zlib_ulong second, int64_t length);
beam_zlib_ulong beam_zlib_crc32_combine64(beam_zlib_ulong first, beam_zlib_ulong second, int64_t length);
beam_zlib_ulong beam_zlib_crc32_combine_gen64(int64_t length);
beam_zlib_ulong beam_zlib_adler32_combine(beam_zlib_ulong first, beam_zlib_ulong second, int64_t length);
beam_zlib_ulong beam_zlib_crc32_combine(beam_zlib_ulong first, beam_zlib_ulong second, int64_t length);
beam_zlib_ulong beam_zlib_crc32_combine_gen(int64_t length);

/* Checked initialization: mismatches leave the supplied stream unchanged. */
#define beam_zlib_deflate_init(s, level) \
    beam_zlib_deflate_init_((s), (level), BEAM_ZLIB_API_VERSION, sizeof(beam_zlib_stream))
#define beam_zlib_deflate_init2(s, level, method, bits, memory, strategy) \
    beam_zlib_deflate_init2_((s), (level), (method), (bits), (memory), (strategy), BEAM_ZLIB_API_VERSION, sizeof(beam_zlib_stream))
#define beam_zlib_inflate_init(s) \
    beam_zlib_inflate_init_((s), BEAM_ZLIB_API_VERSION, sizeof(beam_zlib_stream))
#define beam_zlib_inflate_init2(s, bits) \
    beam_zlib_inflate_init2_((s), (bits), BEAM_ZLIB_API_VERSION, sizeof(beam_zlib_stream))
#ifdef __cplusplus
}
#endif
#endif
