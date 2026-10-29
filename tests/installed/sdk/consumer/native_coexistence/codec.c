/* SPDX-License-Identifier: Apache-2.0 */
#include <beam/zlib.h>
#include <zlib.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "codec:%d: %s\n", __LINE__, #x); return 1; } } while (0)

/* Every declared entry must resolve from either installed runtime linkage. */
static void (*volatile entries[])(void) = {
    (void (*)(void))beam_zlib_adler32,
    (void (*)(void))beam_zlib_adler32_combine,
    (void (*)(void))beam_zlib_adler32_combine64,
    (void (*)(void))beam_zlib_adler32_z,
    (void (*)(void))beam_zlib_compile_flags,
    (void (*)(void))beam_zlib_compress,
    (void (*)(void))beam_zlib_compress2,
    (void (*)(void))beam_zlib_compress2_z,
    (void (*)(void))beam_zlib_compress_bound,
    (void (*)(void))beam_zlib_compress_bound_z,
    (void (*)(void))beam_zlib_compress_z,
    (void (*)(void))beam_zlib_crc32,
    (void (*)(void))beam_zlib_crc32_combine,
    (void (*)(void))beam_zlib_crc32_combine64,
    (void (*)(void))beam_zlib_crc32_combine_gen,
    (void (*)(void))beam_zlib_crc32_combine_gen64,
    (void (*)(void))beam_zlib_crc32_combine_op,
    (void (*)(void))beam_zlib_crc32_z,
    (void (*)(void))beam_zlib_deflate,
    (void (*)(void))beam_zlib_deflate_bound,
    (void (*)(void))beam_zlib_deflate_bound_z,
    (void (*)(void))beam_zlib_deflate_copy,
    (void (*)(void))beam_zlib_deflate_end,
    (void (*)(void))beam_zlib_deflate_get_dictionary,
    (void (*)(void))beam_zlib_deflate_init2_,
    (void (*)(void))beam_zlib_deflate_init_,
    (void (*)(void))beam_zlib_deflate_params,
    (void (*)(void))beam_zlib_deflate_pending,
    (void (*)(void))beam_zlib_deflate_prime,
    (void (*)(void))beam_zlib_deflate_reset,
    (void (*)(void))beam_zlib_deflate_set_dictionary,
    (void (*)(void))beam_zlib_deflate_set_header,
    (void (*)(void))beam_zlib_deflate_tune,
    (void (*)(void))beam_zlib_deflate_used,
    (void (*)(void))beam_zlib_inflate,
    (void (*)(void))beam_zlib_inflate_copy,
    (void (*)(void))beam_zlib_inflate_end,
    (void (*)(void))beam_zlib_inflate_get_dictionary,
    (void (*)(void))beam_zlib_inflate_get_header,
    (void (*)(void))beam_zlib_inflate_init2_,
    (void (*)(void))beam_zlib_inflate_init_,
    (void (*)(void))beam_zlib_inflate_mark,
    (void (*)(void))beam_zlib_inflate_prime,
    (void (*)(void))beam_zlib_inflate_reset,
    (void (*)(void))beam_zlib_inflate_reset2,
    (void (*)(void))beam_zlib_inflate_set_dictionary,
    (void (*)(void))beam_zlib_inflate_sync,
    (void (*)(void))beam_zlib_uncompress,
    (void (*)(void))beam_zlib_uncompress2,
    (void (*)(void))beam_zlib_uncompress2_z,
    (void (*)(void))beam_zlib_uncompress_z,
    (void (*)(void))beam_zlib_version,
};

typedef struct { unsigned live, allowance; } allocations;
static void *allocate(void *opaque, unsigned items, unsigned size)
{
    allocations *a = opaque;
    if (!a->allowance) return NULL;
    --a->allowance;
    void *p = calloc(items, size);
    if (p) ++a->live;
    return p;
}
static void release(void *opaque, void *p)
{
    allocations *a = opaque;
    if (p) { --a->live; free(p); }
}

int beam_test_codec(void)
{
    unsigned char input[] = "dictionary dictionary: independent codec streams";
    unsigned char dictionary[] = "dictionary";
    unsigned char compressed[512], output[512], copied[512];
    size_t compressed_size = sizeof(compressed), output_size = sizeof(output);
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i) REQUIRE(entries[i]);
    REQUIRE(beam_zlib_version()[0]);
    unsigned long flags = beam_zlib_compile_flags();
    REQUIRE((flags & 3) == 1 && ((flags >> 2) & 3) == (sizeof(beam_zlib_ulong) == 8 ? 2 : 1));
    REQUIRE(beam_zlib_compress_bound_z(sizeof(input)) >= sizeof(input));
    REQUIRE(beam_zlib_compress2_z(compressed, &compressed_size, input, sizeof(input), BEAM_ZLIB_BEST_COMPRESSION) == BEAM_ZLIB_OK);
    REQUIRE(beam_zlib_uncompress_z(output, &output_size, compressed, compressed_size) == BEAM_ZLIB_OK);
    REQUIRE(output_size == sizeof(input) && !memcmp(output, input, sizeof(input)));
    /* Exchange bytes with the separately linked system implementation. */
    uLongf system_size = sizeof(output);
    REQUIRE(uncompress(output, &system_size, compressed, compressed_size) == Z_OK);
    REQUIRE(system_size == sizeof(input));
    uLongf system_compressed = sizeof(compressed);
    REQUIRE(compress(compressed, &system_compressed, input, sizeof(input)) == Z_OK);
    size_t consumed = system_compressed;
    output_size = sizeof(output);
    REQUIRE(beam_zlib_uncompress2_z(output, &output_size, compressed, &consumed) == BEAM_ZLIB_OK);
    REQUIRE(consumed == system_compressed && output_size == sizeof(input) && !memcmp(output, input, sizeof(input)));
    REQUIRE(beam_zlib_crc32_z(0, input, sizeof(input)) == crc32(0, input, sizeof(input)));
    beam_zlib_ulong first = beam_zlib_crc32(0, input, 10);
    beam_zlib_ulong second = beam_zlib_crc32(0, input + 10, sizeof(input) - 10);
    REQUIRE(beam_zlib_crc32_combine(first, second, sizeof(input) - 10) == beam_zlib_crc32_z(0, input, sizeof(input)));
    REQUIRE(beam_zlib_crc32_combine_op(first, second, beam_zlib_crc32_combine_gen64(sizeof(input)-10)) == beam_zlib_crc32_z(0, input, sizeof(input)));
    REQUIRE(beam_zlib_crc32_combine(first, second, INT64_C(1) << 33) ==
            beam_zlib_crc32_combine64(first, second, INT64_C(1) << 33));
    REQUIRE(beam_zlib_adler32_combine(first, second, INT64_C(1) << 33) ==
            beam_zlib_adler32_combine64(first, second, INT64_C(1) << 33));
    REQUIRE(beam_zlib_adler32_z(1, input, sizeof(input)) == adler32(1, input, sizeof(input)));
    REQUIRE(beam_zlib_adler32_combine64(beam_zlib_adler32(1, input, 10), beam_zlib_adler32(1, input+10, sizeof(input)-10), sizeof(input)-10) == beam_zlib_adler32_z(1,input,sizeof(input)));

    beam_zlib_stream stream = {0}, clone = {0}, inflater = {0};
    allocations a = {0, 0};
    stream.zalloc = allocate; stream.zfree = release; stream.opaque = &a;
    beam_zlib_stream unchanged;
    memcpy(&unchanged, &stream, sizeof(stream));
    REQUIRE(beam_zlib_deflate_init_(&stream, 6, BEAM_ZLIB_API_VERSION + 1, sizeof(stream)) == BEAM_ZLIB_VERSION_ERROR);
    REQUIRE(!memcmp(&stream, &unchanged, sizeof(stream)));
    REQUIRE(beam_zlib_inflate_init_(&stream, BEAM_ZLIB_API_VERSION, sizeof(stream)-1) == BEAM_ZLIB_VERSION_ERROR);
    REQUIRE(!memcmp(&stream, &unchanged, sizeof(stream)));
    for (unsigned budget = 0; budget < 5; ++budget) {
        a.allowance = budget;
        REQUIRE(beam_zlib_deflate_init(&stream, 6) == BEAM_ZLIB_MEM_ERROR);
        REQUIRE(!a.live && !stream.state);
    }
    a.allowance = 100;
    REQUIRE(beam_zlib_deflate_init(&stream, 6) == BEAM_ZLIB_OK);
    REQUIRE(beam_zlib_deflate_set_dictionary(&stream, dictionary, sizeof(dictionary)) == BEAM_ZLIB_OK);
    unsigned dict_size = 0;
    REQUIRE(beam_zlib_deflate_get_dictionary(&stream, copied, &dict_size) == BEAM_ZLIB_OK && dict_size == sizeof(dictionary));
    REQUIRE(beam_zlib_deflate_tune(&stream, 4, 4, 8, 16) == BEAM_ZLIB_OK);
    REQUIRE(beam_zlib_deflate_copy(&clone, &stream) == BEAM_ZLIB_OK);
    stream.next_in=input; stream.avail_in=sizeof(input); stream.next_out=compressed; stream.avail_out=sizeof(compressed);
    REQUIRE(beam_zlib_deflate_params(&stream, 1, BEAM_ZLIB_FILTERED) == BEAM_ZLIB_OK);
    REQUIRE(beam_zlib_deflate(&stream, BEAM_ZLIB_FINISH) == BEAM_ZLIB_STREAM_END);
    compressed_size=stream.total_out;
    unsigned pending; int bits;
    REQUIRE(beam_zlib_deflate_pending(&stream,&pending,&bits)==BEAM_ZLIB_OK);
    REQUIRE(beam_zlib_deflate_used(&stream,&bits)==BEAM_ZLIB_OK);
    REQUIRE(beam_zlib_deflate_end(&stream)==BEAM_ZLIB_OK);
    REQUIRE(beam_zlib_deflate_reset(&clone)==BEAM_ZLIB_OK);
    REQUIRE(beam_zlib_deflate_end(&clone)==BEAM_ZLIB_OK && !a.live);
    REQUIRE(beam_zlib_inflate_init(&inflater)==BEAM_ZLIB_OK);
    inflater.next_in=compressed; inflater.avail_in=(unsigned)compressed_size; inflater.next_out=output; inflater.avail_out=sizeof(output);
    REQUIRE(beam_zlib_inflate(&inflater,BEAM_ZLIB_NO_FLUSH)==BEAM_ZLIB_NEED_DICT);
    REQUIRE(beam_zlib_inflate_set_dictionary(&inflater,dictionary,sizeof(dictionary))==BEAM_ZLIB_OK);
    REQUIRE(beam_zlib_inflate(&inflater,BEAM_ZLIB_FINISH)==BEAM_ZLIB_STREAM_END);
    REQUIRE(inflater.total_out==sizeof(input) && !memcmp(output,input,sizeof(input)));
    REQUIRE(beam_zlib_inflate_get_dictionary(&inflater,copied,&dict_size)==BEAM_ZLIB_OK && dict_size);
    REQUIRE(beam_zlib_inflate_copy(&clone,&inflater)==BEAM_ZLIB_OK);
    REQUIRE(beam_zlib_inflate_reset2(&clone,15)==BEAM_ZLIB_OK);
    REQUIRE(beam_zlib_inflate_prime(&clone,0,0)==BEAM_ZLIB_OK);
    REQUIRE(beam_zlib_inflate_mark(&clone)==-65536L);
    REQUIRE(beam_zlib_inflate_end(&clone)==BEAM_ZLIB_OK);
    REQUIRE(beam_zlib_inflate_end(&inflater)==BEAM_ZLIB_OK);

    beam_zlib_gzip_header header = {0}, received = {0};
    unsigned char name[]="memory", read_name[32]={0};
    memset(&stream,0,sizeof(stream)); memset(&inflater,0,sizeof(inflater));
    header.name=name; header.time=123;
    received.name=read_name; received.name_max=sizeof(read_name);
    REQUIRE(beam_zlib_deflate_init2(&stream,6,BEAM_ZLIB_DEFLATED,31,8,BEAM_ZLIB_DEFAULT_STRATEGY)==BEAM_ZLIB_OK);
    REQUIRE(beam_zlib_deflate_set_header(&stream,&header)==BEAM_ZLIB_OK);
    stream.next_in=input; stream.avail_in=sizeof(input); stream.next_out=compressed; stream.avail_out=sizeof(compressed);
    REQUIRE(beam_zlib_deflate(&stream,BEAM_ZLIB_FINISH)==BEAM_ZLIB_STREAM_END);
    compressed_size=stream.total_out;
    REQUIRE(beam_zlib_deflate_end(&stream)==BEAM_ZLIB_OK);
    REQUIRE(beam_zlib_inflate_init2(&inflater,31)==BEAM_ZLIB_OK);
    REQUIRE(beam_zlib_inflate_get_header(&inflater,&received)==BEAM_ZLIB_OK);
    inflater.next_in=compressed; inflater.avail_in=(unsigned)compressed_size; inflater.next_out=output; inflater.avail_out=sizeof(output);
    REQUIRE(beam_zlib_inflate(&inflater,BEAM_ZLIB_FINISH)==BEAM_ZLIB_STREAM_END);
    REQUIRE(received.done==1 && received.time==123 && !strcmp((char*)read_name,"memory"));
    REQUIRE(beam_zlib_inflate_end(&inflater)==BEAM_ZLIB_OK);
    return 0;
}
