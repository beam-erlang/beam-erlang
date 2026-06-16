/* SPDX-License-Identifier: Apache-2.0 */
#include "support.h"
#include "bytecode_input.h"

void library_mode(const char *root, const char *fixture)
{
    beam_system *system = NULL;
    beam_vm *vm = NULL;
    beam_vm_options vm_options;
    beam_error error = {0};
    beam_result result = {0};
    char path[4096];
    unsigned char *program;
    size_t program_size;
    const unsigned char dummy_text[] = "_";
    const unsigned char *dummy_items[] = {dummy_text};
    size_t dummy_sizes[] = {1};
    beam_bytes arguments = list_of_binaries(dummy_items, dummy_sizes, 1);
    check(beam_init(&system, &error) == BEAM_OK, "init");
    beam_vm_options_init(&vm_options);

    bytecode_input base = {0};
    check(bytecode_input_read(&base, root, INPUT_BASE, &error) == BEAM_OK, error.message);
    beam_composition_options preparation = {.struct_size = sizeof(preparation), .modules = base.modules, .module_count = 7};
    beam_composition *composition = NULL;
    check(beam_composition_prepare(&preparation, &composition, &error) == BEAM_OK, error.message);
    bytecode_input_release(&base);
    vm_options.composition = composition;
    check(beam_vm_init(system, &vm_options, &vm, &error) == BEAM_OK, error.message);
    beam_composition_release(&composition);
    snprintf(path, sizeof(path), "%s/returning_programs.beam", fixture);
    program = read_file(path, &program_size);
    check(program != NULL, "program");
    check(beam_vm_load(vm, (beam_bytes){program, program_size}) == BEAM_OK, "load program");
    free(program);
    check(beam_vm_start(vm, &error) == BEAM_OK, "start");
    check(beam_vm_call(vm, "returning_programs", "library", arguments, &result) == BEAM_OK, "call");
    check(result.kind == BEAM_RETURN, "library result kind");
    {
        const unsigned char *text = NULL;
        size_t text_size = 0;
        etf_reader cursor = read_term(result.value, 7);
        int huge_reds = 0, map_reds = 0;
        check(etf_binary(&cursor, &text, &text_size), "format huge text");
        check(text_size == 8 && memcmp(text, "99006...", 8) == 0, "format huge value");
        check(etf_integer(&cursor, &huge_reds) && huge_reds > 0 && huge_reds < 500000,
              "format huge reductions");
        check(etf_integer(&cursor, &map_reds) && map_reds > 0 && map_reds < 8000,
              "format map reductions");
        printf("format-huge %.*s reductions %d\n", (int)text_size, text, huge_reds);
        printf("format-map reductions %d\n", map_reds);
        check(etf_binary(&cursor, &text, &text_size) && text_size <= 8,
              "format float bound");
        printf("format-float %.*s\n", (int)text_size, text);
        check(etf_binary(&cursor, &text, &text_size) && text_size <= 8 &&
              text_size >= 1 && text[0] == '[',
              "format nested float");
        printf("format-nested %.*s\n", (int)text_size, text);
        check(etf_binary(&cursor, &text, &text_size) &&
              text_size == strlen("#{1 => 2}|[1, 2]|{1, 2}|#{}") &&
              memcmp(text, "#{1 => 2}|[1, 2]|{1, 2}|#{}", text_size) == 0,
              "format openers");
        printf("format-openers %.*s\n", (int)text_size, text);
        check(etf_binary(&cursor, &text, &text_size) && text_size == 10 &&
              memcmp(text, "LIBRARY_OK", 10) == 0 && etf_done(&cursor), "library marker");
    }
    beam_result_release(&result);
    check(beam_vm_call(vm, "returning_programs", "preloaded", arguments, &result) == BEAM_OK, "preloaded call");
    check(result.kind == BEAM_RETURN, "preloaded kind");
    beam_result_release(&result);
    check(beam_vm_terminate(&vm, NULL) == BEAM_OK, "vm terminate");
    check(beam_terminate(&system) == BEAM_OK, "system terminate");
    free((void *)arguments.data);
    (void)root;
    printf("library ok\n");
}
