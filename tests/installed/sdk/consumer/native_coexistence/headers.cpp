/* SPDX-License-Identifier: Apache-2.0 */
#include <zlib.h>
#include <beam/zlib.h>
#include <type_traits>
static_assert(!std::is_same<z_stream, beam_zlib_stream>::value, "independent system stream type");
extern "C" int beam_test_codec_cpp(void)
{
    beam_zlib_stream beam = {};
    z_stream system = {};
    if (beam_zlib_inflate_init(&beam) != BEAM_ZLIB_OK) return 1;
    if (inflateInit(&system) != Z_OK) { beam_zlib_inflate_end(&beam); return 1; }
    return beam_zlib_inflate_end(&beam) != BEAM_ZLIB_OK || inflateEnd(&system) != Z_OK;
}
