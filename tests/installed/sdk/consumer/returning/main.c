/* SPDX-License-Identifier: Apache-2.0 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <beam/boot.h>
#include "bytecode_input.h"
#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr, "returning SDK check failed at %d: %s (%s)\n", \
            __LINE__, #test, error.message); return 1; \
} } while (0)

int main(int argc, char **argv)
{
    beam_error error = {0};
    beam_system *system = NULL;
    beam_return_boot *boot = NULL, *activated_boot = NULL;
    beam_return_options options = {0};
    beam_return_options raw = {0};
    beam_return_module modules[14] = {{0}};
    beam_return_module *module = &modules[13];
    bytecode_input base = {0};
    beam_outcome first = {0}, second = {0}, activated = {0};
    unsigned char arguments[] = {131,108,0,0,0,1,97,42,106};
    char origin[] = "memory:sdk-origin";
    const unsigned char first_term[] = {131,97,42}, second_term[] = {131,97,43};
    unsigned char *bytes;
    long size;
    FILE *input;
    Dl_info info;
    char expected[PATH_MAX], loaded[PATH_MAX], canonical[PATH_MAX];
    CHECK(argc == 3);
    CHECK(strncmp(beam_version(), "BEAM/Erlang " EXPECTED_BEAM_VERSION " ",
                  sizeof("BEAM/Erlang " EXPECTED_BEAM_VERSION " ") - 1) == 0);
    CHECK(snprintf(expected, sizeof expected, "%s/" BEAM_TEST_RUNTIME_RELATIVE, argv[1]) < (int)sizeof expected);
    CHECK(dladdr((void *)beam_version, &info) && realpath(info.dli_fname, loaded) &&
          realpath(expected, canonical) && !strcmp(loaded, canonical));
    input = fopen(argv[2], "rb");
    CHECK(input && !fseek(input, 0, SEEK_END));
    size = ftell(input);
    CHECK(size > 0 && !fseek(input, 0, SEEK_SET));
    bytes = (unsigned char *)malloc((size_t)size);
    CHECK(bytes && fread(bytes, 1, (size_t)size, input) == (size_t)size);
    CHECK(!fclose(input));
    module->name = "sdk_returning";
    module->bytes.data = bytes;
    module->bytes.size = (size_t)size;
    module->origin = origin;
    options.struct_size = sizeof options;
    options.entry_module = "sdk_returning";
    options.entry_function = "main";
    options.arguments.data = arguments;
    options.arguments.size = sizeof arguments;
    CHECK(bytecode_input_read(&base, argv[1], INPUT_RETURNING, &error) == BEAM_OK);
    for (size_t i = 0; i < base.count; ++i) {
        modules[i].name = base.modules[i].name; modules[i].bytes = base.modules[i].bytes;
        modules[i].origin = base.modules[i].origin;
    }
    options.modules = modules;
    options.module_count = 14;
    raw.struct_size = sizeof raw;
    raw.entry_module = options.entry_module;
    raw.entry_function = options.entry_function;
    raw.arguments = options.arguments;
    raw.modules = modules;
    raw.module_count = 14;
    CHECK(beam_return_prepare(&raw, &boot, &error) == BEAM_OK);
    CHECK(beam_return_discard(&boot) == BEAM_OK && boot == NULL);
    CHECK(beam_init(&system, &error) == BEAM_OK);
    CHECK(beam_return_prepare(&options, &boot, &error) == BEAM_OK);
    arguments[7] = 43;
    memset(origin, 'x', sizeof origin - 1);
    first.struct_size = sizeof first;
    CHECK(beam_return_run(system, &boot, &first, &error) == BEAM_OK && boot == NULL);
    CHECK(first.kind == BEAM_OUTCOME_RETURN && first.term.size == sizeof first_term &&
          !memcmp(first.term.data, first_term, sizeof first_term));
    module->origin = NULL;
    CHECK(beam_return_prepare(&options, &boot, &error) == BEAM_OK);
    arguments[7] = 44;
    module->deferred = 1;
    CHECK(beam_return_prepare(&options, &activated_boot, &error) == BEAM_OK);
    bytecode_input_release(&base);
    memset(bytes, 0, (size_t)size);
    free(bytes);
    second.struct_size = sizeof second;
    CHECK(beam_return_run(system, &boot, &second, &error) == BEAM_OK && boot == NULL);
    CHECK(second.kind == BEAM_OUTCOME_RETURN && second.term.size == sizeof second_term &&
          !memcmp(second.term.data, second_term, sizeof second_term));
    activated.struct_size = sizeof activated;
    CHECK(beam_return_run(system, &activated_boot, &activated, &error) == BEAM_OK && activated_boot == NULL);
    CHECK(activated.kind == BEAM_OUTCOME_RETURN && activated.term.size == 3 && activated.term.data[2] == 44);
    beam_outcome_release(&activated);
    CHECK(beam_terminate(&system) == BEAM_OK && system == NULL);
    CHECK(first.term.size == sizeof first_term &&
          !memcmp(first.term.data, first_term, sizeof first_term));
    beam_outcome_release(&first);
    beam_outcome_release(&second);
    puts("Installed returning SDK");
    return 0;
}
