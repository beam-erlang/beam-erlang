/* SPDX-License-Identifier: Apache-2.0 */
#include "etf.h"
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "ETF check line %d: %s\n", __LINE__, #expr); return 1; } } while (0)

static int counter(const unsigned char *data, size_t size)
{
    etf_reader reader;
    int a, b, c;
    return etf_start(&reader, data, size) && etf_tuple(&reader, 3) &&
        etf_integer(&reader, &a) && etf_integer(&reader, &b) && etf_integer(&reader, &c) &&
        a == 8 && b == 7 && c == 1 && etf_done(&reader);
}

int main(void)
{
    const unsigned char tuple[] = {131,104,3,97,8,97,7,97,1};
    const unsigned char list[] = {131,108,0,0,0,3,97,8,97,7,97,1,106};
    const unsigned char decoy[] = {131,109,0,0,0,6,97,8,97,7,97,1};
    const unsigned char wrong_arity[] = {131,104,4,97,8,97,7,97,1};
    const unsigned char trailing[] = {131,104,3,97,8,97,7,97,1,106};
    const unsigned char large_tuple[] = {131,105,0,0,0,3,97,8,97,7,97,1};
    etf_reader reader;
    const unsigned char *bytes;
    size_t length;
    int value;
    CHECK(counter(tuple, sizeof tuple));
    CHECK(counter(large_tuple, sizeof large_tuple));
    for (size_t size = 0; size < sizeof tuple; ++size) CHECK(!counter(tuple, size));
    CHECK(!counter(list, sizeof list));
    CHECK(!counter(decoy, sizeof decoy));
    CHECK(!counter(wrong_arity, sizeof wrong_arity));
    CHECK(!counter(trailing, sizeof trailing));
    const unsigned char wrong_version[] = {130,104,3,97,8,97,7,97,1};
    CHECK(!counter(wrong_version, sizeof wrong_version));
    const unsigned char unknown[] = {131,70,0,0,0,0,0,0,0,0};
    CHECK(etf_start(&reader, unknown, sizeof unknown) && !etf_integer(&reader, &value));
    CHECK(!etf_done(&reader));
    CHECK(!etf_start(&reader, NULL, 0));
    CHECK(!etf_done(&reader));
    CHECK(etf_start(&reader, list, sizeof list) && etf_list(&reader, 3));
    for (int i = 0; i < 3; ++i) CHECK(etf_integer(&reader, &value));
    CHECK(etf_nil(&reader) && etf_done(&reader));
    CHECK(etf_start(&reader, list, sizeof list) && !etf_list(&reader, 2));
    const unsigned char oversized_list[] = {131,108,255,255,255,255,106};
    CHECK(etf_start(&reader, oversized_list, sizeof oversized_list) && !etf_list(&reader, 3));
    const unsigned char oversized_tuple[] = {131,105,255,255,255,255,97,8,97,7,97,1};
    CHECK(!counter(oversized_tuple, sizeof oversized_tuple));
    unsigned char improper[sizeof list];
    memcpy(improper, list, sizeof list); improper[sizeof improper - 1] = 97;
    CHECK(etf_start(&reader, improper, sizeof improper) && etf_list(&reader, 3));
    for (int i = 0; i < 3; ++i) CHECK(etf_integer(&reader, &value));
    CHECK(!etf_nil(&reader) && !etf_done(&reader));
    const unsigned char binary[] = {131,109,0,0,0,2,'o','k'};
    CHECK(etf_start(&reader, binary, sizeof binary) && etf_binary(&reader, &bytes, &length));
    CHECK(length == 2 && memcmp(bytes, "ok", 2) == 0 && etf_done(&reader));
    for (size_t size = 1; size < sizeof binary; ++size) {
        CHECK(etf_start(&reader, binary, size));
        CHECK(!etf_binary(&reader, &bytes, &length) && !etf_done(&reader));
    }
    const unsigned char oversized[] = {131,109,255,255,255,255,'o','k'};
    CHECK(etf_start(&reader, oversized, sizeof oversized) && !etf_binary(&reader, &bytes, &length));
    const unsigned char empty_binary[] = {131,109,0,0,0,0};
    CHECK(etf_start(&reader, empty_binary, sizeof empty_binary) && etf_binary(&reader, &bytes, &length));
    CHECK(length == 0 && etf_done(&reader));
    const unsigned char text_decoy[] = {131,104,1,109,0,0,0,2,'o','k'};
    CHECK(etf_start(&reader, text_decoy, sizeof text_decoy) && !etf_binary(&reader, &bytes, &length));
    const unsigned char integers[][6] = {{131,98,128,0,0,0}, {131,98,127,255,255,255}, {131,98,255,255,255,255}};
    const int expected[] = {INT32_MIN, INT32_MAX, -1};
    for (size_t i = 0; i < 3; ++i) {
        CHECK(etf_start(&reader, integers[i], 6) && etf_integer(&reader, &value));
        CHECK(value == expected[i] && etf_done(&reader));
        for (size_t size = 1; size < sizeof integers[i]; ++size) {
            CHECK(etf_start(&reader, integers[i], size) && !etf_integer(&reader, &value));
        }
    }
    const unsigned char small_integers[][3] = {{131,97,0}, {131,97,255}};
    for (size_t i = 0; i < 2; ++i) {
        CHECK(etf_start(&reader, small_integers[i], 3) && etf_integer(&reader, &value));
        CHECK(value == (int)i * 255 && etf_done(&reader));
    }
    const unsigned char integer_256[] = {131,98,0,0,1,0};
    CHECK(etf_start(&reader, integer_256, sizeof integer_256) && etf_integer(&reader, &value));
    CHECK(value == 256 && etf_done(&reader));
    const unsigned char atom[] = {131,119,2,'o','k'};
    const unsigned char large_atom[] = {131,118,0,2,'o','k'};
    CHECK(etf_start(&reader, atom, sizeof atom) && etf_atom(&reader, "ok") && etf_done(&reader));
    CHECK(etf_start(&reader, large_atom, sizeof large_atom) && etf_atom(&reader, "ok") && etf_done(&reader));
    CHECK(etf_start(&reader, atom, sizeof atom) && !etf_atom(&reader, "other"));
    CHECK(etf_start(&reader, binary, sizeof binary) && !etf_atom(&reader, "ok"));
    CHECK(etf_start(&reader, atom, sizeof atom - 1) && !etf_atom(&reader, "ok"));
    const unsigned char unicode_atom[] = {131,119,2,0xce,0xbb};
    CHECK(etf_start(&reader, unicode_atom, sizeof unicode_atom) && etf_atom(&reader, "\xce\xbb"));
    CHECK(etf_done(&reader));
    const unsigned char empty_list[] = {131,106};
    CHECK(etf_start(&reader, empty_list, sizeof empty_list) && etf_nil(&reader) && etf_done(&reader));
    const unsigned char trace[] = {
        131,108,0,0,0,1,104,4,119,1,'m',119,1,'f',97,0,
        108,0,0,0,2,104,2,119,4,'f','i','l','e',107,0,1,'x',
        104,2,119,4,'l','i','n','e',97,1,106,106
    };
    CHECK(etf_start(&reader, trace, sizeof trace) && etf_stacktrace(&reader) && etf_done(&reader));
    for (size_t size = 1; size < sizeof trace; ++size) {
        CHECK(etf_start(&reader, trace, size));
        CHECK(!etf_stacktrace(&reader) && !etf_done(&reader));
    }
    unsigned char bad_trace[sizeof trace + 1];
    memcpy(bad_trace, trace, sizeof trace);
    bad_trace[sizeof trace] = 106;
    CHECK(etf_start(&reader, bad_trace, sizeof bad_trace) && etf_stacktrace(&reader));
    CHECK(!etf_done(&reader));
    bad_trace[sizeof trace - 1] = 97;
    CHECK(etf_start(&reader, bad_trace, sizeof trace) && !etf_stacktrace(&reader));
    memcpy(bad_trace, trace, sizeof trace); bad_trace[7] = 3;
    CHECK(etf_start(&reader, bad_trace, sizeof trace) && !etf_stacktrace(&reader));
    memcpy(bad_trace, trace, sizeof trace); bad_trace[15] = 255;
    CHECK(etf_start(&reader, bad_trace, sizeof trace) && etf_stacktrace(&reader) && etf_done(&reader));
    memcpy(bad_trace, trace, sizeof trace); bad_trace[5] = 65;
    CHECK(etf_start(&reader, bad_trace, sizeof trace) && !etf_stacktrace(&reader));
    memcpy(bad_trace, trace, sizeof trace); bad_trace[25] = 'x';
    CHECK(etf_start(&reader, bad_trace, sizeof trace) && !etf_stacktrace(&reader));
    CHECK(etf_start(&reader, empty_list, sizeof empty_list) && etf_stacktrace(&reader) && etf_done(&reader));
    return 0;
}
