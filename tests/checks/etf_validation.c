/* SPDX-License-Identifier: Apache-2.0 */
#include <beam/beam_etf.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define REQUIRE(expr) do { if (!(expr)) { \
    fprintf(stderr, "ETF line %d: %s\n", __LINE__, #expr); return 1; \
} } while (0)

int check_etf(void)
{
    BeamEtfBuffer b, appended;
    char raw[4096], text[2048], byte;
    int i, n, type, size, flag;
    long integer, len;
    unsigned long unsigned_integer;
    BeamEtfLongLong wide;
    BeamEtfULongLong unsigned_wide;
    double floating;
    const char *view;
    size_t bits;
    unsigned int offset;
    REQUIRE(beam_etf_x_new_with_version(&b) == 0);
    /* The pinned ei_encode_SUITE and ETF specification use these wire bytes. */
    REQUIRE(beam_etf_x_encode_tuple_header(&b, 2) == 0);
    REQUIRE(beam_etf_x_encode_atom(&b, "ok") == 0);
    REQUIRE(beam_etf_x_encode_long(&b, 42) == 0);
    const unsigned char golden[] = {131,104,2,119,2,'o','k',97,42};
    REQUIRE(b.index == sizeof golden && !memcmp(b.buff, golden, sizeof golden));
    i = 0; REQUIRE(beam_etf_decode_version(b.buff, &i, &n) == 0 && n == 131);
    REQUIRE(beam_etf_get_type(b.buff, &i, &type, &size) == 0 && size == 2 && i == 1);
    REQUIRE(beam_etf_skip_term(b.buff, &i) == 0 && i == b.index);

    const BeamEtfLongLong values[] = {LLONG_MIN, -2147483648LL, -1, 0, 255, 256, 2147483647LL, LLONG_MAX};
    for (size_t j = 0; j < sizeof values / sizeof *values; ++j) {
        i = n = 0;
        REQUIRE(beam_etf_encode_longlong(NULL, &n, values[j]) == 0);
        REQUIRE(beam_etf_encode_longlong(raw, &i, values[j]) == 0 && i == n);
        i = 0; REQUIRE(beam_etf_decode_longlong(raw, &i, &wide) == 0 && wide == values[j] && i == n);
    }
    i = 0; REQUIRE(beam_etf_encode_ulonglong(raw, &i, ULLONG_MAX) == 0);
    n = 0; REQUIRE(beam_etf_decode_ulonglong(raw, &n, &unsigned_wide) == 0 && unsigned_wide == ULLONG_MAX && n == i);
    n = 0; REQUIRE(beam_etf_decode_longlong(raw, &n, &wide) == -1 && n == 0);
    i = 0; REQUIRE(beam_etf_encode_long(raw, &i, LONG_MIN) == 0);
    n = 0; REQUIRE(beam_etf_decode_long(raw, &n, &integer) == 0 && integer == LONG_MIN && n == i);
    i = 0; REQUIRE(beam_etf_encode_ulong(raw, &i, ULONG_MAX) == 0);
    n = 0; REQUIRE(beam_etf_decode_ulong(raw, &n, &unsigned_integer) == 0 && unsigned_integer == ULONG_MAX && n == i);
    b.index = 0;
    REQUIRE(beam_etf_x_encode_long(&b, -2147483647L) == 0);
    REQUIRE(beam_etf_x_encode_ulong(&b, 4294967295UL) == 0);
    REQUIRE(beam_etf_x_encode_double(&b, 3.5) == 0);
    REQUIRE(beam_etf_x_encode_boolean(&b, 1) == 0);
    REQUIRE(beam_etf_x_encode_char(&b, (char)255) == 0);
    i = 0;
    REQUIRE(beam_etf_decode_long(b.buff, &i, &integer) == 0 && integer == -2147483647L);
    REQUIRE(beam_etf_decode_ulong(b.buff, &i, &unsigned_integer) == 0 && unsigned_integer == 4294967295UL);
    REQUIRE(beam_etf_decode_double(b.buff, &i, &floating) == 0 && floating == 3.5);
    REQUIRE(beam_etf_decode_boolean(b.buff, &i, &flag) == 0 && flag == 1);
    REQUIRE(beam_etf_decode_char(b.buff, &i, &byte) == 0 && (unsigned char)byte == 255 && i == b.index);

    i = 0; REQUIRE(beam_etf_encode_atom(raw, &i, "\xc3\xa9\xf0\x9f\x90\xa7") == 0);
    n = 0; REQUIRE(beam_etf_decode_atom(raw, &n, text, 6) == -1 && n == 0);
    REQUIRE(beam_etf_decode_atom(raw, &n, text, sizeof text) == 0 && !strcmp(text, "\xc3\xa9\xf0\x9f\x90\xa7"));
    const char latin1[] = {'d',0,1,(char)0xe9};
    n = 0; REQUIRE(beam_etf_decode_atom(latin1, &n, text, sizeof text) == 0 && !strcmp(text,"\xc3\xa9"));
    memset(text, 'a', 256); text[256] = 0;
    n = 0; REQUIRE(beam_etf_encode_atom(raw, &n, text) == -1 && n == 0);
    REQUIRE(beam_etf_encode_atom_len(raw, &n, text, 255) == 0);
    n = 0; REQUIRE(beam_etf_encode_atom(raw, &n, "\xc3x") == -1 && n == 0);

    b.index = 0;
    REQUIRE(beam_etf_x_encode_map_header(&b, 1) == 0);
    REQUIRE(beam_etf_x_encode_atom(&b, "list") == 0);
    REQUIRE(beam_etf_x_encode_list_header(&b, 2) == 0);
    REQUIRE(beam_etf_x_encode_string_len(&b, "a\0b", 3) == 0);
    REQUIRE(beam_etf_x_encode_binary(&b, "cd", 2) == 0);
    REQUIRE(beam_etf_x_encode_empty_list(&b) == 0);
    i = 0; REQUIRE(beam_etf_decode_map_header(b.buff, &i, &n) == 0 && n == 1);
    REQUIRE(beam_etf_skip_term(b.buff, &i) == 0);
    n = i; size = 0;
    REQUIRE(beam_etf_decode_iodata(b.buff, &n, &size, NULL) == 0 && size == 5 && n == b.index);
    REQUIRE(beam_etf_decode_iodata(b.buff, &i, &size, text) == 0 && !memcmp(text,"a\0bcd",5));
    /* Improper list tails remain distinct terms. */
    b.index = 0;
    REQUIRE(beam_etf_x_encode_list_header(&b, 1) == 0);
    REQUIRE(beam_etf_x_encode_long(&b, 7) == 0 && beam_etf_x_encode_atom(&b, "tail") == 0);
    i = 0; REQUIRE(beam_etf_decode_list_header(b.buff, &i, &n) == 0 && n == 1);
    REQUIRE(beam_etf_decode_long(b.buff, &i, &integer) == 0 && integer == 7);
    REQUIRE(beam_etf_decode_atom(b.buff, &i, text, sizeof text) == 0 && !strcmp(text,"tail") && i == b.index);
    i = 0; REQUIRE(beam_etf_skip_term(b.buff, &i) == 0 && i == b.index);

    b.index = 0;
    REQUIRE(beam_etf_x_encode_binary(&b,"copied",6) == 0);
    i = 0; REQUIRE(beam_etf_decode_binary(b.buff,&i,text,&len) == 0 && len == 6);
    b.buff[5] = 'C'; REQUIRE(text[0] == 'c');
    b.index = 0;
    REQUIRE(beam_etf_x_encode_bitstring(&b,"\xaa\xc0",0,10) == 0);
    i = 0; REQUIRE(beam_etf_decode_bitstring(b.buff,&i,&view,&offset,&bits) == 0 && bits == 10 && offset == 0);
    REQUIRE(view == b.buff+6 && (unsigned char)view[0] == 0xaa && i == b.index);

    BeamEtfPid pid = {"node@host", 123, 456, 0xabcdef01}, decoded_pid;
    BeamEtfPort port = {"node@host", 0x123456789abcdef0ULL, 1234}, decoded_port;
    BeamEtfRef ref = {"node@host", 5, {1,2,3,4,5}, 1234}, decoded_ref;
    b.index = 0;
    REQUIRE(beam_etf_x_encode_pid(&b,&pid) == 0 && beam_etf_x_encode_port(&b,&port) == 0 && beam_etf_x_encode_ref(&b,&ref) == 0);
    i = 0;
    REQUIRE(beam_etf_decode_pid(b.buff,&i,&decoded_pid) == 0 && beam_etf_cmp_pids(&pid,&decoded_pid) == 0);
    REQUIRE(beam_etf_decode_port(b.buff,&i,&decoded_port) == 0 && beam_etf_cmp_ports(&port,&decoded_port) == 0);
    REQUIRE(beam_etf_decode_ref(b.buff,&i,&decoded_ref) == 0 && beam_etf_cmp_refs(&ref,&decoded_ref) == 0 && i == b.index);
    decoded_ref.n[4]++; REQUIRE(beam_etf_cmp_refs(&ref,&decoded_ref) < 0);

    BeamEtfFun fun = {0}, decoded = {0};
    fun.type = BEAM_ETF_FUN_EXPORT; fun.arity = 1;
    strcpy(fun.module,"module"); fun.u.exprt.func = "function";
    b.index = 0; REQUIRE(beam_etf_x_encode_fun(&b,&fun) == 0);
    i = 0; REQUIRE(beam_etf_decode_fun(b.buff,&i,&decoded) == 0);
    REQUIRE(!decoded.u.exprt.func_allocated && decoded.u.exprt.func == decoded.module+7 && !strcmp(decoded.u.exprt.func,"function"));
    beam_etf_free_fun(&decoded);
    /* Two maximum-width UTF-8 atoms require a separately allocated function. */
    for (i=0; i<255; ++i) memcpy(text+4*i,"\xf0\x9f\x90\xa7",4);
    text[1020] = 0; memcpy(fun.module,text,1021); fun.u.exprt.func = text;
    b.index = 0; REQUIRE(beam_etf_x_encode_fun(&b,&fun) == 0);
    i = 0; REQUIRE(beam_etf_decode_fun(b.buff,&i,&decoded) == 0 && decoded.u.exprt.func_allocated && !strcmp(decoded.u.exprt.func,text));
    beam_etf_free_fun(&decoded);
    memset(&fun,0,sizeof fun); fun.type = BEAM_ETF_FUN_CLOSURE; fun.arity = 0;
    strcpy(fun.module,"module"); fun.u.closure.pid = pid;
    fun.u.closure.n_free_vars = 1; fun.u.closure.free_vars = "a\x2a"; fun.u.closure.free_var_len = 2;
    b.index = 0; REQUIRE(beam_etf_x_encode_fun(&b,&fun) == 0);
    i = 0; REQUIRE(beam_etf_decode_fun(b.buff,&i,&decoded) == 0 && decoded.u.closure.free_var_len == 2);
    REQUIRE(decoded.u.closure.free_vars != b.buff && !memcmp(decoded.u.closure.free_vars,"a\x2a",2));
    beam_etf_free_fun(&decoded);

    REQUIRE(beam_etf_x_new(&appended) == 0 && beam_etf_x_append(&appended,&b) == 0);
    REQUIRE(appended.index == b.index && !memcmp(appended.buff,b.buff,b.index));
    memset(raw,42,sizeof raw);
    REQUIRE(beam_etf_x_append_buf(&appended,raw,sizeof raw) == 0 && appended.buffsz >= appended.index);
    REQUIRE(beam_etf_x_free(&appended) == 0 && appended.buff == NULL);
    REQUIRE(beam_etf_x_free(&b) == 0);
    return 0;
}
