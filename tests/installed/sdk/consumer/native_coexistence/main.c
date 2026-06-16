/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#define PCRE2_CODE_UNIT_WIDTH 8
#include <beam.h>
#include <beam_native.h>
#include <pcre2.h>
#include <zlib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static int regex_check(void)
{
    int error;
    PCRE2_SIZE offset;
    pcre2_code *code = pcre2_compile((PCRE2_SPTR)"^(hello) ([0-9]+)$",
        PCRE2_ZERO_TERMINATED, 0, &error, &offset, NULL);
    if (!code)
        return 1;
    pcre2_match_data *data = pcre2_match_data_create_from_pattern(code, NULL);
    if (!data) {
        pcre2_code_free(code);
        return 1;
    }
    int count = pcre2_match(code, (PCRE2_SPTR)"hello 42", 8, 0, 0, data, NULL);
    PCRE2_SIZE *captures = pcre2_get_ovector_pointer(data);
    int failed = count != 3 || captures[0] != 0 || captures[1] != 8 ||
        captures[2] != 0 || captures[3] != 5 || captures[4] != 6 || captures[5] != 8;
    failed |= pcre2_match(code, (PCRE2_SPTR)"goodbye", 7, 0, 0, data, NULL)
        != PCRE2_ERROR_NOMATCH;
    pcre2_match_data_free(data);
    pcre2_code_free(code);
    return failed;
}

static int compression_check(void)
{
    const unsigned char input[] = "host compression remains independent of VM lifetimes";
    unsigned char compressed[128], output[sizeof(input)];
    uLongf compressed_size = sizeof(compressed), output_size = sizeof(output);
    if (compress(compressed, &compressed_size, input, sizeof(input)) != Z_OK ||
        uncompress(output, &output_size, compressed, compressed_size) != Z_OK)
        return 1;
    return output_size != sizeof(input) || memcmp(output, input, sizeof(input));
}

static beam_native_trace_backend selected_backend;

static int native_check(void)
{
    return regex_check() || compression_check() ||
        beam_native_trace_backend_get() != selected_backend;
}

int main(void)
{
    beam_system *system = NULL;
    beam_vm *vm = NULL;
    beam_error error;
    beam_vm_options options;
    Dl_info provider;
    int failed = 1;
    selected_backend = beam_native_trace_backend_get();
    if (selected_backend < BEAM_NATIVE_TRACE_NONE ||
        selected_backend > BEAM_NATIVE_TRACE_LTTNG)
        return 1;
    /* Diagnose the unsafe binding before invoking an ABI-incompatible function. */
    if (!dladdr(dlsym(RTLD_DEFAULT, "pcre2_match_8"), &provider) ||
        !provider.dli_fname || !strstr(provider.dli_fname, "libpcre2-8.so")) {
        fputs("Host PCRE resolved outside the system PCRE library\n", stderr);
        return 1;
    }
    if (!dladdr(dlsym(RTLD_DEFAULT, "inflate"), &provider) ||
        !provider.dli_fname || !strstr(provider.dli_fname, "libz.so")) {
        fputs("Host compression resolved outside the system zlib library\n", stderr);
        return 1;
    }
    if (native_check() || beam_init(&system, &error) != BEAM_OK)
        goto cleanup;
    beam_vm_options_init(&options);
    for (int cycle = 0; cycle < 3; ++cycle) {
        if (beam_vm_init(system, &options, &vm, &error) != BEAM_OK ||
            native_check() || beam_vm_start(vm, &error) != BEAM_OK ||
            native_check() || beam_vm_terminate(&vm, NULL) != BEAM_OK ||
            native_check())
            goto cleanup;
    }
    failed = 0;
cleanup:
    if (beam_vm_terminate(&vm, NULL) != BEAM_OK ||
        beam_terminate(&system) != BEAM_OK || native_check())
        failed = 1;
    if (failed)
        fputs("Native coexistence or VM lifecycle failed\n", stderr);
    else
        puts("Native coexistence OK");
    return failed;
}
