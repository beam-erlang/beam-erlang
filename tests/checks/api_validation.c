/* SPDX-License-Identifier: Apache-2.0 */
#include <beam/beam.h>
#include <beam/process.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(call, expected) do { \
    beam_status actual = (call); \
    if (actual != (expected)) { \
        fprintf(stderr, "%d: %s returned %s: %s (%d)\n", __LINE__, #call, \
                beam_status_message(actual), error.message, error.native_code); \
        return 1; \
    } \
} while (0)

static int check_library_path(void)
{
    beam_error error = {0};
    size_t required = 0;
    char small[2] = {'x', 'y'};
    char *library;
    CHECK(beam_library_path(NULL, 0, &required, &error), BEAM_OK);
    if (required < 2) return 1;
    CHECK(beam_library_path(small, sizeof small, &required, &error), BEAM_INVALID_ARGUMENT);
    if (small[0] != 'x' || small[1] != 'y') return 1;
    library = malloc(required);
    if (!library) return 1;
    CHECK(beam_library_path(library, required, &required, &error), BEAM_OK);
#ifdef _WIN32
    if (!((library[0] == '\\' && library[1] == '\\') ||
          (library[0] && library[1] == ':' && (library[2] == '/' || library[2] == '\\')))) return 1;
#else
    if (library[0] != '/') return 1;
#endif
    if (strlen(library) + 1 != required) return 1;
    free(library);
    CHECK(beam_library_path(NULL, 1, &required, &error), BEAM_INVALID_ARGUMENT);
    CHECK(beam_library_path(NULL, 0, NULL, &error), BEAM_INVALID_ARGUMENT);
    return 0;
}

static int check_management_unstarted(beam_vm *vm)
{
    beam_error error={0};
    beam_scheduler_snapshot schedulers={0};
    beam_process_list list={0};
    beam_process_query query;
    beam_process_snapshot snapshot={0};
    beam_process_spawn_options options;
    BeamPid pid;
    beam_enif_set_pid_undefined(&pid);
    beam_process_query_init(NULL);
    beam_process_spawn_options_init(NULL);
    beam_process_query_init(&query);
    beam_process_spawn_options_init(&options);
    if(query.struct_size!=sizeof query || query.fields ||
       options.struct_size!=sizeof options ||
       !beam_enif_is_pid_undefined(&options.group_leader)) return 1;
    beam_status expected=vm?BEAM_INVALID_STATE:BEAM_INVALID_ARGUMENT;
    CHECK(beam_scheduler_inspect_all(vm,&schedulers),expected);
    CHECK(beam_process_list_pids(vm,&list),expected);
    CHECK(beam_process_inspect(vm,&pid,&query,&snapshot),expected);
    CHECK(beam_process_spawn(vm,"missing","entry",NULL,0,&options,&pid,&error),expected);
    if(schedulers.items || list.pids || snapshot.fields || !beam_enif_is_pid_undefined(&pid)) return 1;
    beam_scheduler_snapshot_release(NULL);
    beam_scheduler_snapshot_release(&schedulers);
    beam_process_list_release(NULL);
    beam_process_list_release(&list);
    beam_process_snapshot_release(NULL);
    beam_process_snapshot_release(&snapshot);
    return 0;
}

static int check_embedded_dependencies(void)
{
    beam_error error = {0};
    beam_composition *prepared = NULL;
    beam_embedded_dependency entries[2] = {
        {sizeof(beam_embedded_dependency), "fixture_included", BEAM_EMBEDDED_INCLUDED},
        {sizeof(beam_embedded_dependency), "fixture_excluded", BEAM_EMBEDDED_EXCLUDED}};
    beam_composition_options options = {.struct_size = sizeof(options),
        .embedded_dependency_count = 2, .embedded_dependencies = entries};
    CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_OK);
    beam_composition_release(&prepared);
    /* PCRE belongs to an ordinary provider and is not a VM-reserved name. */
    entries[0].name = "pcre2";
    CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_OK);
    beam_composition_release(&prepared);
    entries[0].name = "fixture_included";
    entries[1].name = entries[0].name;
    CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_INVALID_ARGUMENT);
    if (prepared) return 1;
    entries[1].name = "fixture_excluded";
    const char *invalid[] = {NULL, "", "asmjit", "STL", "ryu", "zlib", "\xc0\xaf", "\xed\xa0\x80"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
        entries[0].name = invalid[i];
        CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_INVALID_ARGUMENT);
        if (prepared) return 1;
    }
    char name[1025];
    for (size_t i = 0; i < 256; ++i) memcpy(name + i * 4, "\xf0\x9f\x90\xa7", 4);
    name[1020] = 0; entries[0].name = name;
    CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_OK);
    beam_composition_release(&prepared);
    memcpy(name + 1020, "\xf0\x9f\x90\xa7", 4); name[1024] = 0;
    CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_INVALID_ARGUMENT);
    entries[0].name = "fixture_included";
    entries[0].state = (beam_embedded_dependency_state)2;
    CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_INVALID_ARGUMENT);
    entries[0].state = BEAM_EMBEDDED_INCLUDED; --entries[0].struct_size;
    CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_INVALID_ARGUMENT);
    ++entries[0].struct_size; options.embedded_dependencies = NULL;
    CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_INVALID_ARGUMENT);
    options.embedded_dependencies = entries; options.embedded_dependency_count = 1025;
    CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_INVALID_ARGUMENT);
    return prepared != NULL;
}

static int check_builtin_bindings(void)
{
    beam_error error = {0};
    beam_composition *prepared = NULL;
    beam_builtin_binding entries[2] = {
        {sizeof(beam_builtin_binding), "bytes_split_2", "custom_bytes", "cut", 2},
        {sizeof(beam_builtin_binding), "bytes_find_all_2", "binary", "matches", 2}};
    beam_composition_options options = {.struct_size = sizeof options,
        .builtin_binding_count = 2, .builtin_bindings = entries};
    CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_OK);
    beam_composition_release(&prepared);
    entries[1].implementation = entries[0].implementation;
    CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_INVALID_ARGUMENT);
    entries[1].implementation = "bytes_find_all_2";
    entries[1].module = entries[0].module;
    entries[1].function = entries[0].function;
    CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_INVALID_ARGUMENT);
    options.builtin_binding_count = 1;
    const char *invalid[] = {NULL, "", "\xc0\xaf", "\xed\xa0\x80"};
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; ++i) {
        entries[0].module = invalid[i];
        CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_INVALID_ARGUMENT);
        entries[0].module = "custom_bytes";
        entries[0].function = invalid[i];
        CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_INVALID_ARGUMENT);
        entries[0].function = "cut";
        entries[0].implementation = invalid[i];
        CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_INVALID_ARGUMENT);
        entries[0].implementation = "bytes_split_2";
    }
    const char *reserved[] = {"erlang", "erts_internal", "erts_code_purger",
        "erts_literal_area_collector", "beam_internal", "erts_dirty_process_signal_handler"};
    for (size_t i = 0; i < sizeof reserved / sizeof *reserved; ++i) {
        entries[0].module = reserved[i];
        CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_INVALID_ARGUMENT);
    }
    const char *ordinary[] = {"atomics", "counters", "persistent_term"};
    for (size_t i = 0; i < sizeof ordinary / sizeof *ordinary; ++i) {
        entries[0].module = ordinary[i];
        CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_OK);
        beam_composition_release(&prepared);
    }
    entries[0].module = "maps"; entries[0].function = "get";
    CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_OK);
    beam_composition_release(&prepared);
    entries[0].module = "ets"; entries[0].function = "delete";
    CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_OK);
    beam_composition_release(&prepared);
    entries[0].module = "custom_bytes"; entries[0].function = "cut";
    entries[0].arity = 3;
    CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_INVALID_ARGUMENT);
    entries[0].arity = 2; --entries[0].struct_size;
    CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_INVALID_ARGUMENT);
    ++entries[0].struct_size;
    char name[1025];
    for (size_t i = 0; i < 256; ++i) memcpy(name + 4*i, "\xf0\x9f\x90\xa7", 4);
    name[1020] = 0; entries[0].module = name;
    CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_OK);
    beam_composition_release(&prepared);
    memcpy(name + 1020, "\xf0\x9f\x90\xa7", 4); name[1024] = 0;
    CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_INVALID_ARGUMENT);
    entries[0].module = "custom_bytes"; options.builtin_binding_count = 1025;
    CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_INVALID_ARGUMENT);
    options.builtin_binding_count = 1; options.builtin_bindings = NULL;
    CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_INVALID_ARGUMENT);
    options.builtin_binding_count = 0;
    CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_OK);
    beam_composition_release(&prepared);
    options.builtin_bindings = entries;
    CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_OK);
    beam_composition_release(&prepared);
    options.builtin_binding_count = 1;
    CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_OK);
    beam_composition *unchanged = prepared;
    CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_INVALID_ARGUMENT);
    if (prepared != unchanged) return 1;
    beam_composition_release(&prepared);
    static const struct { const char *id; unsigned arity; } contracts[] = {
        {"list_member_2", 2},
        {"list_reverse_2", 2},
        {"list_keymember_3", 3},
        {"list_keysearch_3", 3},
        {"list_keyfind_3", 3},
        {"charlist_parse_float_prefix_1", 1},
        {"chardata_to_utf8_2", 2},
        {"chardata_to_codepoints_2", 2},
        {"bytes_is_7bit_1", 1},
        {"bytes_compile_pattern_1", 1},
        {"bytes_match_list_2", 2},
        {"bytes_match_list_3", 3},
        {"bytes_find_all_2", 2},
        {"bytes_matches_list_3", 3},
        {"bytes_common_prefix_size_1", 1},
        {"bytes_common_suffix_size_1", 1},
        {"bytes_first_1", 1},
        {"bytes_last_1", 1},
        {"bytes_at_2", 2},
        {"bytes_part_2", 2},
        {"bytes_part_3", 3},
        {"bytes_from_list_1", 1},
        {"bytes_copy_1", 1},
        {"bytes_copy_2", 2},
        {"bitstring_referenced_size_1", 1},
        {"bytes_encode_unsigned_1", 1},
        {"bytes_encode_unsigned_2", 2},
        {"bytes_decode_unsigned_1", 1},
        {"bytes_decode_unsigned_2", 2},
        {"bytes_repeat_modern_2", 2},
        {"bytes_referenced_size_modern_1", 1},
        {"bytes_find_modern_2", 2},
        {"bytes_find_modern_3", 3},
        {"bytes_find_all_modern_3", 3},
        {"map_find_2", 2},
        {"map_get_2", 2},
        {"map_from_list_1", 1},
        {"map_is_key_2", 2},
        {"map_keys_1", 1},
        {"map_merge_2", 2},
        {"map_put_3", 3},
        {"map_remove_2", 2},
        {"map_update_3", 3},
        {"map_values_1", 1},
        {"bytes_split_2", 2},
        {"bytes_split_list_3", 3},
        {"bytes_split_map_3", 3},
        {"map_take_2", 2},
        {"persistent_value_put_2", 2},
        {"persistent_value_get_1", 1},
        {"persistent_value_get_0", 0},
        {"persistent_value_erase_1", 1},
        {"persistent_value_info_0", 0},
        {"atomic_array_get_2", 2},
        {"atomic_array_put_3", 3},
        {"atomic_array_add_3", 3},
        {"atomic_array_add_get_3", 3},
        {"atomic_array_exchange_3", 3},
        {"atomic_array_compare_exchange_4", 4},
        {"atomic_array_info_1", 1},
        {"persistent_value_get_2", 2},
        {"map_from_keys_2", 2},
        {"persistent_value_put_new_2", 2},
        {"list_contains_modern_2", 2},
        {"list_reverse_modern_2", 2},
        {"map_find_modern_2", 2},
        {"map_put_modern_3", 3},
        {"text_from_utf8_modern_1", 1},
        {"atomic_array_new_list_modern_2", 2},
        {"counter_array_new_list_modern_2", 2},
        {"counter_array_get_modern_2", 2},
        {"counter_array_put_modern_3", 3},
        {"counter_array_add_modern_3", 3},
        {"counter_array_info_modern_1", 1},
        {"term_table_internal_request_all_0", 0},
        {"term_table_new_list_2", 2},
        {"term_table_delete_1", 1},
        {"term_table_delete_2", 2},
        {"term_table_delete_object_2", 2},
        {"term_table_first_1", 1},
        {"term_table_first_lookup_1", 1},
        {"term_table_is_compiled_ms_1", 1},
        {"term_table_lookup_2", 2},
        {"term_table_lookup_element_3", 3},
        {"term_table_lookup_element_4", 4},
        {"term_table_info_1", 1},
        {"term_table_info_2", 2},
        {"term_table_last_1", 1},
        {"term_table_last_lookup_1", 1},
        {"term_table_match_1", 1},
        {"term_table_match_2", 2},
        {"term_table_match_3", 3},
        {"term_table_match_object_1", 1},
        {"term_table_match_object_2", 2},
        {"term_table_match_object_3", 3},
        {"term_table_member_2", 2},
        {"term_table_next_2", 2},
        {"term_table_next_lookup_2", 2},
        {"term_table_prev_2", 2},
        {"term_table_prev_lookup_2", 2},
        {"term_table_insert_2", 2},
        {"term_table_insert_new_2", 2},
        {"term_table_rename_2", 2},
        {"term_table_safe_fixtable_2", 2},
        {"term_table_slot_2", 2},
        {"term_table_update_counter_3", 3},
        {"term_table_select_1", 1},
        {"term_table_select_2", 2},
        {"term_table_select_3", 3},
        {"term_table_select_count_2", 2},
        {"term_table_select_reverse_1", 1},
        {"term_table_select_reverse_2", 2},
        {"term_table_select_reverse_3", 3},
        {"term_table_select_replace_2", 2},
        {"term_table_match_spec_compile_1", 1},
        {"term_table_match_spec_run_r_3", 3},
        {"term_table_update_element_3", 3},
        {"term_table_give_away_3", 3},
        {"term_table_setopts_2", 2},
        {"term_table_take_2", 2},
        {"term_table_update_counter_4", 4},
        {"term_table_whereis_1", 1},
        {"term_table_internal_delete_all_2", 2},
        {"term_table_internal_select_delete_2", 2},
        {"term_table_update_element_4", 4},
    };
    for (size_t i = 0; i < sizeof contracts / sizeof *contracts; ++i) {
        entries[0].implementation = contracts[i].id;
        entries[0].arity = contracts[i].arity;
        CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_OK);
        beam_composition_release(&prepared);
        ++entries[0].arity;
        CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_INVALID_ARGUMENT);
    }
    static const char *retired[] = {
        "bytes_copy_modern_1",
        "bytes_compile_pattern_modern_1",
        "bytes_common_prefix_size_modern_1",
        "bytes_common_suffix_size_modern_1",
        "bytes_encode_unsigned_modern_1",
        "bytes_encode_unsigned_modern_2",
        "bytes_decode_unsigned_modern_1",
        "bytes_decode_unsigned_modern_2",
        "bytes_split_list_2",
        "bytes_split_map_2",
        "bytes_matches_list_2",
        "bytes_find_all_modern_2",
        "atomic_array_get_modern_2",
        "atomic_array_put_modern_3",
        "atomic_array_add_modern_3",
        "atomic_array_add_get_modern_3",
        "atomic_array_exchange_modern_3",
        "atomic_array_compare_exchange_modern_4",
        "atomic_array_info_modern_1",
        "persistent_value_put_modern_2",
        "persistent_value_put_new_modern_2",
        "persistent_value_get_modern_0",
        "persistent_value_get_modern_1",
        "persistent_value_get_modern_2",
        "persistent_value_erase_modern_1",
        "persistent_value_info_modern_0",
    };
    for (size_t i=0;i<sizeof retired/sizeof *retired;++i) {
        entries[0].implementation=retired[i];
        entries[0].arity=(unsigned)strtoul(strrchr(retired[i],'_')+1,NULL,10);
        CHECK(beam_composition_prepare(&options, &prepared, &error), BEAM_INVALID_ARGUMENT);
        if (prepared != NULL) return 1;
    }
    return 0;
}

int check_etf(void);

int
main(void)
{
    beam_host_info host = {0};
    beam_vm_options vm_options;
    beam_system *system = NULL;
    beam_vm *vm = NULL;
    beam_error error = {0};
    if (check_etf() != 0 || check_library_path() != 0 || check_embedded_dependencies() != 0 ||
        check_builtin_bindings() != 0 || check_management_unstarted(NULL) != 0) return 1;
    CHECK(beam_query_host(NULL, &error), BEAM_INVALID_ARGUMENT);
    CHECK(beam_query_host(&host, &error), BEAM_INVALID_ARGUMENT);
    host.struct_size = sizeof host;
    CHECK(beam_query_host(&host, &error), BEAM_OK);
    CHECK(beam_init(NULL, &error), BEAM_INVALID_ARGUMENT);
    CHECK(beam_vm_init(NULL, NULL, &vm, &error), BEAM_INVALID_ARGUMENT);
    CHECK(beam_init(&system, &error), BEAM_OK);
    beam_vm_options_init(&vm_options);
    if (vm_options.struct_size != sizeof vm_options ||
        vm_options.schedulers != 1 ||
        vm_options.schedulers_online != 1 || vm_options.dirty_cpu_schedulers != 1 ||
        vm_options.dirty_cpu_schedulers_online != 1 || vm_options.dirty_io_schedulers != 1 ||
        vm_options.async_threads != 1 || vm_options.poll_threads != 1 ||
        vm_options.process_limit != 1048576 || vm_options.atom_limit != 1048576 ||
        vm_options.min_heap_size != 233 || vm_options.min_bin_vheap_size != 32768 ||
        vm_options.max_heap_size != 0)
        return 1;
    --vm_options.struct_size;
    CHECK(beam_vm_init(system, &vm_options, &vm, &error), BEAM_INVALID_ARGUMENT);
    beam_vm_options_init(&vm_options);
    vm_options.schedulers = 0;
    CHECK(beam_vm_init(system, &vm_options, &vm, &error), BEAM_INVALID_ARGUMENT);
    beam_vm_options_init(&vm_options);
    vm_options.schedulers_online = 2;
    CHECK(beam_vm_init(system, &vm_options, &vm, &error), BEAM_INVALID_ARGUMENT);
    beam_vm_options_init(&vm_options);
    vm_options.dirty_cpu_schedulers_online = 2;
    CHECK(beam_vm_init(system, &vm_options, &vm, &error), BEAM_INVALID_ARGUMENT);
    beam_vm_options_init(&vm_options);
    vm_options.min_heap_size = INT_MAX;
    CHECK(beam_vm_init(system, &vm_options, &vm, &error), BEAM_INVALID_ARGUMENT);
    beam_vm_options_init(&vm_options);
    vm_options.min_bin_vheap_size = INT_MAX;
    CHECK(beam_vm_init(system, &vm_options, &vm, &error), BEAM_INVALID_ARGUMENT);
    beam_vm_options_init(&vm_options);
    vm_options.min_heap_size = 234;
    vm_options.max_heap_size = 234;
    CHECK(beam_vm_init(system, &vm_options, &vm, &error), BEAM_INVALID_ARGUMENT);
    if (vm != NULL || error.message[0] == '\0')
        return 1;
    /* Defaults and a fresh explicit composition share one system. */
    CHECK(beam_vm_init(system, NULL, &vm, &error), BEAM_OK);
    if(check_management_unstarted(vm)) return 1;
    CHECK(beam_vm_terminate(&vm, NULL), BEAM_OK);
    beam_vm_options_init(&vm_options);
    beam_composition_options preparation = {0};
    beam_composition *composition = NULL;
    preparation.struct_size = sizeof preparation;
    CHECK(beam_composition_prepare(&preparation, &composition, &error), BEAM_OK);
    vm_options.composition = composition;
    CHECK(beam_vm_init(system, &vm_options, &vm, &error), BEAM_OK);
    CHECK(beam_vm_terminate(&vm, NULL), BEAM_OK);
    beam_composition_release(&composition);
    CHECK(beam_terminate(&system), BEAM_OK);
    if (system != NULL)
        return 1;
    return 0;
}
