/* SPDX-License-Identifier: Apache-2.0 */
#include <beam/sdk/etf.h>
#include <limits.h>
int codec_probe(void)
{
    BeamEtfBuffer buffer;
    long value;
    unsigned long unsigned_value;
    int index = 0;
    if (beam_etf_embed_x_new(&buffer) ||
        beam_etf_embed_x_encode_long(&buffer, 42) ||
        beam_etf_embed_decode_long(buffer.buff, &index, &value) ||
        value != 42 || index != buffer.index) return 1;
    buffer.index = index = 0;
    if (beam_etf_embed_x_encode_long(&buffer, LONG_MIN) ||
        beam_etf_embed_decode_long(buffer.buff, &index, &value) ||
        value != LONG_MIN || index != buffer.index) return 1;
    buffer.index = index = 0;
    if (beam_etf_embed_x_encode_ulong(&buffer, ULONG_MAX) ||
        beam_etf_embed_decode_ulong(buffer.buff, &index, &unsigned_value) ||
        unsigned_value != ULONG_MAX || index != buffer.index) return 1;
    return beam_etf_embed_x_free(&buffer);
}
