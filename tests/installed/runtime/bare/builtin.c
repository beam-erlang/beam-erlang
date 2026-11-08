/* SPDX-License-Identifier: Apache-2.0 */
#include <beam/beam.h>
#include <beam/library.h>
#include <beam/libraries/tracing.h>
#include <erl_nif.h>
#include "fixtures.h"
#include "support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "builtin:%d: %s (%s)\n", __LINE__, #test, error.message); return 1; } } while (0)

static const beam_builtin_binding modern_bindings[] = {
{sizeof(beam_builtin_binding), "vm_abs_1", "beam", "abs", 1},
{sizeof(beam_builtin_binding), "vm_adler32_1", "beam", "adler32", 1},
{sizeof(beam_builtin_binding), "vm_adler32_2", "beam", "adler32", 2},
{sizeof(beam_builtin_binding), "vm_adler32_combine_3", "beam", "adler32_combine", 3},
{sizeof(beam_builtin_binding), "vm_atom_to_list_1", "beam", "atom_to_list", 1},
{sizeof(beam_builtin_binding), "vm_binary_to_list_1", "beam", "binary_to_list", 1},
{sizeof(beam_builtin_binding), "vm_binary_to_list_3", "beam", "binary_to_list", 3},
{sizeof(beam_builtin_binding), "vm_binary_to_term_1", "beam", "binary_to_term", 1},
{sizeof(beam_builtin_binding), "vm_crc32_1", "beam", "crc32", 1},
{sizeof(beam_builtin_binding), "vm_crc32_2", "beam", "crc32", 2},
{sizeof(beam_builtin_binding), "vm_crc32_combine_3", "beam", "crc32_combine", 3},
{sizeof(beam_builtin_binding), "vm_date_0", "beam", "date", 0},
{sizeof(beam_builtin_binding), "vm_delete_module_1", "beam", "delete_module", 1},
{sizeof(beam_builtin_binding), "vm_display_1", "beam", "display", 1},
{sizeof(beam_builtin_binding), "vm_display_string_2", "beam", "display_string", 2},
{sizeof(beam_builtin_binding), "vm_element_2", "beam", "element", 2},
{sizeof(beam_builtin_binding), "vm_erase_0", "beam", "erase", 0},
{sizeof(beam_builtin_binding), "vm_erase_1", "beam", "erase", 1},
{sizeof(beam_builtin_binding), "vm_exit_1", "beam", "exit", 1},
{sizeof(beam_builtin_binding), "vm_exit_2", "beam", "exit", 2},
{sizeof(beam_builtin_binding), "vm_exit_signal_2", "beam", "exit_signal", 2},
{sizeof(beam_builtin_binding), "vm_external_size_1", "beam", "external_size", 1},
{sizeof(beam_builtin_binding), "vm_external_size_2", "beam", "external_size", 2},
{sizeof(beam_builtin_binding), "vm_float_1", "beam", "float", 1},
{sizeof(beam_builtin_binding), "vm_float_to_list_1", "beam", "float_to_list", 1},
{sizeof(beam_builtin_binding), "vm_float_to_list_2", "beam", "float_to_list", 2},
{sizeof(beam_builtin_binding), "vm_fun_info_2", "beam", "fun_info", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_garbage_collect_1", "erts_internal", "garbage_collect", 1},
{sizeof(beam_builtin_binding), "vm_get_0", "beam", "get", 0},
{sizeof(beam_builtin_binding), "vm_get_1", "beam", "get", 1},
{sizeof(beam_builtin_binding), "vm_get_keys_1", "beam", "get_keys", 1},
{sizeof(beam_builtin_binding), "vm_group_leader_0", "beam", "group_leader", 0},
{sizeof(beam_builtin_binding), "vm_erts_internal_group_leader_2", "erts_internal", "group_leader", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_group_leader_3", "erts_internal", "group_leader", 3},
{sizeof(beam_builtin_binding), "vm_halt_2", "beam", "halt", 2},
{sizeof(beam_builtin_binding), "vm_phash_2", "beam", "phash", 2},
{sizeof(beam_builtin_binding), "vm_phash2_1", "beam", "phash2", 1},
{sizeof(beam_builtin_binding), "vm_phash2_2", "beam", "phash2", 2},
{sizeof(beam_builtin_binding), "vm_hd_1", "beam", "hd", 1},
{sizeof(beam_builtin_binding), "vm_integer_to_list_1", "beam", "integer_to_list", 1},
{sizeof(beam_builtin_binding), "vm_length_1", "beam", "length", 1},
{sizeof(beam_builtin_binding), "vm_link_1", "beam", "link", 1},
{sizeof(beam_builtin_binding), "vm_list_to_atom_1", "beam", "list_to_atom", 1},
{sizeof(beam_builtin_binding), "vm_list_to_binary_1", "beam", "list_to_binary", 1},
{sizeof(beam_builtin_binding), "vm_list_to_float_1", "beam", "list_to_float", 1},
{sizeof(beam_builtin_binding), "vm_list_to_pid_1", "beam", "list_to_pid", 1},
{sizeof(beam_builtin_binding), "vm_list_to_port_1", "beam", "list_to_port", 1},
{sizeof(beam_builtin_binding), "vm_list_to_ref_1", "beam", "list_to_ref", 1},
{sizeof(beam_builtin_binding), "vm_list_to_tuple_1", "beam", "list_to_tuple", 1},
{sizeof(beam_builtin_binding), "vm_loaded_0", "beam", "loaded", 0},
{sizeof(beam_builtin_binding), "vm_localtime_0", "beam", "localtime", 0},
{sizeof(beam_builtin_binding), "vm_localtime_to_universaltime_2", "beam", "localtime_to_universaltime", 2},
{sizeof(beam_builtin_binding), "vm_make_ref_0", "beam", "make_ref", 0},
{sizeof(beam_builtin_binding), "vm_unique_integer_0", "beam", "unique_integer", 0},
{sizeof(beam_builtin_binding), "vm_unique_integer_1", "beam", "unique_integer", 1},
{sizeof(beam_builtin_binding), "vm_md5_1", "beam", "md5", 1},
{sizeof(beam_builtin_binding), "vm_md5_init_0", "beam", "md5_init", 0},
{sizeof(beam_builtin_binding), "vm_md5_update_2", "beam", "md5_update", 2},
{sizeof(beam_builtin_binding), "vm_md5_final_1", "beam", "md5_final", 1},
{sizeof(beam_builtin_binding), "vm_module_loaded_1", "beam", "module_loaded", 1},
{sizeof(beam_builtin_binding), "vm_function_exported_3", "beam", "function_exported", 3},
{sizeof(beam_builtin_binding), "vm_monitor_node_2", "beam", "monitor_node", 2},
{sizeof(beam_builtin_binding), "vm_monitor_node_3", "beam", "monitor_node", 3},
{sizeof(beam_builtin_binding), "vm_node_1", "beam", "node", 1},
{sizeof(beam_builtin_binding), "vm_node_0", "beam", "node", 0},
{sizeof(beam_builtin_binding), "vm_nodes_0", "beam", "nodes", 0},
{sizeof(beam_builtin_binding), "vm_nodes_1", "beam", "nodes", 1},
{sizeof(beam_builtin_binding), "vm_nodes_2", "beam", "nodes", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_open_port_2", "erts_internal", "open_port", 2},
{sizeof(beam_builtin_binding), "vm_pid_to_list_1", "beam", "pid_to_list", 1},
{sizeof(beam_builtin_binding), "vm_ports_0", "beam", "ports", 0},
{sizeof(beam_builtin_binding), "vm_pre_loaded_0", "beam", "pre_loaded", 0},
{sizeof(beam_builtin_binding), "vm_process_flag_2", "beam", "process_flag", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_process_flag_3", "erts_internal", "process_flag", 3},
{sizeof(beam_builtin_binding), "vm_process_info_1", "beam", "process_info", 1},
{sizeof(beam_builtin_binding), "vm_process_info_2", "beam", "process_info", 2},
{sizeof(beam_builtin_binding), "vm_processes_0", "beam", "processes", 0},
{sizeof(beam_builtin_binding), "vm_put_2", "beam", "put", 2},
{sizeof(beam_builtin_binding), "vm_register_2", "beam", "register", 2},
{sizeof(beam_builtin_binding), "vm_registered_0", "beam", "registered", 0},
{sizeof(beam_builtin_binding), "vm_round_1", "beam", "round", 1},
{sizeof(beam_builtin_binding), "vm_self_0", "beam", "self", 0},
{sizeof(beam_builtin_binding), "vm_setelement_3", "beam", "setelement", 3},
{sizeof(beam_builtin_binding), "vm_size_1", "beam", "size", 1},
{sizeof(beam_builtin_binding), "vm_spawn_3", "beam", "spawn", 3},
{sizeof(beam_builtin_binding), "vm_spawn_link_3", "beam", "spawn_link", 3},
{sizeof(beam_builtin_binding), "vm_split_binary_2", "beam", "split_binary", 2},
{sizeof(beam_builtin_binding), "vm_statistics_1", "beam", "statistics", 1},
{sizeof(beam_builtin_binding), "vm_term_to_binary_1", "beam", "term_to_binary", 1},
{sizeof(beam_builtin_binding), "vm_term_to_binary_2", "beam", "term_to_binary", 2},
{sizeof(beam_builtin_binding), "vm_term_to_iovec_1", "beam", "term_to_iovec", 1},
{sizeof(beam_builtin_binding), "vm_term_to_iovec_2", "beam", "term_to_iovec", 2},
{sizeof(beam_builtin_binding), "vm_throw_1", "beam", "throw", 1},
{sizeof(beam_builtin_binding), "vm_time_0", "beam", "time", 0},
{sizeof(beam_builtin_binding), "vm_tl_1", "beam", "tl", 1},
{sizeof(beam_builtin_binding), "vm_trunc_1", "beam", "trunc", 1},
{sizeof(beam_builtin_binding), "vm_tuple_to_list_1", "beam", "tuple_to_list", 1},
{sizeof(beam_builtin_binding), "vm_universaltime_0", "beam", "universaltime", 0},
{sizeof(beam_builtin_binding), "vm_universaltime_to_localtime_1", "beam", "universaltime_to_localtime", 1},
{sizeof(beam_builtin_binding), "vm_unlink_1", "beam", "unlink", 1},
{sizeof(beam_builtin_binding), "vm_unregister_1", "beam", "unregister", 1},
{sizeof(beam_builtin_binding), "vm_whereis_1", "beam", "whereis", 1},
{sizeof(beam_builtin_binding), "vm_spawn_opt_4", "beam", "spawn_opt", 4},
{sizeof(beam_builtin_binding), "vm_setnode_2", "beam", "setnode", 2},
{sizeof(beam_builtin_binding), "vm_dist_get_stat_1", "beam", "dist_get_stat", 1},
{sizeof(beam_builtin_binding), "vm_dist_ctrl_input_handler_2", "beam", "dist_ctrl_input_handler", 2},
{sizeof(beam_builtin_binding), "vm_dist_ctrl_put_data_2", "beam", "dist_ctrl_put_data", 2},
{sizeof(beam_builtin_binding), "vm_dist_ctrl_get_data_1", "beam", "dist_ctrl_get_data", 1},
{sizeof(beam_builtin_binding), "vm_dist_ctrl_get_data_notification_1", "beam", "dist_ctrl_get_data_notification", 1},
{sizeof(beam_builtin_binding), "vm_dist_ctrl_get_opt_2", "beam", "dist_ctrl_get_opt", 2},
{sizeof(beam_builtin_binding), "vm_dist_ctrl_set_opt_3", "beam", "dist_ctrl_set_opt", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_port_info_1", "erts_internal", "port_info", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_port_info_2", "erts_internal", "port_info", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_port_call_3", "erts_internal", "port_call", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_port_command_3", "erts_internal", "port_command", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_port_control_3", "erts_internal", "port_control", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_port_close_1", "erts_internal", "port_close", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_port_connect_2", "erts_internal", "port_connect", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_request_system_task_3", "erts_internal", "request_system_task", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_request_system_task_4", "erts_internal", "request_system_task", 4},
{sizeof(beam_builtin_binding), "vm_erts_internal_check_process_code_1", "erts_internal", "check_process_code", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_map_to_tuple_keys_1", "erts_internal", "map_to_tuple_keys", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_term_type_1", "erts_internal", "term_type", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_map_hashmap_children_1", "erts_internal", "map_hashmap_children", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_time_unit_0", "erts_internal", "time_unit", 0},
{sizeof(beam_builtin_binding), "vm_erts_internal_perf_counter_unit_0", "erts_internal", "perf_counter_unit", 0},
{sizeof(beam_builtin_binding), "vm_erts_internal_is_system_process_1", "erts_internal", "is_system_process", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_system_check_1", "erts_internal", "system_check", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_host_request_1", "erts_internal", "host_request", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_request_failure_2", "erts_internal", "request_failure", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_vm_context_0", "erts_internal", "vm_context", 0},
{sizeof(beam_builtin_binding), "vm_erts_internal_scheduler_wall_time_1", "erts_internal", "scheduler_wall_time", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_dirty_process_handle_signals_1", "erts_internal", "dirty_process_handle_signals", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_create_dist_channel_3", "erts_internal", "create_dist_channel", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_ets_super_user_1", "erts_internal", "ets_super_user", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_spawn_request_4", "erts_internal", "spawn_request", 4},
{sizeof(beam_builtin_binding), "vm_erts_internal_dist_spawn_request_4", "erts_internal", "dist_spawn_request", 4},
{sizeof(beam_builtin_binding), "vm_erts_internal_no_aux_work_threads_0", "erts_internal", "no_aux_work_threads", 0},
{sizeof(beam_builtin_binding), "vm_spawn_request_abandon_1", "beam", "spawn_request_abandon", 1},
{sizeof(beam_builtin_binding), "vm_erts_literal_area_collector_release_area_switch_0", "erts_literal_area_collector", "release_area_switch", 0},
{sizeof(beam_builtin_binding), "vm_erts_literal_area_collector_send_copy_request_3", "erts_literal_area_collector", "send_copy_request", 3},
{sizeof(beam_builtin_binding), "vm_port_set_data_2", "beam", "port_set_data", 2},
{sizeof(beam_builtin_binding), "vm_port_get_data_1", "beam", "port_get_data", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_trace_pattern_3", "erts_internal", "trace_pattern", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_trace_3", "erts_internal", "trace", 3},
{sizeof(beam_builtin_binding), "vm_trace_delivered_1", "beam_trace", "trace_delivered", 1},
{sizeof(beam_builtin_binding), "vm_seq_trace_2", "beam_trace", "seq_trace", 2},
{sizeof(beam_builtin_binding), "vm_seq_trace_info_1", "beam_trace", "seq_trace_info", 1},
{sizeof(beam_builtin_binding), "vm_seq_trace_print_1", "beam_trace", "seq_trace_print", 1},
{sizeof(beam_builtin_binding), "vm_seq_trace_print_2", "beam_trace", "seq_trace_print", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_suspend_process_2", "erts_internal", "suspend_process", 2},
{sizeof(beam_builtin_binding), "vm_resume_process_1", "beam", "resume_process", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_process_display_2", "erts_internal", "process_display", 2},
{sizeof(beam_builtin_binding), "vm_bump_reductions_1", "beam", "bump_reductions", 1},
{sizeof(beam_builtin_binding), "vm_math_cos_1", "beam_math", "cos", 1},
{sizeof(beam_builtin_binding), "vm_math_cosh_1", "beam_math", "cosh", 1},
{sizeof(beam_builtin_binding), "vm_math_sin_1", "beam_math", "sin", 1},
{sizeof(beam_builtin_binding), "vm_math_sinh_1", "beam_math", "sinh", 1},
{sizeof(beam_builtin_binding), "vm_math_tan_1", "beam_math", "tan", 1},
{sizeof(beam_builtin_binding), "vm_math_tanh_1", "beam_math", "tanh", 1},
{sizeof(beam_builtin_binding), "vm_math_acos_1", "beam_math", "acos", 1},
{sizeof(beam_builtin_binding), "vm_math_acosh_1", "beam_math", "acosh", 1},
{sizeof(beam_builtin_binding), "vm_math_asin_1", "beam_math", "asin", 1},
{sizeof(beam_builtin_binding), "vm_math_asinh_1", "beam_math", "asinh", 1},
{sizeof(beam_builtin_binding), "vm_math_atan_1", "beam_math", "atan", 1},
{sizeof(beam_builtin_binding), "vm_math_atanh_1", "beam_math", "atanh", 1},
{sizeof(beam_builtin_binding), "vm_math_erf_1", "beam_math", "erf", 1},
{sizeof(beam_builtin_binding), "vm_math_erfc_1", "beam_math", "erfc", 1},
{sizeof(beam_builtin_binding), "vm_math_exp_1", "beam_math", "exp", 1},
{sizeof(beam_builtin_binding), "vm_math_log_1", "beam_math", "log", 1},
{sizeof(beam_builtin_binding), "vm_math_log2_1", "beam_math", "log2", 1},
{sizeof(beam_builtin_binding), "vm_math_log10_1", "beam_math", "log10", 1},
{sizeof(beam_builtin_binding), "vm_math_sqrt_1", "beam_math", "sqrt", 1},
{sizeof(beam_builtin_binding), "vm_math_atan2_2", "beam_math", "atan2", 2},
{sizeof(beam_builtin_binding), "vm_math_pow_2", "beam_math", "pow", 2},
{sizeof(beam_builtin_binding), "vm_make_tuple_2", "beam", "make_tuple", 2},
{sizeof(beam_builtin_binding), "vm_append_element_2", "beam", "append_element", 2},
{sizeof(beam_builtin_binding), "vm_make_tuple_3", "beam", "make_tuple", 3},
{sizeof(beam_builtin_binding), "vm_system_flag_2", "beam", "system_flag", 2},
{sizeof(beam_builtin_binding), "vm_system_info_1", "beam", "system_info", 1},
{sizeof(beam_builtin_binding), "vm_system_profile_2", "beam", "system_profile", 2},
{sizeof(beam_builtin_binding), "vm_system_profile_0", "beam", "system_profile", 0},
{sizeof(beam_builtin_binding), "vm_ref_to_list_1", "beam", "ref_to_list", 1},
{sizeof(beam_builtin_binding), "vm_port_to_list_1", "beam", "port_to_list", 1},
{sizeof(beam_builtin_binding), "vm_fun_to_list_1", "beam", "fun_to_list", 1},
{sizeof(beam_builtin_binding), "vm_monitor_2", "beam", "monitor", 2},
{sizeof(beam_builtin_binding), "vm_demonitor_1", "beam", "demonitor", 1},
{sizeof(beam_builtin_binding), "vm_demonitor_2", "beam", "demonitor", 2},
{sizeof(beam_builtin_binding), "vm_is_process_alive_1", "beam", "is_process_alive", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_is_process_alive_2", "erts_internal", "is_process_alive", 2},
{sizeof(beam_builtin_binding), "vm_error_1", "beam", "error", 1},
{sizeof(beam_builtin_binding), "vm_error_2", "beam", "error", 2},
{sizeof(beam_builtin_binding), "vm_raise_3", "beam", "raise", 3},
{sizeof(beam_builtin_binding), "vm_is_builtin_3", "beam", "is_builtin", 3},
{sizeof(beam_builtin_binding), "vm_and_2", "beam", "and", 2},
{sizeof(beam_builtin_binding), "vm_or_2", "beam", "or", 2},
{sizeof(beam_builtin_binding), "vm_xor_2", "beam", "xor", 2},
{sizeof(beam_builtin_binding), "vm_not_1", "beam", "not", 1},
{sizeof(beam_builtin_binding), "vm_sgt_2", "beam", ">", 2},
{sizeof(beam_builtin_binding), "vm_sge_2", "beam", ">=", 2},
{sizeof(beam_builtin_binding), "vm_slt_2", "beam", "<", 2},
{sizeof(beam_builtin_binding), "vm_sle_2", "beam", "=<", 2},
{sizeof(beam_builtin_binding), "vm_seq_2", "beam", "=:=", 2},
{sizeof(beam_builtin_binding), "vm_seqeq_2", "beam", "==", 2},
{sizeof(beam_builtin_binding), "vm_sneq_2", "beam", "=/=", 2},
{sizeof(beam_builtin_binding), "vm_sneqeq_2", "beam", "/=", 2},
{sizeof(beam_builtin_binding), "vm_splus_2", "beam", "+", 2},
{sizeof(beam_builtin_binding), "vm_sminus_2", "beam", "-", 2},
{sizeof(beam_builtin_binding), "vm_stimes_2", "beam", "*", 2},
{sizeof(beam_builtin_binding), "vm_div_2", "beam", "/", 2},
{sizeof(beam_builtin_binding), "vm_intdiv_2", "beam", "div", 2},
{sizeof(beam_builtin_binding), "vm_rem_2", "beam", "rem", 2},
{sizeof(beam_builtin_binding), "vm_bor_2", "beam", "bor", 2},
{sizeof(beam_builtin_binding), "vm_band_2", "beam", "band", 2},
{sizeof(beam_builtin_binding), "vm_bxor_2", "beam", "bxor", 2},
{sizeof(beam_builtin_binding), "vm_bsl_2", "beam", "bsl", 2},
{sizeof(beam_builtin_binding), "vm_bsr_2", "beam", "bsr", 2},
{sizeof(beam_builtin_binding), "vm_bnot_1", "beam", "bnot", 1},
{sizeof(beam_builtin_binding), "vm_sminus_1", "beam", "-", 1},
{sizeof(beam_builtin_binding), "vm_splus_1", "beam", "+", 1},
{sizeof(beam_builtin_binding), "vm_ebif_bang_2", "beam", "!", 2},
{sizeof(beam_builtin_binding), "vm_send_2", "beam", "send", 2},
{sizeof(beam_builtin_binding), "vm_send_3", "beam", "send", 3},
{sizeof(beam_builtin_binding), "vm_ebif_plusplus_2", "beam", "++", 2},
{sizeof(beam_builtin_binding), "vm_append_2", "beam", "append", 2},
{sizeof(beam_builtin_binding), "vm_ebif_minusminus_2", "beam", "--", 2},
{sizeof(beam_builtin_binding), "vm_subtract_2", "beam", "subtract", 2},
{sizeof(beam_builtin_binding), "vm_is_atom_1", "beam", "is_atom", 1},
{sizeof(beam_builtin_binding), "vm_is_list_1", "beam", "is_list", 1},
{sizeof(beam_builtin_binding), "vm_is_tuple_1", "beam", "is_tuple", 1},
{sizeof(beam_builtin_binding), "vm_is_float_1", "beam", "is_float", 1},
{sizeof(beam_builtin_binding), "vm_is_integer_1", "beam", "is_integer", 1},
{sizeof(beam_builtin_binding), "vm_is_number_1", "beam", "is_number", 1},
{sizeof(beam_builtin_binding), "vm_is_pid_1", "beam", "is_pid", 1},
{sizeof(beam_builtin_binding), "vm_is_port_1", "beam", "is_port", 1},
{sizeof(beam_builtin_binding), "vm_is_reference_1", "beam", "is_reference", 1},
{sizeof(beam_builtin_binding), "vm_is_binary_1", "beam", "is_binary", 1},
{sizeof(beam_builtin_binding), "vm_is_function_1", "beam", "is_function", 1},
{sizeof(beam_builtin_binding), "vm_is_function_2", "beam", "is_function", 2},
{sizeof(beam_builtin_binding), "vm_is_record_2", "beam", "is_record", 2},
{sizeof(beam_builtin_binding), "vm_is_record_3", "beam", "is_record", 3},
{sizeof(beam_builtin_binding), "vm_match_spec_test_3", "beam", "match_spec_test", 3},
{sizeof(beam_builtin_binding), "vm_os_getenv_1", "os", "getenv", 1},
{sizeof(beam_builtin_binding), "vm_os_putenv_2", "os", "putenv", 2},
{sizeof(beam_builtin_binding), "vm_os_unsetenv_1", "os", "unsetenv", 1},
{sizeof(beam_builtin_binding), "vm_os_getpid_0", "os", "getpid", 0},
{sizeof(beam_builtin_binding), "vm_os_timestamp_0", "os", "timestamp", 0},
{sizeof(beam_builtin_binding), "vm_os_system_time_0", "os", "system_time", 0},
{sizeof(beam_builtin_binding), "vm_os_system_time_1", "os", "system_time", 1},
{sizeof(beam_builtin_binding), "vm_os_perf_counter_0", "os", "perf_counter", 0},
{sizeof(beam_builtin_binding), "vm_erl_ddll_try_load_3", "beam_ddll", "try_load", 3},
{sizeof(beam_builtin_binding), "vm_erl_ddll_try_unload_2", "beam_ddll", "try_unload", 2},
{sizeof(beam_builtin_binding), "vm_erl_ddll_loaded_drivers_0", "beam_ddll", "loaded_drivers", 0},
{sizeof(beam_builtin_binding), "vm_erl_ddll_info_2", "beam_ddll", "info", 2},
{sizeof(beam_builtin_binding), "vm_erl_ddll_format_error_int_1", "beam_ddll", "format_error_int", 1},
{sizeof(beam_builtin_binding), "vm_erl_ddll_monitor_2", "beam_ddll", "monitor", 2},
{sizeof(beam_builtin_binding), "vm_erl_ddll_demonitor_1", "beam_ddll", "demonitor", 1},
{sizeof(beam_builtin_binding), "vm_erts_debug_disassemble_1", "erts_debug", "disassemble", 1},
{sizeof(beam_builtin_binding), "vm_erts_debug_breakpoint_2", "erts_debug", "breakpoint", 2},
{sizeof(beam_builtin_binding), "vm_erts_debug_same_2", "erts_debug", "same", 2},
{sizeof(beam_builtin_binding), "vm_erts_debug_flat_size_1", "erts_debug", "flat_size", 1},
{sizeof(beam_builtin_binding), "vm_erts_debug_get_internal_state_1", "erts_debug", "get_internal_state", 1},
{sizeof(beam_builtin_binding), "vm_erts_debug_set_internal_state_2", "erts_debug", "set_internal_state", 2},
{sizeof(beam_builtin_binding), "vm_erts_debug_dist_ext_to_term_2", "erts_debug", "dist_ext_to_term", 2},
{sizeof(beam_builtin_binding), "vm_erts_debug_instructions_0", "erts_debug", "instructions", 0},
{sizeof(beam_builtin_binding), "vm_erts_debug_interpreter_size_0", "erts_debug", "interpreter_size", 0},
{sizeof(beam_builtin_binding), "vm_erts_debug_dirty_cpu_2", "erts_debug", "dirty_cpu", 2},
{sizeof(beam_builtin_binding), "vm_erts_debug_dirty_io_2", "erts_debug", "dirty_io", 2},
{sizeof(beam_builtin_binding), "vm_erts_debug_dirty_3", "erts_debug", "dirty", 3},
{sizeof(beam_builtin_binding), "vm_erts_debug_lcnt_control_2", "erts_debug", "lcnt_control", 2},
{sizeof(beam_builtin_binding), "vm_erts_debug_lcnt_control_1", "erts_debug", "lcnt_control", 1},
{sizeof(beam_builtin_binding), "vm_erts_debug_lcnt_collect_0", "erts_debug", "lcnt_collect", 0},
{sizeof(beam_builtin_binding), "vm_erts_debug_lcnt_clear_0", "erts_debug", "lcnt_clear", 0},
{sizeof(beam_builtin_binding), "vm_hibernate_3", "beam", "hibernate", 3},
{sizeof(beam_builtin_binding), "vm_error_logger_warning_map_0", "error_logger", "warning_map", 0},
{sizeof(beam_builtin_binding), "vm_get_module_info_1", "beam", "get_module_info", 1},
{sizeof(beam_builtin_binding), "vm_get_module_info_2", "beam", "get_module_info", 2},
{sizeof(beam_builtin_binding), "vm_is_boolean_1", "beam", "is_boolean", 1},
{sizeof(beam_builtin_binding), "vm_make_fun_3", "beam", "make_fun", 3},
{sizeof(beam_builtin_binding), "vm_iolist_size_1", "beam", "iolist_size", 1},
{sizeof(beam_builtin_binding), "vm_iolist_to_binary_1", "beam", "iolist_to_binary", 1},
{sizeof(beam_builtin_binding), "vm_list_to_existing_atom_1", "beam", "list_to_existing_atom", 1},
{sizeof(beam_builtin_binding), "vm_is_bitstring_1", "beam", "is_bitstring", 1},
{sizeof(beam_builtin_binding), "vm_tuple_size_1", "beam", "tuple_size", 1},
{sizeof(beam_builtin_binding), "vm_byte_size_1", "beam", "byte_size", 1},
{sizeof(beam_builtin_binding), "vm_bit_size_1", "beam", "bit_size", 1},
{sizeof(beam_builtin_binding), "vm_list_to_bitstring_1", "beam", "list_to_bitstring", 1},
{sizeof(beam_builtin_binding), "vm_bitstring_to_list_1", "beam", "bitstring_to_list", 1},
{sizeof(beam_builtin_binding), "vm_decode_packet_3", "beam", "decode_packet", 3},
{sizeof(beam_builtin_binding), "vm_atom_to_binary_2", "beam", "atom_to_binary", 2},
{sizeof(beam_builtin_binding), "vm_binary_to_atom_2", "beam", "binary_to_atom", 2},
{sizeof(beam_builtin_binding), "vm_binary_to_existing_atom_2", "beam", "binary_to_existing_atom", 2},
{sizeof(beam_builtin_binding), "vm_net_kernel_dflag_unicode_io_1", "net_kernel", "dflag_unicode_io", 1},
{sizeof(beam_builtin_binding), "vm_load_nif_2", "beam", "load_nif", 2},
{sizeof(beam_builtin_binding), "vm_call_on_load_function_1", "beam", "call_on_load_function", 1},
{sizeof(beam_builtin_binding), "vm_finish_after_on_load_2", "beam", "finish_after_on_load", 2},
{sizeof(beam_builtin_binding), "vm_binary_to_term_2", "beam", "binary_to_term", 2},
{sizeof(beam_builtin_binding), "vm_binary_part_2", "beam", "binary_part", 2},
{sizeof(beam_builtin_binding), "vm_binary_part_3", "beam", "binary_part", 3},
{sizeof(beam_builtin_binding), "vm_nif_error_1", "beam", "nif_error", 1},
{sizeof(beam_builtin_binding), "vm_nif_error_2", "beam", "nif_error", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_filename_to_native_1", "erts_internal", "filename_to_native", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_filename_from_native_1", "erts_internal", "filename_from_native", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_filename_normalize_utf8_1", "erts_internal", "filename_normalize_utf8", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_filename_is_translatable_1", "erts_internal", "filename_is_translatable", 1},
{sizeof(beam_builtin_binding), "vm_file_native_name_encoding_0", "file", "native_name_encoding", 0},
{sizeof(beam_builtin_binding), "vm_check_old_code_1", "beam", "check_old_code", 1},
{sizeof(beam_builtin_binding), "vm_universaltime_to_posixtime_1", "beam", "universaltime_to_posixtime", 1},
{sizeof(beam_builtin_binding), "vm_posixtime_to_universaltime_1", "beam", "posixtime_to_universaltime", 1},
{sizeof(beam_builtin_binding), "vm_dt_put_tag_1", "beam", "dt_put_tag", 1},
{sizeof(beam_builtin_binding), "vm_dt_get_tag_0", "beam", "dt_get_tag", 0},
{sizeof(beam_builtin_binding), "vm_dt_get_tag_data_0", "beam", "dt_get_tag_data", 0},
{sizeof(beam_builtin_binding), "vm_dt_spread_tag_1", "beam", "dt_spread_tag", 1},
{sizeof(beam_builtin_binding), "vm_dt_restore_tag_1", "beam", "dt_restore_tag", 1},
{sizeof(beam_builtin_binding), "vm_dt_prepend_vm_tag_data_1", "beam", "dt_prepend_vm_tag_data", 1},
{sizeof(beam_builtin_binding), "vm_dt_append_vm_tag_data_1", "beam", "dt_append_vm_tag_data", 1},
{sizeof(beam_builtin_binding), "vm_finish_loading_1", "beam", "finish_loading", 1},
{sizeof(beam_builtin_binding), "vm_insert_element_3", "beam", "insert_element", 3},
{sizeof(beam_builtin_binding), "vm_delete_element_2", "beam", "delete_element", 2},
{sizeof(beam_builtin_binding), "vm_integer_to_binary_1", "beam", "integer_to_binary", 1},
{sizeof(beam_builtin_binding), "vm_float_to_binary_1", "beam", "float_to_binary", 1},
{sizeof(beam_builtin_binding), "vm_float_to_binary_2", "beam", "float_to_binary", 2},
{sizeof(beam_builtin_binding), "vm_binary_to_float_1", "beam", "binary_to_float", 1},
{sizeof(beam_builtin_binding), "vm_io_printable_range_0", "io", "printable_range", 0},
{sizeof(beam_builtin_binding), "vm_is_map_1", "beam", "is_map", 1},
{sizeof(beam_builtin_binding), "vm_map_size_1", "beam", "map_size", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_cmp_term_2", "erts_internal", "cmp_term", 2},
{sizeof(beam_builtin_binding), "vm_fun_info_mfa_1", "beam", "fun_info_mfa", 1},
{sizeof(beam_builtin_binding), "vm_get_keys_0", "beam", "get_keys", 0},
{sizeof(beam_builtin_binding), "vm_erts_debug_map_info_1", "erts_debug", "map_info", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_is_process_executing_dirty_1", "erts_internal", "is_process_executing_dirty", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_check_dirty_process_code_2", "erts_internal", "check_dirty_process_code", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_purge_module_2", "erts_internal", "purge_module", 2},
{sizeof(beam_builtin_binding), "vm_erts_debug_size_shared_1", "erts_debug", "size_shared", 1},
{sizeof(beam_builtin_binding), "vm_erts_debug_copy_shared_2", "erts_debug", "copy_shared", 2},
{sizeof(beam_builtin_binding), "vm_has_prepared_code_on_load_1", "beam", "has_prepared_code_on_load", 1},
{sizeof(beam_builtin_binding), "vm_floor_1", "beam", "floor", 1},
{sizeof(beam_builtin_binding), "vm_ceil_1", "beam", "ceil", 1},
{sizeof(beam_builtin_binding), "vm_math_floor_1", "beam_math", "floor", 1},
{sizeof(beam_builtin_binding), "vm_math_ceil_1", "beam_math", "ceil", 1},
{sizeof(beam_builtin_binding), "vm_math_fmod_2", "beam_math", "fmod", 2},
{sizeof(beam_builtin_binding), "vm_os_set_signal_2", "os", "set_signal", 2},
{sizeof(beam_builtin_binding), "vm_iolist_to_iovec_1", "beam", "iolist_to_iovec", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_get_dflags_0", "erts_internal", "get_dflags", 0},
{sizeof(beam_builtin_binding), "vm_erts_internal_new_connection_1", "erts_internal", "new_connection", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_map_next_3", "erts_internal", "map_next", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_gather_alloc_histograms_1", "erts_internal", "gather_alloc_histograms", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_gather_carrier_info_1", "erts_internal", "gather_carrier_info", 1},
{sizeof(beam_builtin_binding), "vm_map_get_2", "beam", "map_get", 2},
{sizeof(beam_builtin_binding), "vm_is_map_key_2", "beam", "is_map_key", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_erase_persistent_terms_0", "erts_internal", "erase_persistent_terms", 0},
{sizeof(beam_builtin_binding), "vm_erts_internal_atomics_new_2", "erts_internal", "atomics_new", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_counters_new_1", "erts_internal", "counters_new", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_counters_get_2", "erts_internal", "counters_get", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_counters_add_3", "erts_internal", "counters_add", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_counters_put_3", "erts_internal", "counters_put", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_counters_info_1", "erts_internal", "counters_info", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_spawn_system_process_3", "erts_internal", "spawn_system_process", 3},
{sizeof(beam_builtin_binding), "vm_integer_to_list_2", "beam", "integer_to_list", 2},
{sizeof(beam_builtin_binding), "vm_integer_to_binary_2", "beam", "integer_to_binary", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_ets_lookup_binary_info_2", "erts_internal", "ets_lookup_binary_info", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_ets_raw_first_1", "erts_internal", "ets_raw_first", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_ets_raw_next_2", "erts_internal", "ets_raw_next", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_abort_pending_connection_2", "erts_internal", "abort_pending_connection", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_get_creation_0", "erts_internal", "get_creation", 0},
{sizeof(beam_builtin_binding), "vm_erts_internal_beamfile_gunzip_1", "erts_internal", "beamfile_gunzip", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_prepare_loading_2", "erts_internal", "prepare_loading", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_beamfile_chunk_2", "erts_internal", "beamfile_chunk", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_beamfile_module_md5_1", "erts_internal", "beamfile_module_md5", 1},
{sizeof(beam_builtin_binding), "vm_os_env_0", "os", "env", 0},
{sizeof(beam_builtin_binding), "vm_alias_1", "beam", "alias", 1},
{sizeof(beam_builtin_binding), "vm_unalias_1", "beam", "unalias", 1},
{sizeof(beam_builtin_binding), "vm_monitor_3", "beam", "monitor", 3},
{sizeof(beam_builtin_binding), "vm_error_3", "beam", "error", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_binary_to_integer_2", "erts_internal", "binary_to_integer", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_list_to_integer_2", "erts_internal", "list_to_integer", 2},
{sizeof(beam_builtin_binding), "vm_min_2", "beam", "min", 2},
{sizeof(beam_builtin_binding), "vm_max_2", "beam", "max", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_term_to_string_2", "erts_internal", "term_to_string", 2},
{sizeof(beam_builtin_binding), "vm_code_coverage_support_0", "code", "coverage_support", 0},
{sizeof(beam_builtin_binding), "vm_code_get_coverage_mode_0", "code", "get_coverage_mode", 0},
{sizeof(beam_builtin_binding), "vm_code_get_coverage_mode_1", "code", "get_coverage_mode", 1},
{sizeof(beam_builtin_binding), "vm_code_get_coverage_2", "code", "get_coverage", 2},
{sizeof(beam_builtin_binding), "vm_code_reset_coverage_1", "code", "reset_coverage", 1},
{sizeof(beam_builtin_binding), "vm_code_set_coverage_mode_1", "code", "set_coverage_mode", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_trace_session_create_3", "beam_trace", "trace_session_create", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_trace_session_destroy_1", "beam_trace", "trace_session_destroy", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_trace_4", "beam_trace", "trace", 4},
{sizeof(beam_builtin_binding), "vm_erts_internal_trace_pattern_4", "beam_trace", "trace_pattern", 4},
{sizeof(beam_builtin_binding), "vm_erts_internal_trace_info_3", "beam_trace", "trace_info", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_system_monitor_1", "erts_internal", "system_monitor", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_system_monitor_3", "erts_internal", "system_monitor", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_processes_next_1", "erts_internal", "processes_next", 1},
{sizeof(beam_builtin_binding), "vm_code_get_debug_info_1", "code", "get_debug_info", 1},
{sizeof(beam_builtin_binding), "vm_exit_3", "beam", "exit", 3},
{sizeof(beam_builtin_binding), "vm_link_2", "beam", "link", 2},
{sizeof(beam_builtin_binding), "vm_erl_debugger_supported_0", "beam_debugger", "supported", 0},
{sizeof(beam_builtin_binding), "vm_erl_debugger_instrumentations_0", "beam_debugger", "instrumentations", 0},
{sizeof(beam_builtin_binding), "vm_erl_debugger_toggle_instrumentations_1", "beam_debugger", "toggle_instrumentations", 1},
{sizeof(beam_builtin_binding), "vm_erl_debugger_register_1", "beam_debugger", "register", 1},
{sizeof(beam_builtin_binding), "vm_erl_debugger_unregister_2", "beam_debugger", "unregister", 2},
{sizeof(beam_builtin_binding), "vm_erl_debugger_whereis_0", "beam_debugger", "whereis", 0},
{sizeof(beam_builtin_binding), "vm_erl_debugger_breakpoint_3", "beam_debugger", "breakpoint", 3},
{sizeof(beam_builtin_binding), "vm_erl_debugger_breakpoints_1", "beam_debugger", "breakpoints", 1},
{sizeof(beam_builtin_binding), "vm_erl_debugger_breakpoints_3", "beam_debugger", "breakpoints", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_notify_breakpoint_hit_3", "erts_internal", "notify_breakpoint_hit", 3},
{sizeof(beam_builtin_binding), "vm_erl_debugger_stack_frames_2", "beam_debugger", "stack_frames", 2},
{sizeof(beam_builtin_binding), "vm_erl_debugger_peek_stack_frame_slot_4", "beam_debugger", "peek_stack_frame_slot", 4},
{sizeof(beam_builtin_binding), "vm_erl_debugger_xregs_count_1", "beam_debugger", "xregs_count", 1},
{sizeof(beam_builtin_binding), "vm_erl_debugger_peek_xreg_3", "beam_debugger", "peek_xreg", 3},
{sizeof(beam_builtin_binding), "vm_erts_debug_unaligned_bitstring_2", "erts_debug", "unaligned_bitstring", 2},
{sizeof(beam_builtin_binding), "vm_is_integer_3", "beam", "is_integer", 3},
{sizeof(beam_builtin_binding), "vm_exit_signal_3", "beam", "exit_signal", 3},
{sizeof(beam_builtin_binding), "vm_is_record_1", "beam", "is_record", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_dynamic_node_name_0", "erts_internal", "dynamic_node_name", 0},
{sizeof(beam_builtin_binding), "vm_erts_internal_dynamic_node_name_1", "erts_internal", "dynamic_node_name", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_ets_info_binary_1", "erts_internal", "ets_info_binary", 1},

/* Existing instruction-backed callables. */
{sizeof(beam_builtin_binding),"vm_apply_2","beam","apply",2},
{sizeof(beam_builtin_binding),"vm_apply_3","beam","apply",3},
{sizeof(beam_builtin_binding),"vm_yield_0","beam","yield",0},
{sizeof(beam_builtin_binding),"vm_hibernate_0","beam","hibernate",0},
    {sizeof(beam_builtin_binding), "clock_monotonic_0", "beam_time", "monotonic_time", 0},
    {sizeof(beam_builtin_binding), "clock_monotonic_1", "beam_time", "monotonic_time", 1},
    {sizeof(beam_builtin_binding), "clock_system_0", "beam_time", "system_time", 0},
    {sizeof(beam_builtin_binding), "clock_system_1", "beam_time", "system_time", 1},
    {sizeof(beam_builtin_binding), "clock_offset_0", "beam_time", "time_offset", 0},
    {sizeof(beam_builtin_binding), "clock_offset_1", "beam_time", "time_offset", 1},
    {sizeof(beam_builtin_binding), "clock_timestamp_0", "beam_time", "timestamp", 0},
    {sizeof(beam_builtin_binding), "timer_start_3", "beam_time", "start_timer", 3},
    {sizeof(beam_builtin_binding), "timer_start_4", "beam_time", "start_timer", 4},
    {sizeof(beam_builtin_binding), "timer_send_after_3", "beam_time", "send_after", 3},
    {sizeof(beam_builtin_binding), "timer_send_after_4", "beam_time", "send_after", 4},
    {sizeof(beam_builtin_binding), "timer_cancel_1", "beam_time", "cancel_timer", 1},
    {sizeof(beam_builtin_binding), "timer_cancel_2", "beam_time", "cancel_timer", 2},
    {sizeof(beam_builtin_binding), "timer_read_1", "beam_time", "read_timer", 1},
    {sizeof(beam_builtin_binding), "timer_read_2", "beam_time", "read_timer", 2},

    {sizeof(beam_builtin_binding), "atomic_array_new_list_modern_2", "beam_atomics", "new", 2},
    {sizeof(beam_builtin_binding), "atomic_array_get_2", "beam_atomics", "get", 2},
    {sizeof(beam_builtin_binding), "atomic_array_put_3", "beam_atomics", "put", 3},
    {sizeof(beam_builtin_binding), "atomic_array_add_3", "beam_atomics", "add", 3},
    {sizeof(beam_builtin_binding), "atomic_array_add_get_3", "beam_atomics", "add_get", 3},
    {sizeof(beam_builtin_binding), "atomic_array_exchange_3", "beam_atomics", "exchange", 3},
    {sizeof(beam_builtin_binding), "atomic_array_compare_exchange_4", "beam_atomics", "compare_exchange", 4},
    {sizeof(beam_builtin_binding), "atomic_array_info_1", "beam_atomics", "info", 1},
    {sizeof(beam_builtin_binding), "counter_array_new_list_modern_2", "beam_counters", "new", 2},
    {sizeof(beam_builtin_binding), "counter_array_get_modern_2", "beam_counters", "get", 2},
    {sizeof(beam_builtin_binding), "counter_array_put_modern_3", "beam_counters", "put", 3},
    {sizeof(beam_builtin_binding), "counter_array_add_modern_3", "beam_counters", "add", 3},
    {sizeof(beam_builtin_binding), "counter_array_info_modern_1", "beam_counters", "info", 1},
    {sizeof(beam_builtin_binding), "persistent_value_put_2", "beam_persistent_term", "put", 2},
    {sizeof(beam_builtin_binding), "persistent_value_put_new_2", "beam_persistent_term", "put_new", 2},
    {sizeof(beam_builtin_binding), "persistent_value_get_0", "beam_persistent_term", "get", 0},
    {sizeof(beam_builtin_binding), "persistent_value_get_1", "beam_persistent_term", "get", 1},
    {sizeof(beam_builtin_binding), "persistent_value_get_2", "beam_persistent_term", "get", 2},
    {sizeof(beam_builtin_binding), "persistent_value_erase_1", "beam_persistent_term", "erase", 1},
    {sizeof(beam_builtin_binding), "persistent_value_info_0", "beam_persistent_term", "info", 0},
    {sizeof(beam_builtin_binding), "term_table_internal_request_all_0", "beam_ets", "internal_request_all", 0},
    {sizeof(beam_builtin_binding), "term_table_new_list_2", "beam_ets", "new", 2},
    {sizeof(beam_builtin_binding), "term_table_delete_1", "beam_ets", "delete", 1},
    {sizeof(beam_builtin_binding), "term_table_delete_2", "beam_ets", "delete", 2},
    {sizeof(beam_builtin_binding), "term_table_delete_object_2", "beam_ets", "delete_object", 2},
    {sizeof(beam_builtin_binding), "term_table_first_1", "beam_ets", "first", 1},
    {sizeof(beam_builtin_binding), "term_table_first_lookup_1", "beam_ets", "first_lookup", 1},
    {sizeof(beam_builtin_binding), "term_table_is_compiled_ms_1", "beam_ets", "is_compiled_ms", 1},
    {sizeof(beam_builtin_binding), "term_table_lookup_2", "beam_ets", "lookup", 2},
    {sizeof(beam_builtin_binding), "term_table_lookup_element_3", "beam_ets", "lookup_element", 3},
    {sizeof(beam_builtin_binding), "term_table_lookup_element_4", "beam_ets", "lookup_element", 4},
    {sizeof(beam_builtin_binding), "term_table_info_1", "beam_ets", "info", 1},
    {sizeof(beam_builtin_binding), "term_table_info_2", "beam_ets", "info", 2},
    {sizeof(beam_builtin_binding), "term_table_last_1", "beam_ets", "last", 1},
    {sizeof(beam_builtin_binding), "term_table_last_lookup_1", "beam_ets", "last_lookup", 1},
    {sizeof(beam_builtin_binding), "term_table_match_1", "beam_ets", "match", 1},
    {sizeof(beam_builtin_binding), "term_table_match_2", "beam_ets", "match", 2},
    {sizeof(beam_builtin_binding), "term_table_match_3", "beam_ets", "match", 3},
    {sizeof(beam_builtin_binding), "term_table_match_object_1", "beam_ets", "match_object", 1},
    {sizeof(beam_builtin_binding), "term_table_match_object_2", "beam_ets", "match_object", 2},
    {sizeof(beam_builtin_binding), "term_table_match_object_3", "beam_ets", "match_object", 3},
    {sizeof(beam_builtin_binding), "term_table_member_2", "beam_ets", "member", 2},
    {sizeof(beam_builtin_binding), "term_table_next_2", "beam_ets", "next", 2},
    {sizeof(beam_builtin_binding), "term_table_next_lookup_2", "beam_ets", "next_lookup", 2},
    {sizeof(beam_builtin_binding), "term_table_prev_2", "beam_ets", "prev", 2},
    {sizeof(beam_builtin_binding), "term_table_prev_lookup_2", "beam_ets", "prev_lookup", 2},
    {sizeof(beam_builtin_binding), "term_table_insert_2", "beam_ets", "insert", 2},
    {sizeof(beam_builtin_binding), "term_table_insert_new_2", "beam_ets", "insert_new", 2},
    {sizeof(beam_builtin_binding), "term_table_rename_2", "beam_ets", "rename", 2},
    {sizeof(beam_builtin_binding), "term_table_safe_fixtable_2", "beam_ets", "safe_fixtable", 2},
    {sizeof(beam_builtin_binding), "term_table_slot_2", "beam_ets", "slot", 2},
    {sizeof(beam_builtin_binding), "term_table_update_counter_3", "beam_ets", "update_counter", 3},
    {sizeof(beam_builtin_binding), "term_table_select_1", "beam_ets", "select", 1},
    {sizeof(beam_builtin_binding), "term_table_select_2", "beam_ets", "select", 2},
    {sizeof(beam_builtin_binding), "term_table_select_3", "beam_ets", "select", 3},
    {sizeof(beam_builtin_binding), "term_table_select_count_2", "beam_ets", "select_count", 2},
    {sizeof(beam_builtin_binding), "term_table_select_reverse_1", "beam_ets", "select_reverse", 1},
    {sizeof(beam_builtin_binding), "term_table_select_reverse_2", "beam_ets", "select_reverse", 2},
    {sizeof(beam_builtin_binding), "term_table_select_reverse_3", "beam_ets", "select_reverse", 3},
    {sizeof(beam_builtin_binding), "term_table_select_replace_2", "beam_ets", "select_replace", 2},
    {sizeof(beam_builtin_binding), "term_table_match_spec_compile_1", "beam_ets", "match_spec_compile", 1},
    {sizeof(beam_builtin_binding), "term_table_match_spec_run_r_3", "beam_ets", "match_spec_run_r", 3},
    {sizeof(beam_builtin_binding), "term_table_update_element_3", "beam_ets", "update_element", 3},
    {sizeof(beam_builtin_binding), "term_table_give_away_3", "beam_ets", "give_away", 3},
    {sizeof(beam_builtin_binding), "term_table_setopts_2", "beam_ets", "setopts", 2},
    {sizeof(beam_builtin_binding), "term_table_take_2", "beam_ets", "take", 2},
    {sizeof(beam_builtin_binding), "term_table_update_counter_4", "beam_ets", "update_counter", 4},
    {sizeof(beam_builtin_binding), "term_table_whereis_1", "beam_ets", "whereis", 1},
    {sizeof(beam_builtin_binding), "term_table_internal_delete_all_2", "beam_ets", "internal_delete_all", 2},
    {sizeof(beam_builtin_binding), "term_table_internal_select_delete_2", "beam_ets", "internal_select_delete", 2},
    {sizeof(beam_builtin_binding), "term_table_update_element_4", "beam_ets", "update_element", 4},
{sizeof(beam_builtin_binding), "vm_records_get_module_1", "beam_records", "get_module", 1},
{sizeof(beam_builtin_binding), "vm_records_get_name_1", "beam_records", "get_name", 1},
{sizeof(beam_builtin_binding), "vm_records_get_2", "beam_records", "get", 2},
{sizeof(beam_builtin_binding), "vm_records_get_field_names_1", "beam_records", "get_field_names", 1},
{sizeof(beam_builtin_binding), "vm_records_is_exported_1", "beam_records", "is_exported", 1},
{sizeof(beam_builtin_binding), "vm_records_create_4", "beam_records", "create", 4},
{sizeof(beam_builtin_binding), "vm_records_update_4", "beam_records", "update", 4},
{sizeof(beam_builtin_binding), "vm_records_get_definition_2", "beam_records", "get_definition", 2},
{sizeof(beam_builtin_binding), "bytes_compile_pattern_1", "beam_binary", "compile_pattern", 1},
{sizeof(beam_builtin_binding), "bytes_match_list_2", "beam_binary", "match", 2},
{sizeof(beam_builtin_binding), "bytes_match_list_3", "beam_binary", "match", 3},
{sizeof(beam_builtin_binding), "bytes_find_all_2", "beam_binary", "matches", 2},
{sizeof(beam_builtin_binding), "bytes_matches_list_3", "beam_binary", "matches", 3},
{sizeof(beam_builtin_binding), "bytes_common_prefix_size_1", "beam_binary", "longest_common_prefix", 1},
{sizeof(beam_builtin_binding), "bytes_common_suffix_size_1", "beam_binary", "longest_common_suffix", 1},
{sizeof(beam_builtin_binding), "bytes_first_1", "beam_binary", "first", 1},
{sizeof(beam_builtin_binding), "bytes_last_1", "beam_binary", "last", 1},
{sizeof(beam_builtin_binding), "bytes_at_2", "beam_binary", "at", 2},
{sizeof(beam_builtin_binding), "bytes_part_2", "beam_binary", "part", 2},
{sizeof(beam_builtin_binding), "bytes_part_3", "beam_binary", "part", 3},
{sizeof(beam_builtin_binding), "bytes_from_list_1", "beam_binary", "list_to_bin", 1},
{sizeof(beam_builtin_binding), "bytes_copy_1", "beam_binary", "copy", 1},
{sizeof(beam_builtin_binding), "bytes_copy_2", "beam_binary", "copy", 2},
{sizeof(beam_builtin_binding), "bitstring_referenced_size_1", "beam_binary", "referenced_byte_size", 1},
{sizeof(beam_builtin_binding), "bytes_encode_unsigned_1", "beam_binary", "encode_unsigned", 1},
{sizeof(beam_builtin_binding), "bytes_encode_unsigned_2", "beam_binary", "encode_unsigned", 2},
{sizeof(beam_builtin_binding), "bytes_decode_unsigned_1", "beam_binary", "decode_unsigned", 1},
{sizeof(beam_builtin_binding), "bytes_decode_unsigned_2", "beam_binary", "decode_unsigned", 2},
{sizeof(beam_builtin_binding), "bytes_split_2", "beam_binary", "split", 2},
{sizeof(beam_builtin_binding), "bytes_split_list_3", "beam_binary", "split", 3},
{sizeof(beam_builtin_binding), "list_member_2", "beam_lists", "member", 2},
{sizeof(beam_builtin_binding), "list_reverse_2", "beam_lists", "reverse", 2},
{sizeof(beam_builtin_binding), "list_keymember_3", "beam_lists", "keymember", 3},
{sizeof(beam_builtin_binding), "list_keysearch_3", "beam_lists", "keysearch", 3},
{sizeof(beam_builtin_binding), "list_keyfind_3", "beam_lists", "keyfind", 3},
{sizeof(beam_builtin_binding), "map_find_2", "beam_maps", "find", 2},
{sizeof(beam_builtin_binding), "map_get_2", "beam_maps", "get", 2},
{sizeof(beam_builtin_binding), "map_from_list_1", "beam_maps", "from_list", 1},
{sizeof(beam_builtin_binding), "map_is_key_2", "beam_maps", "is_key", 2},
{sizeof(beam_builtin_binding), "map_keys_1", "beam_maps", "keys", 1},
{sizeof(beam_builtin_binding), "map_merge_2", "beam_maps", "merge", 2},
{sizeof(beam_builtin_binding), "map_put_3", "beam_maps", "put", 3},
{sizeof(beam_builtin_binding), "map_remove_2", "beam_maps", "remove", 2},
{sizeof(beam_builtin_binding), "map_update_3", "beam_maps", "update", 3},
{sizeof(beam_builtin_binding), "map_values_1", "beam_maps", "values", 1},
{sizeof(beam_builtin_binding), "map_take_2", "beam_maps", "take", 2},
{sizeof(beam_builtin_binding), "map_from_keys_2", "beam_maps", "from_keys", 2},
{sizeof(beam_builtin_binding), "charlist_parse_float_prefix_1", "beam_string", "list_to_float", 1},
{sizeof(beam_builtin_binding), "chardata_to_utf8_2", "beam_unicode", "characters_to_binary", 2},
{sizeof(beam_builtin_binding), "chardata_to_codepoints_2", "beam_unicode", "characters_to_list", 2},
{sizeof(beam_builtin_binding), "bytes_is_7bit_1", "beam_unicode", "bin_is_7bit", 1},
};
static const beam_builtin_binding otp_bindings[] = {
{sizeof(beam_builtin_binding), "vm_abs_1", "erlang", "abs", 1},
{sizeof(beam_builtin_binding), "vm_adler32_1", "erlang", "adler32", 1},
{sizeof(beam_builtin_binding), "vm_adler32_2", "erlang", "adler32", 2},
{sizeof(beam_builtin_binding), "vm_adler32_combine_3", "erlang", "adler32_combine", 3},
{sizeof(beam_builtin_binding), "vm_atom_to_list_1", "erlang", "atom_to_list", 1},
{sizeof(beam_builtin_binding), "vm_binary_to_list_1", "erlang", "binary_to_list", 1},
{sizeof(beam_builtin_binding), "vm_binary_to_list_3", "erlang", "binary_to_list", 3},
{sizeof(beam_builtin_binding), "vm_binary_to_term_1", "erlang", "binary_to_term", 1},
{sizeof(beam_builtin_binding), "vm_crc32_1", "erlang", "crc32", 1},
{sizeof(beam_builtin_binding), "vm_crc32_2", "erlang", "crc32", 2},
{sizeof(beam_builtin_binding), "vm_crc32_combine_3", "erlang", "crc32_combine", 3},
{sizeof(beam_builtin_binding), "vm_date_0", "erlang", "date", 0},
{sizeof(beam_builtin_binding), "vm_delete_module_1", "erlang", "delete_module", 1},
{sizeof(beam_builtin_binding), "vm_display_1", "erlang", "display", 1},
{sizeof(beam_builtin_binding), "vm_display_string_2", "erlang", "display_string", 2},
{sizeof(beam_builtin_binding), "vm_element_2", "erlang", "element", 2},
{sizeof(beam_builtin_binding), "vm_erase_0", "erlang", "erase", 0},
{sizeof(beam_builtin_binding), "vm_erase_1", "erlang", "erase", 1},
{sizeof(beam_builtin_binding), "vm_exit_1", "erlang", "exit", 1},
{sizeof(beam_builtin_binding), "vm_exit_2", "erlang", "exit", 2},
{sizeof(beam_builtin_binding), "vm_exit_signal_2", "erlang", "exit_signal", 2},
{sizeof(beam_builtin_binding), "vm_external_size_1", "erlang", "external_size", 1},
{sizeof(beam_builtin_binding), "vm_external_size_2", "erlang", "external_size", 2},
{sizeof(beam_builtin_binding), "vm_float_1", "erlang", "float", 1},
{sizeof(beam_builtin_binding), "vm_float_to_list_1", "erlang", "float_to_list", 1},
{sizeof(beam_builtin_binding), "vm_float_to_list_2", "erlang", "float_to_list", 2},
{sizeof(beam_builtin_binding), "vm_fun_info_2", "erlang", "fun_info", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_garbage_collect_1", "erts_internal", "garbage_collect", 1},
{sizeof(beam_builtin_binding), "vm_get_0", "erlang", "get", 0},
{sizeof(beam_builtin_binding), "vm_get_1", "erlang", "get", 1},
{sizeof(beam_builtin_binding), "vm_get_keys_1", "erlang", "get_keys", 1},
{sizeof(beam_builtin_binding), "vm_group_leader_0", "erlang", "group_leader", 0},
{sizeof(beam_builtin_binding), "vm_erts_internal_group_leader_2", "erts_internal", "group_leader", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_group_leader_3", "erts_internal", "group_leader", 3},
{sizeof(beam_builtin_binding), "vm_halt_2", "erlang", "halt", 2},
{sizeof(beam_builtin_binding), "vm_phash_2", "erlang", "phash", 2},
{sizeof(beam_builtin_binding), "vm_phash2_1", "erlang", "phash2", 1},
{sizeof(beam_builtin_binding), "vm_phash2_2", "erlang", "phash2", 2},
{sizeof(beam_builtin_binding), "vm_hd_1", "erlang", "hd", 1},
{sizeof(beam_builtin_binding), "vm_integer_to_list_1", "erlang", "integer_to_list", 1},
{sizeof(beam_builtin_binding), "vm_length_1", "erlang", "length", 1},
{sizeof(beam_builtin_binding), "vm_link_1", "erlang", "link", 1},
{sizeof(beam_builtin_binding), "vm_list_to_atom_1", "erlang", "list_to_atom", 1},
{sizeof(beam_builtin_binding), "vm_list_to_binary_1", "erlang", "list_to_binary", 1},
{sizeof(beam_builtin_binding), "vm_list_to_float_1", "erlang", "list_to_float", 1},
{sizeof(beam_builtin_binding), "vm_list_to_pid_1", "erlang", "list_to_pid", 1},
{sizeof(beam_builtin_binding), "vm_list_to_port_1", "erlang", "list_to_port", 1},
{sizeof(beam_builtin_binding), "vm_list_to_ref_1", "erlang", "list_to_ref", 1},
{sizeof(beam_builtin_binding), "vm_list_to_tuple_1", "erlang", "list_to_tuple", 1},
{sizeof(beam_builtin_binding), "vm_loaded_0", "erlang", "loaded", 0},
{sizeof(beam_builtin_binding), "vm_localtime_0", "erlang", "localtime", 0},
{sizeof(beam_builtin_binding), "vm_localtime_to_universaltime_2", "erlang", "localtime_to_universaltime", 2},
{sizeof(beam_builtin_binding), "vm_make_ref_0", "erlang", "make_ref", 0},
{sizeof(beam_builtin_binding), "vm_unique_integer_0", "erlang", "unique_integer", 0},
{sizeof(beam_builtin_binding), "vm_unique_integer_1", "erlang", "unique_integer", 1},
{sizeof(beam_builtin_binding), "vm_md5_1", "erlang", "md5", 1},
{sizeof(beam_builtin_binding), "vm_md5_init_0", "erlang", "md5_init", 0},
{sizeof(beam_builtin_binding), "vm_md5_update_2", "erlang", "md5_update", 2},
{sizeof(beam_builtin_binding), "vm_md5_final_1", "erlang", "md5_final", 1},
{sizeof(beam_builtin_binding), "vm_module_loaded_1", "erlang", "module_loaded", 1},
{sizeof(beam_builtin_binding), "vm_function_exported_3", "erlang", "function_exported", 3},
{sizeof(beam_builtin_binding), "vm_monitor_node_2", "erlang", "monitor_node", 2},
{sizeof(beam_builtin_binding), "vm_monitor_node_3", "erlang", "monitor_node", 3},
{sizeof(beam_builtin_binding), "vm_node_1", "erlang", "node", 1},
{sizeof(beam_builtin_binding), "vm_node_0", "erlang", "node", 0},
{sizeof(beam_builtin_binding), "vm_nodes_0", "erlang", "nodes", 0},
{sizeof(beam_builtin_binding), "vm_nodes_1", "erlang", "nodes", 1},
{sizeof(beam_builtin_binding), "vm_nodes_2", "erlang", "nodes", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_open_port_2", "erts_internal", "open_port", 2},
{sizeof(beam_builtin_binding), "vm_pid_to_list_1", "erlang", "pid_to_list", 1},
{sizeof(beam_builtin_binding), "vm_ports_0", "erlang", "ports", 0},
{sizeof(beam_builtin_binding), "vm_pre_loaded_0", "erlang", "pre_loaded", 0},
{sizeof(beam_builtin_binding), "vm_process_flag_2", "erlang", "process_flag", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_process_flag_3", "erts_internal", "process_flag", 3},
{sizeof(beam_builtin_binding), "vm_process_info_1", "erlang", "process_info", 1},
{sizeof(beam_builtin_binding), "vm_process_info_2", "erlang", "process_info", 2},
{sizeof(beam_builtin_binding), "vm_processes_0", "erlang", "processes", 0},
{sizeof(beam_builtin_binding), "vm_put_2", "erlang", "put", 2},
{sizeof(beam_builtin_binding), "vm_register_2", "erlang", "register", 2},
{sizeof(beam_builtin_binding), "vm_registered_0", "erlang", "registered", 0},
{sizeof(beam_builtin_binding), "vm_round_1", "erlang", "round", 1},
{sizeof(beam_builtin_binding), "vm_self_0", "erlang", "self", 0},
{sizeof(beam_builtin_binding), "vm_setelement_3", "erlang", "setelement", 3},
{sizeof(beam_builtin_binding), "vm_size_1", "erlang", "size", 1},
{sizeof(beam_builtin_binding), "vm_spawn_3", "erlang", "spawn", 3},
{sizeof(beam_builtin_binding), "vm_spawn_link_3", "erlang", "spawn_link", 3},
{sizeof(beam_builtin_binding), "vm_split_binary_2", "erlang", "split_binary", 2},
{sizeof(beam_builtin_binding), "vm_statistics_1", "erlang", "statistics", 1},
{sizeof(beam_builtin_binding), "vm_term_to_binary_1", "erlang", "term_to_binary", 1},
{sizeof(beam_builtin_binding), "vm_term_to_binary_2", "erlang", "term_to_binary", 2},
{sizeof(beam_builtin_binding), "vm_term_to_iovec_1", "erlang", "term_to_iovec", 1},
{sizeof(beam_builtin_binding), "vm_term_to_iovec_2", "erlang", "term_to_iovec", 2},
{sizeof(beam_builtin_binding), "vm_throw_1", "erlang", "throw", 1},
{sizeof(beam_builtin_binding), "vm_time_0", "erlang", "time", 0},
{sizeof(beam_builtin_binding), "vm_tl_1", "erlang", "tl", 1},
{sizeof(beam_builtin_binding), "vm_trunc_1", "erlang", "trunc", 1},
{sizeof(beam_builtin_binding), "vm_tuple_to_list_1", "erlang", "tuple_to_list", 1},
{sizeof(beam_builtin_binding), "vm_universaltime_0", "erlang", "universaltime", 0},
{sizeof(beam_builtin_binding), "vm_universaltime_to_localtime_1", "erlang", "universaltime_to_localtime", 1},
{sizeof(beam_builtin_binding), "vm_unlink_1", "erlang", "unlink", 1},
{sizeof(beam_builtin_binding), "vm_unregister_1", "erlang", "unregister", 1},
{sizeof(beam_builtin_binding), "vm_whereis_1", "erlang", "whereis", 1},
{sizeof(beam_builtin_binding), "vm_spawn_opt_4", "erlang", "spawn_opt", 4},
{sizeof(beam_builtin_binding), "vm_setnode_2", "erlang", "setnode", 2},
{sizeof(beam_builtin_binding), "vm_dist_get_stat_1", "erlang", "dist_get_stat", 1},
{sizeof(beam_builtin_binding), "vm_dist_ctrl_input_handler_2", "erlang", "dist_ctrl_input_handler", 2},
{sizeof(beam_builtin_binding), "vm_dist_ctrl_put_data_2", "erlang", "dist_ctrl_put_data", 2},
{sizeof(beam_builtin_binding), "vm_dist_ctrl_get_data_1", "erlang", "dist_ctrl_get_data", 1},
{sizeof(beam_builtin_binding), "vm_dist_ctrl_get_data_notification_1", "erlang", "dist_ctrl_get_data_notification", 1},
{sizeof(beam_builtin_binding), "vm_dist_ctrl_get_opt_2", "erlang", "dist_ctrl_get_opt", 2},
{sizeof(beam_builtin_binding), "vm_dist_ctrl_set_opt_3", "erlang", "dist_ctrl_set_opt", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_port_info_1", "erts_internal", "port_info", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_port_info_2", "erts_internal", "port_info", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_port_call_3", "erts_internal", "port_call", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_port_command_3", "erts_internal", "port_command", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_port_control_3", "erts_internal", "port_control", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_port_close_1", "erts_internal", "port_close", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_port_connect_2", "erts_internal", "port_connect", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_request_system_task_3", "erts_internal", "request_system_task", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_request_system_task_4", "erts_internal", "request_system_task", 4},
{sizeof(beam_builtin_binding), "vm_erts_internal_check_process_code_1", "erts_internal", "check_process_code", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_map_to_tuple_keys_1", "erts_internal", "map_to_tuple_keys", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_term_type_1", "erts_internal", "term_type", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_map_hashmap_children_1", "erts_internal", "map_hashmap_children", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_time_unit_0", "erts_internal", "time_unit", 0},
{sizeof(beam_builtin_binding), "vm_erts_internal_perf_counter_unit_0", "erts_internal", "perf_counter_unit", 0},
{sizeof(beam_builtin_binding), "vm_erts_internal_is_system_process_1", "erts_internal", "is_system_process", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_system_check_1", "erts_internal", "system_check", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_host_request_1", "erts_internal", "host_request", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_request_failure_2", "erts_internal", "request_failure", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_vm_context_0", "erts_internal", "vm_context", 0},
{sizeof(beam_builtin_binding), "vm_erts_internal_scheduler_wall_time_1", "erts_internal", "scheduler_wall_time", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_dirty_process_handle_signals_1", "erts_internal", "dirty_process_handle_signals", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_create_dist_channel_3", "erts_internal", "create_dist_channel", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_ets_super_user_1", "erts_internal", "ets_super_user", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_spawn_request_4", "erts_internal", "spawn_request", 4},
{sizeof(beam_builtin_binding), "vm_erts_internal_dist_spawn_request_4", "erts_internal", "dist_spawn_request", 4},
{sizeof(beam_builtin_binding), "vm_erts_internal_no_aux_work_threads_0", "erts_internal", "no_aux_work_threads", 0},
{sizeof(beam_builtin_binding), "vm_spawn_request_abandon_1", "erlang", "spawn_request_abandon", 1},
{sizeof(beam_builtin_binding), "vm_erts_literal_area_collector_release_area_switch_0", "erts_literal_area_collector", "release_area_switch", 0},
{sizeof(beam_builtin_binding), "vm_erts_literal_area_collector_send_copy_request_3", "erts_literal_area_collector", "send_copy_request", 3},
{sizeof(beam_builtin_binding), "vm_port_set_data_2", "erlang", "port_set_data", 2},
{sizeof(beam_builtin_binding), "vm_port_get_data_1", "erlang", "port_get_data", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_trace_pattern_3", "erts_internal", "trace_pattern", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_trace_3", "erts_internal", "trace", 3},
{sizeof(beam_builtin_binding), "vm_trace_info_2", "erlang", "trace_info", 2},
{sizeof(beam_builtin_binding), "vm_trace_delivered_1", "erlang", "trace_delivered", 1},
{sizeof(beam_builtin_binding), "vm_seq_trace_2", "erlang", "seq_trace", 2},
{sizeof(beam_builtin_binding), "vm_seq_trace_info_1", "erlang", "seq_trace_info", 1},
{sizeof(beam_builtin_binding), "vm_seq_trace_print_1", "erlang", "seq_trace_print", 1},
{sizeof(beam_builtin_binding), "vm_seq_trace_print_2", "erlang", "seq_trace_print", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_suspend_process_2", "erts_internal", "suspend_process", 2},
{sizeof(beam_builtin_binding), "vm_resume_process_1", "erlang", "resume_process", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_process_display_2", "erts_internal", "process_display", 2},
{sizeof(beam_builtin_binding), "vm_bump_reductions_1", "erlang", "bump_reductions", 1},
{sizeof(beam_builtin_binding), "vm_math_cos_1", "math", "cos", 1},
{sizeof(beam_builtin_binding), "vm_math_cosh_1", "math", "cosh", 1},
{sizeof(beam_builtin_binding), "vm_math_sin_1", "math", "sin", 1},
{sizeof(beam_builtin_binding), "vm_math_sinh_1", "math", "sinh", 1},
{sizeof(beam_builtin_binding), "vm_math_tan_1", "math", "tan", 1},
{sizeof(beam_builtin_binding), "vm_math_tanh_1", "math", "tanh", 1},
{sizeof(beam_builtin_binding), "vm_math_acos_1", "math", "acos", 1},
{sizeof(beam_builtin_binding), "vm_math_acosh_1", "math", "acosh", 1},
{sizeof(beam_builtin_binding), "vm_math_asin_1", "math", "asin", 1},
{sizeof(beam_builtin_binding), "vm_math_asinh_1", "math", "asinh", 1},
{sizeof(beam_builtin_binding), "vm_math_atan_1", "math", "atan", 1},
{sizeof(beam_builtin_binding), "vm_math_atanh_1", "math", "atanh", 1},
{sizeof(beam_builtin_binding), "vm_math_erf_1", "math", "erf", 1},
{sizeof(beam_builtin_binding), "vm_math_erfc_1", "math", "erfc", 1},
{sizeof(beam_builtin_binding), "vm_math_exp_1", "math", "exp", 1},
{sizeof(beam_builtin_binding), "vm_math_log_1", "math", "log", 1},
{sizeof(beam_builtin_binding), "vm_math_log2_1", "math", "log2", 1},
{sizeof(beam_builtin_binding), "vm_math_log10_1", "math", "log10", 1},
{sizeof(beam_builtin_binding), "vm_math_sqrt_1", "math", "sqrt", 1},
{sizeof(beam_builtin_binding), "vm_math_atan2_2", "math", "atan2", 2},
{sizeof(beam_builtin_binding), "vm_math_pow_2", "math", "pow", 2},
{sizeof(beam_builtin_binding), "vm_make_tuple_2", "erlang", "make_tuple", 2},
{sizeof(beam_builtin_binding), "vm_append_element_2", "erlang", "append_element", 2},
{sizeof(beam_builtin_binding), "vm_make_tuple_3", "erlang", "make_tuple", 3},
{sizeof(beam_builtin_binding), "vm_system_flag_2", "erlang", "system_flag", 2},
{sizeof(beam_builtin_binding), "vm_system_info_1", "erlang", "system_info", 1},
{sizeof(beam_builtin_binding), "vm_system_profile_2", "erlang", "system_profile", 2},
{sizeof(beam_builtin_binding), "vm_system_profile_0", "erlang", "system_profile", 0},
{sizeof(beam_builtin_binding), "vm_ref_to_list_1", "erlang", "ref_to_list", 1},
{sizeof(beam_builtin_binding), "vm_port_to_list_1", "erlang", "port_to_list", 1},
{sizeof(beam_builtin_binding), "vm_fun_to_list_1", "erlang", "fun_to_list", 1},
{sizeof(beam_builtin_binding), "vm_monitor_2", "erlang", "monitor", 2},
{sizeof(beam_builtin_binding), "vm_demonitor_1", "erlang", "demonitor", 1},
{sizeof(beam_builtin_binding), "vm_demonitor_2", "erlang", "demonitor", 2},
{sizeof(beam_builtin_binding), "vm_is_process_alive_1", "erlang", "is_process_alive", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_is_process_alive_2", "erts_internal", "is_process_alive", 2},
{sizeof(beam_builtin_binding), "vm_error_1", "erlang", "error", 1},
{sizeof(beam_builtin_binding), "vm_error_2", "erlang", "error", 2},
{sizeof(beam_builtin_binding), "vm_raise_3", "erlang", "raise", 3},
{sizeof(beam_builtin_binding), "vm_is_builtin_3", "erlang", "is_builtin", 3},
{sizeof(beam_builtin_binding), "vm_and_2", "erlang", "and", 2},
{sizeof(beam_builtin_binding), "vm_or_2", "erlang", "or", 2},
{sizeof(beam_builtin_binding), "vm_xor_2", "erlang", "xor", 2},
{sizeof(beam_builtin_binding), "vm_not_1", "erlang", "not", 1},
{sizeof(beam_builtin_binding), "vm_sgt_2", "erlang", ">", 2},
{sizeof(beam_builtin_binding), "vm_sge_2", "erlang", ">=", 2},
{sizeof(beam_builtin_binding), "vm_slt_2", "erlang", "<", 2},
{sizeof(beam_builtin_binding), "vm_sle_2", "erlang", "=<", 2},
{sizeof(beam_builtin_binding), "vm_seq_2", "erlang", "=:=", 2},
{sizeof(beam_builtin_binding), "vm_seqeq_2", "erlang", "==", 2},
{sizeof(beam_builtin_binding), "vm_sneq_2", "erlang", "=/=", 2},
{sizeof(beam_builtin_binding), "vm_sneqeq_2", "erlang", "/=", 2},
{sizeof(beam_builtin_binding), "vm_splus_2", "erlang", "+", 2},
{sizeof(beam_builtin_binding), "vm_sminus_2", "erlang", "-", 2},
{sizeof(beam_builtin_binding), "vm_stimes_2", "erlang", "*", 2},
{sizeof(beam_builtin_binding), "vm_div_2", "erlang", "/", 2},
{sizeof(beam_builtin_binding), "vm_intdiv_2", "erlang", "div", 2},
{sizeof(beam_builtin_binding), "vm_rem_2", "erlang", "rem", 2},
{sizeof(beam_builtin_binding), "vm_bor_2", "erlang", "bor", 2},
{sizeof(beam_builtin_binding), "vm_band_2", "erlang", "band", 2},
{sizeof(beam_builtin_binding), "vm_bxor_2", "erlang", "bxor", 2},
{sizeof(beam_builtin_binding), "vm_bsl_2", "erlang", "bsl", 2},
{sizeof(beam_builtin_binding), "vm_bsr_2", "erlang", "bsr", 2},
{sizeof(beam_builtin_binding), "vm_bnot_1", "erlang", "bnot", 1},
{sizeof(beam_builtin_binding), "vm_sminus_1", "erlang", "-", 1},
{sizeof(beam_builtin_binding), "vm_splus_1", "erlang", "+", 1},
{sizeof(beam_builtin_binding), "vm_ebif_bang_2", "erlang", "!", 2},
{sizeof(beam_builtin_binding), "vm_send_2", "erlang", "send", 2},
{sizeof(beam_builtin_binding), "vm_send_3", "erlang", "send", 3},
{sizeof(beam_builtin_binding), "vm_ebif_plusplus_2", "erlang", "++", 2},
{sizeof(beam_builtin_binding), "vm_append_2", "erlang", "append", 2},
{sizeof(beam_builtin_binding), "vm_ebif_minusminus_2", "erlang", "--", 2},
{sizeof(beam_builtin_binding), "vm_subtract_2", "erlang", "subtract", 2},
{sizeof(beam_builtin_binding), "vm_is_atom_1", "erlang", "is_atom", 1},
{sizeof(beam_builtin_binding), "vm_is_list_1", "erlang", "is_list", 1},
{sizeof(beam_builtin_binding), "vm_is_tuple_1", "erlang", "is_tuple", 1},
{sizeof(beam_builtin_binding), "vm_is_float_1", "erlang", "is_float", 1},
{sizeof(beam_builtin_binding), "vm_is_integer_1", "erlang", "is_integer", 1},
{sizeof(beam_builtin_binding), "vm_is_number_1", "erlang", "is_number", 1},
{sizeof(beam_builtin_binding), "vm_is_pid_1", "erlang", "is_pid", 1},
{sizeof(beam_builtin_binding), "vm_is_port_1", "erlang", "is_port", 1},
{sizeof(beam_builtin_binding), "vm_is_reference_1", "erlang", "is_reference", 1},
{sizeof(beam_builtin_binding), "vm_is_binary_1", "erlang", "is_binary", 1},
{sizeof(beam_builtin_binding), "vm_is_function_1", "erlang", "is_function", 1},
{sizeof(beam_builtin_binding), "vm_is_function_2", "erlang", "is_function", 2},
{sizeof(beam_builtin_binding), "vm_is_record_2", "erlang", "is_record", 2},
{sizeof(beam_builtin_binding), "vm_is_record_3", "erlang", "is_record", 3},
{sizeof(beam_builtin_binding), "vm_match_spec_test_3", "erlang", "match_spec_test", 3},
{sizeof(beam_builtin_binding), "vm_os_getenv_1", "os", "getenv", 1},
{sizeof(beam_builtin_binding), "vm_os_putenv_2", "os", "putenv", 2},
{sizeof(beam_builtin_binding), "vm_os_unsetenv_1", "os", "unsetenv", 1},
{sizeof(beam_builtin_binding), "vm_os_getpid_0", "os", "getpid", 0},
{sizeof(beam_builtin_binding), "vm_os_timestamp_0", "os", "timestamp", 0},
{sizeof(beam_builtin_binding), "vm_os_system_time_0", "os", "system_time", 0},
{sizeof(beam_builtin_binding), "vm_os_system_time_1", "os", "system_time", 1},
{sizeof(beam_builtin_binding), "vm_os_perf_counter_0", "os", "perf_counter", 0},
{sizeof(beam_builtin_binding), "vm_erl_ddll_try_load_3", "erl_ddll", "try_load", 3},
{sizeof(beam_builtin_binding), "vm_erl_ddll_try_unload_2", "erl_ddll", "try_unload", 2},
{sizeof(beam_builtin_binding), "vm_erl_ddll_loaded_drivers_0", "erl_ddll", "loaded_drivers", 0},
{sizeof(beam_builtin_binding), "vm_erl_ddll_info_2", "erl_ddll", "info", 2},
{sizeof(beam_builtin_binding), "vm_erl_ddll_format_error_int_1", "erl_ddll", "format_error_int", 1},
{sizeof(beam_builtin_binding), "vm_erl_ddll_monitor_2", "erl_ddll", "monitor", 2},
{sizeof(beam_builtin_binding), "vm_erl_ddll_demonitor_1", "erl_ddll", "demonitor", 1},
{sizeof(beam_builtin_binding), "vm_erts_debug_disassemble_1", "erts_debug", "disassemble", 1},
{sizeof(beam_builtin_binding), "vm_erts_debug_breakpoint_2", "erts_debug", "breakpoint", 2},
{sizeof(beam_builtin_binding), "vm_erts_debug_same_2", "erts_debug", "same", 2},
{sizeof(beam_builtin_binding), "vm_erts_debug_flat_size_1", "erts_debug", "flat_size", 1},
{sizeof(beam_builtin_binding), "vm_erts_debug_get_internal_state_1", "erts_debug", "get_internal_state", 1},
{sizeof(beam_builtin_binding), "vm_erts_debug_set_internal_state_2", "erts_debug", "set_internal_state", 2},
{sizeof(beam_builtin_binding), "vm_erts_debug_dist_ext_to_term_2", "erts_debug", "dist_ext_to_term", 2},
{sizeof(beam_builtin_binding), "vm_erts_debug_instructions_0", "erts_debug", "instructions", 0},
{sizeof(beam_builtin_binding), "vm_erts_debug_interpreter_size_0", "erts_debug", "interpreter_size", 0},
{sizeof(beam_builtin_binding), "vm_erts_debug_dirty_cpu_2", "erts_debug", "dirty_cpu", 2},
{sizeof(beam_builtin_binding), "vm_erts_debug_dirty_io_2", "erts_debug", "dirty_io", 2},
{sizeof(beam_builtin_binding), "vm_erts_debug_dirty_3", "erts_debug", "dirty", 3},
{sizeof(beam_builtin_binding), "vm_erts_debug_lcnt_control_2", "erts_debug", "lcnt_control", 2},
{sizeof(beam_builtin_binding), "vm_erts_debug_lcnt_control_1", "erts_debug", "lcnt_control", 1},
{sizeof(beam_builtin_binding), "vm_erts_debug_lcnt_collect_0", "erts_debug", "lcnt_collect", 0},
{sizeof(beam_builtin_binding), "vm_erts_debug_lcnt_clear_0", "erts_debug", "lcnt_clear", 0},
{sizeof(beam_builtin_binding), "vm_hibernate_3", "erlang", "hibernate", 3},
{sizeof(beam_builtin_binding), "vm_error_logger_warning_map_0", "error_logger", "warning_map", 0},
{sizeof(beam_builtin_binding), "vm_get_module_info_1", "erlang", "get_module_info", 1},
{sizeof(beam_builtin_binding), "vm_get_module_info_2", "erlang", "get_module_info", 2},
{sizeof(beam_builtin_binding), "vm_is_boolean_1", "erlang", "is_boolean", 1},
{sizeof(beam_builtin_binding), "vm_make_fun_3", "erlang", "make_fun", 3},
{sizeof(beam_builtin_binding), "vm_iolist_size_1", "erlang", "iolist_size", 1},
{sizeof(beam_builtin_binding), "vm_iolist_to_binary_1", "erlang", "iolist_to_binary", 1},
{sizeof(beam_builtin_binding), "vm_list_to_existing_atom_1", "erlang", "list_to_existing_atom", 1},
{sizeof(beam_builtin_binding), "vm_is_bitstring_1", "erlang", "is_bitstring", 1},
{sizeof(beam_builtin_binding), "vm_tuple_size_1", "erlang", "tuple_size", 1},
{sizeof(beam_builtin_binding), "vm_byte_size_1", "erlang", "byte_size", 1},
{sizeof(beam_builtin_binding), "vm_bit_size_1", "erlang", "bit_size", 1},
{sizeof(beam_builtin_binding), "vm_list_to_bitstring_1", "erlang", "list_to_bitstring", 1},
{sizeof(beam_builtin_binding), "vm_bitstring_to_list_1", "erlang", "bitstring_to_list", 1},
{sizeof(beam_builtin_binding), "vm_decode_packet_3", "erlang", "decode_packet", 3},
{sizeof(beam_builtin_binding), "vm_atom_to_binary_2", "erlang", "atom_to_binary", 2},
{sizeof(beam_builtin_binding), "vm_binary_to_atom_2", "erlang", "binary_to_atom", 2},
{sizeof(beam_builtin_binding), "vm_binary_to_existing_atom_2", "erlang", "binary_to_existing_atom", 2},
{sizeof(beam_builtin_binding), "vm_net_kernel_dflag_unicode_io_1", "net_kernel", "dflag_unicode_io", 1},
{sizeof(beam_builtin_binding), "vm_load_nif_2", "erlang", "load_nif", 2},
{sizeof(beam_builtin_binding), "vm_call_on_load_function_1", "erlang", "call_on_load_function", 1},
{sizeof(beam_builtin_binding), "vm_finish_after_on_load_2", "erlang", "finish_after_on_load", 2},
{sizeof(beam_builtin_binding), "vm_binary_to_term_2", "erlang", "binary_to_term", 2},
{sizeof(beam_builtin_binding), "vm_binary_part_2", "erlang", "binary_part", 2},
{sizeof(beam_builtin_binding), "vm_binary_part_3", "erlang", "binary_part", 3},
{sizeof(beam_builtin_binding), "vm_nif_error_1", "erlang", "nif_error", 1},
{sizeof(beam_builtin_binding), "vm_nif_error_2", "erlang", "nif_error", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_filename_to_native_1", "erts_internal", "filename_to_native", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_filename_from_native_1", "erts_internal", "filename_from_native", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_filename_normalize_utf8_1", "erts_internal", "filename_normalize_utf8", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_filename_is_translatable_1", "erts_internal", "filename_is_translatable", 1},
{sizeof(beam_builtin_binding), "vm_file_native_name_encoding_0", "file", "native_name_encoding", 0},
{sizeof(beam_builtin_binding), "vm_check_old_code_1", "erlang", "check_old_code", 1},
{sizeof(beam_builtin_binding), "vm_universaltime_to_posixtime_1", "erlang", "universaltime_to_posixtime", 1},
{sizeof(beam_builtin_binding), "vm_posixtime_to_universaltime_1", "erlang", "posixtime_to_universaltime", 1},
{sizeof(beam_builtin_binding), "vm_dt_put_tag_1", "erlang", "dt_put_tag", 1},
{sizeof(beam_builtin_binding), "vm_dt_get_tag_0", "erlang", "dt_get_tag", 0},
{sizeof(beam_builtin_binding), "vm_dt_get_tag_data_0", "erlang", "dt_get_tag_data", 0},
{sizeof(beam_builtin_binding), "vm_dt_spread_tag_1", "erlang", "dt_spread_tag", 1},
{sizeof(beam_builtin_binding), "vm_dt_restore_tag_1", "erlang", "dt_restore_tag", 1},
{sizeof(beam_builtin_binding), "vm_dt_prepend_vm_tag_data_1", "erlang", "dt_prepend_vm_tag_data", 1},
{sizeof(beam_builtin_binding), "vm_dt_append_vm_tag_data_1", "erlang", "dt_append_vm_tag_data", 1},
{sizeof(beam_builtin_binding), "vm_finish_loading_1", "erlang", "finish_loading", 1},
{sizeof(beam_builtin_binding), "vm_insert_element_3", "erlang", "insert_element", 3},
{sizeof(beam_builtin_binding), "vm_delete_element_2", "erlang", "delete_element", 2},
{sizeof(beam_builtin_binding), "vm_integer_to_binary_1", "erlang", "integer_to_binary", 1},
{sizeof(beam_builtin_binding), "vm_float_to_binary_1", "erlang", "float_to_binary", 1},
{sizeof(beam_builtin_binding), "vm_float_to_binary_2", "erlang", "float_to_binary", 2},
{sizeof(beam_builtin_binding), "vm_binary_to_float_1", "erlang", "binary_to_float", 1},
{sizeof(beam_builtin_binding), "vm_io_printable_range_0", "io", "printable_range", 0},
{sizeof(beam_builtin_binding), "vm_is_map_1", "erlang", "is_map", 1},
{sizeof(beam_builtin_binding), "vm_map_size_1", "erlang", "map_size", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_cmp_term_2", "erts_internal", "cmp_term", 2},
{sizeof(beam_builtin_binding), "vm_fun_info_mfa_1", "erlang", "fun_info_mfa", 1},
{sizeof(beam_builtin_binding), "vm_get_keys_0", "erlang", "get_keys", 0},
{sizeof(beam_builtin_binding), "vm_erts_debug_map_info_1", "erts_debug", "map_info", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_is_process_executing_dirty_1", "erts_internal", "is_process_executing_dirty", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_check_dirty_process_code_2", "erts_internal", "check_dirty_process_code", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_purge_module_2", "erts_internal", "purge_module", 2},
{sizeof(beam_builtin_binding), "vm_erts_debug_size_shared_1", "erts_debug", "size_shared", 1},
{sizeof(beam_builtin_binding), "vm_erts_debug_copy_shared_2", "erts_debug", "copy_shared", 2},
{sizeof(beam_builtin_binding), "vm_has_prepared_code_on_load_1", "erlang", "has_prepared_code_on_load", 1},
{sizeof(beam_builtin_binding), "vm_floor_1", "erlang", "floor", 1},
{sizeof(beam_builtin_binding), "vm_ceil_1", "erlang", "ceil", 1},
{sizeof(beam_builtin_binding), "vm_math_floor_1", "math", "floor", 1},
{sizeof(beam_builtin_binding), "vm_math_ceil_1", "math", "ceil", 1},
{sizeof(beam_builtin_binding), "vm_math_fmod_2", "math", "fmod", 2},
{sizeof(beam_builtin_binding), "vm_os_set_signal_2", "os", "set_signal", 2},
{sizeof(beam_builtin_binding), "vm_iolist_to_iovec_1", "erlang", "iolist_to_iovec", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_get_dflags_0", "erts_internal", "get_dflags", 0},
{sizeof(beam_builtin_binding), "vm_erts_internal_new_connection_1", "erts_internal", "new_connection", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_map_next_3", "erts_internal", "map_next", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_gather_alloc_histograms_1", "erts_internal", "gather_alloc_histograms", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_gather_carrier_info_1", "erts_internal", "gather_carrier_info", 1},
{sizeof(beam_builtin_binding), "vm_map_get_2", "erlang", "map_get", 2},
{sizeof(beam_builtin_binding), "vm_is_map_key_2", "erlang", "is_map_key", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_erase_persistent_terms_0", "erts_internal", "erase_persistent_terms", 0},
{sizeof(beam_builtin_binding), "vm_erts_internal_atomics_new_2", "erts_internal", "atomics_new", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_counters_new_1", "erts_internal", "counters_new", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_counters_get_2", "erts_internal", "counters_get", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_counters_add_3", "erts_internal", "counters_add", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_counters_put_3", "erts_internal", "counters_put", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_counters_info_1", "erts_internal", "counters_info", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_spawn_system_process_3", "erts_internal", "spawn_system_process", 3},
{sizeof(beam_builtin_binding), "vm_integer_to_list_2", "erlang", "integer_to_list", 2},
{sizeof(beam_builtin_binding), "vm_integer_to_binary_2", "erlang", "integer_to_binary", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_ets_lookup_binary_info_2", "erts_internal", "ets_lookup_binary_info", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_ets_raw_first_1", "erts_internal", "ets_raw_first", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_ets_raw_next_2", "erts_internal", "ets_raw_next", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_abort_pending_connection_2", "erts_internal", "abort_pending_connection", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_get_creation_0", "erts_internal", "get_creation", 0},
{sizeof(beam_builtin_binding), "vm_erts_internal_beamfile_gunzip_1", "erts_internal", "beamfile_gunzip", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_prepare_loading_2", "erts_internal", "prepare_loading", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_beamfile_chunk_2", "erts_internal", "beamfile_chunk", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_beamfile_module_md5_1", "erts_internal", "beamfile_module_md5", 1},
{sizeof(beam_builtin_binding), "vm_os_env_0", "os", "env", 0},
{sizeof(beam_builtin_binding), "vm_alias_1", "erlang", "alias", 1},
{sizeof(beam_builtin_binding), "vm_unalias_1", "erlang", "unalias", 1},
{sizeof(beam_builtin_binding), "vm_monitor_3", "erlang", "monitor", 3},
{sizeof(beam_builtin_binding), "vm_error_3", "erlang", "error", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_binary_to_integer_2", "erts_internal", "binary_to_integer", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_list_to_integer_2", "erts_internal", "list_to_integer", 2},
{sizeof(beam_builtin_binding), "vm_min_2", "erlang", "min", 2},
{sizeof(beam_builtin_binding), "vm_max_2", "erlang", "max", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_term_to_string_2", "erts_internal", "term_to_string", 2},
{sizeof(beam_builtin_binding), "vm_code_coverage_support_0", "code", "coverage_support", 0},
{sizeof(beam_builtin_binding), "vm_code_get_coverage_mode_0", "code", "get_coverage_mode", 0},
{sizeof(beam_builtin_binding), "vm_code_get_coverage_mode_1", "code", "get_coverage_mode", 1},
{sizeof(beam_builtin_binding), "vm_code_get_coverage_2", "code", "get_coverage", 2},
{sizeof(beam_builtin_binding), "vm_code_reset_coverage_1", "code", "reset_coverage", 1},
{sizeof(beam_builtin_binding), "vm_code_set_coverage_mode_1", "code", "set_coverage_mode", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_trace_session_create_3", "erts_internal", "trace_session_create", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_trace_session_destroy_1", "erts_internal", "trace_session_destroy", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_trace_4", "erts_internal", "trace", 4},
{sizeof(beam_builtin_binding), "vm_erts_internal_trace_pattern_4", "erts_internal", "trace_pattern", 4},
{sizeof(beam_builtin_binding), "vm_erts_internal_trace_info_3", "erts_internal", "trace_info", 3},
{sizeof(beam_builtin_binding), "vm_erts_trace_cleaner_check_0", "erts_trace_cleaner", "check", 0},
{sizeof(beam_builtin_binding), "vm_erts_trace_cleaner_send_trace_clean_signal_1", "erts_trace_cleaner", "send_trace_clean_signal", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_system_monitor_1", "erts_internal", "system_monitor", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_system_monitor_3", "erts_internal", "system_monitor", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_processes_next_1", "erts_internal", "processes_next", 1},
{sizeof(beam_builtin_binding), "vm_code_get_debug_info_1", "code", "get_debug_info", 1},
{sizeof(beam_builtin_binding), "vm_exit_3", "erlang", "exit", 3},
{sizeof(beam_builtin_binding), "vm_link_2", "erlang", "link", 2},
{sizeof(beam_builtin_binding), "vm_erl_debugger_supported_0", "erl_debugger", "supported", 0},
{sizeof(beam_builtin_binding), "vm_erl_debugger_instrumentations_0", "erl_debugger", "instrumentations", 0},
{sizeof(beam_builtin_binding), "vm_erl_debugger_toggle_instrumentations_1", "erl_debugger", "toggle_instrumentations", 1},
{sizeof(beam_builtin_binding), "vm_erl_debugger_register_1", "erl_debugger", "register", 1},
{sizeof(beam_builtin_binding), "vm_erl_debugger_unregister_2", "erl_debugger", "unregister", 2},
{sizeof(beam_builtin_binding), "vm_erl_debugger_whereis_0", "erl_debugger", "whereis", 0},
{sizeof(beam_builtin_binding), "vm_erl_debugger_breakpoint_3", "erl_debugger", "breakpoint", 3},
{sizeof(beam_builtin_binding), "vm_erl_debugger_breakpoints_1", "erl_debugger", "breakpoints", 1},
{sizeof(beam_builtin_binding), "vm_erl_debugger_breakpoints_3", "erl_debugger", "breakpoints", 3},
{sizeof(beam_builtin_binding), "vm_erts_internal_notify_breakpoint_hit_3", "erts_internal", "notify_breakpoint_hit", 3},
{sizeof(beam_builtin_binding), "vm_erl_debugger_stack_frames_2", "erl_debugger", "stack_frames", 2},
{sizeof(beam_builtin_binding), "vm_erl_debugger_peek_stack_frame_slot_4", "erl_debugger", "peek_stack_frame_slot", 4},
{sizeof(beam_builtin_binding), "vm_erl_debugger_xregs_count_1", "erl_debugger", "xregs_count", 1},
{sizeof(beam_builtin_binding), "vm_erl_debugger_peek_xreg_3", "erl_debugger", "peek_xreg", 3},
{sizeof(beam_builtin_binding), "vm_erts_debug_unaligned_bitstring_2", "erts_debug", "unaligned_bitstring", 2},
{sizeof(beam_builtin_binding), "vm_is_integer_3", "erlang", "is_integer", 3},
{sizeof(beam_builtin_binding), "vm_exit_signal_3", "erlang", "exit_signal", 3},
{sizeof(beam_builtin_binding), "vm_is_record_1", "erlang", "is_record", 1},
{sizeof(beam_builtin_binding), "vm_records_get_module_1", "records", "get_module", 1},
{sizeof(beam_builtin_binding), "vm_records_get_name_1", "records", "get_name", 1},
{sizeof(beam_builtin_binding), "vm_records_get_2", "records", "get", 2},
{sizeof(beam_builtin_binding), "vm_records_get_field_names_1", "records", "get_field_names", 1},
{sizeof(beam_builtin_binding), "vm_records_is_exported_1", "records", "is_exported", 1},
{sizeof(beam_builtin_binding), "vm_records_create_4", "records", "create", 4},
{sizeof(beam_builtin_binding), "vm_records_update_4", "records", "update", 4},
{sizeof(beam_builtin_binding), "vm_records_get_definition_2", "records", "get_definition", 2},
{sizeof(beam_builtin_binding), "vm_erts_internal_dynamic_node_name_0", "erts_internal", "dynamic_node_name", 0},
{sizeof(beam_builtin_binding), "vm_erts_internal_dynamic_node_name_1", "erts_internal", "dynamic_node_name", 1},
{sizeof(beam_builtin_binding), "vm_erts_internal_ets_info_binary_1", "erts_internal", "ets_info_binary", 1},
{sizeof(beam_builtin_binding),"vm_apply_2","erlang","apply",2},
{sizeof(beam_builtin_binding),"vm_apply_3","erlang","apply",3},
{sizeof(beam_builtin_binding),"vm_yield_0","erlang","yield",0},
{sizeof(beam_builtin_binding),"vm_hibernate_0","erlang","hibernate",0},
    {sizeof(beam_builtin_binding), "clock_monotonic_0", "erlang", "monotonic_time", 0},
    {sizeof(beam_builtin_binding), "clock_monotonic_1", "erlang", "monotonic_time", 1},
    {sizeof(beam_builtin_binding), "clock_system_0", "erlang", "system_time", 0},
    {sizeof(beam_builtin_binding), "clock_system_1", "erlang", "system_time", 1},
    {sizeof(beam_builtin_binding), "clock_offset_0", "erlang", "time_offset", 0},
    {sizeof(beam_builtin_binding), "clock_offset_1", "erlang", "time_offset", 1},
    {sizeof(beam_builtin_binding), "clock_timestamp_0", "erlang", "timestamp", 0},
    {sizeof(beam_builtin_binding), "timer_start_3", "erlang", "start_timer", 3},
    {sizeof(beam_builtin_binding), "timer_start_4", "erlang", "start_timer", 4},
    {sizeof(beam_builtin_binding), "timer_send_after_3", "erlang", "send_after", 3},
    {sizeof(beam_builtin_binding), "timer_send_after_4", "erlang", "send_after", 4},
    {sizeof(beam_builtin_binding), "timer_cancel_1", "erlang", "cancel_timer", 1},
    {sizeof(beam_builtin_binding), "timer_cancel_2", "erlang", "cancel_timer", 2},
    {sizeof(beam_builtin_binding), "timer_read_1", "erlang", "read_timer", 1},
    {sizeof(beam_builtin_binding), "timer_read_2", "erlang", "read_timer", 2},
    {sizeof(beam_builtin_binding), "clock_legacy_now_0", "erlang", "now", 0},
    {sizeof(beam_builtin_binding), "bytes_compile_pattern_1", "binary", "compile_pattern", 1},
    {sizeof(beam_builtin_binding), "bytes_match_list_2", "binary", "match", 2},
    {sizeof(beam_builtin_binding), "bytes_match_list_3", "binary", "match", 3},
    {sizeof(beam_builtin_binding), "bytes_find_all_2", "binary", "matches", 2},
    {sizeof(beam_builtin_binding), "bytes_matches_list_3", "binary", "matches", 3},
    {sizeof(beam_builtin_binding), "bytes_common_prefix_size_1", "binary", "longest_common_prefix", 1},
    {sizeof(beam_builtin_binding), "bytes_common_suffix_size_1", "binary", "longest_common_suffix", 1},
    {sizeof(beam_builtin_binding), "bytes_first_1", "binary", "first", 1},
    {sizeof(beam_builtin_binding), "bytes_last_1", "binary", "last", 1},
    {sizeof(beam_builtin_binding), "bytes_at_2", "binary", "at", 2},
    {sizeof(beam_builtin_binding), "bytes_part_2", "binary", "part", 2},
    {sizeof(beam_builtin_binding), "bytes_part_3", "binary", "part", 3},
    {sizeof(beam_builtin_binding), "bytes_from_list_1", "binary", "list_to_bin", 1},
    {sizeof(beam_builtin_binding), "bytes_copy_1", "binary", "copy", 1},
    {sizeof(beam_builtin_binding), "bytes_copy_2", "binary", "copy", 2},
    {sizeof(beam_builtin_binding), "bitstring_referenced_size_1", "binary", "referenced_byte_size", 1},
    {sizeof(beam_builtin_binding), "bytes_encode_unsigned_1", "binary", "encode_unsigned", 1},
    {sizeof(beam_builtin_binding), "bytes_encode_unsigned_2", "binary", "encode_unsigned", 2},
    {sizeof(beam_builtin_binding), "bytes_decode_unsigned_1", "binary", "decode_unsigned", 1},
    {sizeof(beam_builtin_binding), "bytes_decode_unsigned_2", "binary", "decode_unsigned", 2},
    {sizeof(beam_builtin_binding), "bytes_split_2", "binary", "split", 2},
    {sizeof(beam_builtin_binding), "bytes_split_list_3", "binary", "split", 3},
    {sizeof(beam_builtin_binding), "list_member_2", "lists", "member", 2},
    {sizeof(beam_builtin_binding), "list_reverse_2", "lists", "reverse", 2},
    {sizeof(beam_builtin_binding), "list_keymember_3", "lists", "keymember", 3},
    {sizeof(beam_builtin_binding), "list_keysearch_3", "lists", "keysearch", 3},
    {sizeof(beam_builtin_binding), "list_keyfind_3", "lists", "keyfind", 3},
    {sizeof(beam_builtin_binding), "map_find_2", "maps", "find", 2},
    {sizeof(beam_builtin_binding), "map_get_2", "maps", "get", 2},
    {sizeof(beam_builtin_binding), "map_from_list_1", "maps", "from_list", 1},
    {sizeof(beam_builtin_binding), "map_is_key_2", "maps", "is_key", 2},
    {sizeof(beam_builtin_binding), "map_keys_1", "maps", "keys", 1},
    {sizeof(beam_builtin_binding), "map_merge_2", "maps", "merge", 2},
    {sizeof(beam_builtin_binding), "map_put_3", "maps", "put", 3},
    {sizeof(beam_builtin_binding), "map_remove_2", "maps", "remove", 2},
    {sizeof(beam_builtin_binding), "map_update_3", "maps", "update", 3},
    {sizeof(beam_builtin_binding), "map_values_1", "maps", "values", 1},
    {sizeof(beam_builtin_binding), "map_take_2", "maps", "take", 2},
    {sizeof(beam_builtin_binding), "map_from_keys_2", "maps", "from_keys", 2},
    {sizeof(beam_builtin_binding), "charlist_parse_float_prefix_1", "string", "list_to_float", 1},
    {sizeof(beam_builtin_binding), "chardata_to_utf8_2", "unicode", "characters_to_binary", 2},
    {sizeof(beam_builtin_binding), "chardata_to_codepoints_2", "unicode", "characters_to_list", 2},
    {sizeof(beam_builtin_binding), "bytes_is_7bit_1", "unicode", "bin_is_7bit", 1},

    {sizeof(beam_builtin_binding), "atomic_array_get_2", "atomics", "get", 2},
    {sizeof(beam_builtin_binding), "atomic_array_put_3", "atomics", "put", 3},
    {sizeof(beam_builtin_binding), "atomic_array_add_3", "atomics", "add", 3},
    {sizeof(beam_builtin_binding), "atomic_array_add_get_3", "atomics", "add_get", 3},
    {sizeof(beam_builtin_binding), "atomic_array_exchange_3", "atomics", "exchange", 3},
    {sizeof(beam_builtin_binding), "atomic_array_compare_exchange_4", "atomics", "compare_exchange", 4},
    {sizeof(beam_builtin_binding), "atomic_array_info_1", "atomics", "info", 1},
    {sizeof(beam_builtin_binding), "persistent_value_put_2", "persistent_term", "put", 2},
    {sizeof(beam_builtin_binding), "persistent_value_put_new_2", "persistent_term", "put_new", 2},
    {sizeof(beam_builtin_binding), "persistent_value_get_0", "persistent_term", "get", 0},
    {sizeof(beam_builtin_binding), "persistent_value_get_1", "persistent_term", "get", 1},
    {sizeof(beam_builtin_binding), "persistent_value_get_2", "persistent_term", "get", 2},
    {sizeof(beam_builtin_binding), "persistent_value_erase_1", "persistent_term", "erase", 1},
    {sizeof(beam_builtin_binding), "persistent_value_info_0", "persistent_term", "info", 0},
    {sizeof(beam_builtin_binding), "term_table_internal_request_all_0", "ets", "internal_request_all", 0},
    {sizeof(beam_builtin_binding), "term_table_new_list_2", "ets", "new", 2},
    {sizeof(beam_builtin_binding), "term_table_delete_1", "ets", "delete", 1},
    {sizeof(beam_builtin_binding), "term_table_delete_2", "ets", "delete", 2},
    {sizeof(beam_builtin_binding), "term_table_delete_object_2", "ets", "delete_object", 2},
    {sizeof(beam_builtin_binding), "term_table_first_1", "ets", "first", 1},
    {sizeof(beam_builtin_binding), "term_table_first_lookup_1", "ets", "first_lookup", 1},
    {sizeof(beam_builtin_binding), "term_table_is_compiled_ms_1", "ets", "is_compiled_ms", 1},
    {sizeof(beam_builtin_binding), "term_table_lookup_2", "ets", "lookup", 2},
    {sizeof(beam_builtin_binding), "term_table_lookup_element_3", "ets", "lookup_element", 3},
    {sizeof(beam_builtin_binding), "term_table_lookup_element_4", "ets", "lookup_element", 4},
    {sizeof(beam_builtin_binding), "term_table_info_1", "ets", "info", 1},
    {sizeof(beam_builtin_binding), "term_table_info_2", "ets", "info", 2},
    {sizeof(beam_builtin_binding), "term_table_last_1", "ets", "last", 1},
    {sizeof(beam_builtin_binding), "term_table_last_lookup_1", "ets", "last_lookup", 1},
    {sizeof(beam_builtin_binding), "term_table_match_1", "ets", "match", 1},
    {sizeof(beam_builtin_binding), "term_table_match_2", "ets", "match", 2},
    {sizeof(beam_builtin_binding), "term_table_match_3", "ets", "match", 3},
    {sizeof(beam_builtin_binding), "term_table_match_object_1", "ets", "match_object", 1},
    {sizeof(beam_builtin_binding), "term_table_match_object_2", "ets", "match_object", 2},
    {sizeof(beam_builtin_binding), "term_table_match_object_3", "ets", "match_object", 3},
    {sizeof(beam_builtin_binding), "term_table_member_2", "ets", "member", 2},
    {sizeof(beam_builtin_binding), "term_table_next_2", "ets", "next", 2},
    {sizeof(beam_builtin_binding), "term_table_next_lookup_2", "ets", "next_lookup", 2},
    {sizeof(beam_builtin_binding), "term_table_prev_2", "ets", "prev", 2},
    {sizeof(beam_builtin_binding), "term_table_prev_lookup_2", "ets", "prev_lookup", 2},
    {sizeof(beam_builtin_binding), "term_table_insert_2", "ets", "insert", 2},
    {sizeof(beam_builtin_binding), "term_table_insert_new_2", "ets", "insert_new", 2},
    {sizeof(beam_builtin_binding), "term_table_rename_2", "ets", "rename", 2},
    {sizeof(beam_builtin_binding), "term_table_safe_fixtable_2", "ets", "safe_fixtable", 2},
    {sizeof(beam_builtin_binding), "term_table_slot_2", "ets", "slot", 2},
    {sizeof(beam_builtin_binding), "term_table_update_counter_3", "ets", "update_counter", 3},
    {sizeof(beam_builtin_binding), "term_table_select_1", "ets", "select", 1},
    {sizeof(beam_builtin_binding), "term_table_select_2", "ets", "select", 2},
    {sizeof(beam_builtin_binding), "term_table_select_3", "ets", "select", 3},
    {sizeof(beam_builtin_binding), "term_table_select_count_2", "ets", "select_count", 2},
    {sizeof(beam_builtin_binding), "term_table_select_reverse_1", "ets", "select_reverse", 1},
    {sizeof(beam_builtin_binding), "term_table_select_reverse_2", "ets", "select_reverse", 2},
    {sizeof(beam_builtin_binding), "term_table_select_reverse_3", "ets", "select_reverse", 3},
    {sizeof(beam_builtin_binding), "term_table_select_replace_2", "ets", "select_replace", 2},
    {sizeof(beam_builtin_binding), "term_table_match_spec_compile_1", "ets", "match_spec_compile", 1},
    {sizeof(beam_builtin_binding), "term_table_match_spec_run_r_3", "ets", "match_spec_run_r", 3},
    {sizeof(beam_builtin_binding), "term_table_update_element_3", "ets", "update_element", 3},
    {sizeof(beam_builtin_binding), "term_table_give_away_3", "ets", "give_away", 3},
    {sizeof(beam_builtin_binding), "term_table_setopts_2", "ets", "setopts", 2},
    {sizeof(beam_builtin_binding), "term_table_take_2", "ets", "take", 2},
    {sizeof(beam_builtin_binding), "term_table_update_counter_4", "ets", "update_counter", 4},
    {sizeof(beam_builtin_binding), "term_table_whereis_1", "ets", "whereis", 1},
    {sizeof(beam_builtin_binding), "term_table_internal_delete_all_2", "ets", "internal_delete_all", 2},
    {sizeof(beam_builtin_binding), "term_table_internal_select_delete_2", "ets", "internal_select_delete", 2},
    {sizeof(beam_builtin_binding), "term_table_update_element_4", "ets", "update_element", 4},
};

extern ErlNifEntry *builtin_tracer_nif_init(ERL_NIF_INIT_ARGS);
static ErlNifEntry *tracer_initialize(void)
{
    return builtin_tracer_nif_init(
#ifdef _WIN32
        NULL
#endif
    );
}

static int returns_ok(beam_vm *vm, const char *function)
{
    beam_error error = {0};
    beam_result result = {0};
    const unsigned char empty[] = {131, 106};
    const unsigned char ok[] = {131, 119, 2, 'o', 'k'};
    int matched;
    if (beam_vm_call(vm, "bif_selection", function, (beam_bytes){empty, sizeof empty}, &result) != BEAM_OK)
        return 0;
    matched = result.kind == BEAM_RETURN && result.value.size == sizeof ok &&
        !memcmp(result.value.data, ok, sizeof ok);
    if (!matched) {
        fprintf(stderr, "builtin: %s returned kind %d size %zu ETF=", function, (int)result.kind, result.value.size);
        for (size_t i=0;i<result.value.size && i<4096;++i)
            fprintf(stderr,"%02x",result.value.data[i]);
        fputc('\n',stderr);
    }
    beam_result_release(&result);
    (void)error;
    return matched;
}

/* Test instrumentation observes actual VM allocator and thread ownership. */
extern void beam_runtime_memory_usage(size_t *, size_t *, size_t *, size_t *);
extern void ethr_runtime_usage(size_t *, size_t *, size_t *, size_t *, size_t *, size_t *);
extern void erts_sys_resource_usage(size_t *, size_t *);
static int state_resources_released(void)
{
    size_t allocations, bytes, mappings, mapped_bytes;
    size_t vm_keys, system_keys, vm_callbacks, system_callbacks, blocks, slots;
    size_t vm_locks, system_locks;
    beam_runtime_memory_usage(&allocations, &bytes, &mappings, &mapped_bytes);
    ethr_runtime_usage(&vm_keys, &system_keys, &vm_callbacks, &system_callbacks, &blocks, &slots);
    erts_sys_resource_usage(&vm_locks, &system_locks);
    return !allocations && !bytes && !mappings && !mapped_bytes &&
           !vm_keys && !vm_callbacks && !vm_locks;
}

static int run_case(beam_system *system, const beam_builtin_binding *functions, size_t count,
                    int explicit_list, int load_bytes, int load_ordinary, const char *function)
{
    beam_error error = {0};
    beam_vm *vm = NULL;
    beam_vm_options options;
    beam_composition *composition = NULL;
    beam_composition_options preparation = {.struct_size = sizeof preparation};
    const beam_core_export canonical_exports[] = {
        {sizeof(beam_core_export), "erlang", "convert_time_unit", 3},
        {sizeof(beam_core_export), "erlang", "trace", 3},
        {sizeof(beam_core_export), "erlang", "trace_pattern", 2},
        {sizeof(beam_core_export), "erlang", "trace_pattern", 3}};
    if (functions == otp_bindings) {
        preparation.default_tracer_module = "erl_tracer";
        preparation.core_export_count = sizeof canonical_exports / sizeof *canonical_exports;
        preparation.core_exports = canonical_exports;
    }
    if (explicit_list) {
        preparation.builtin_bindings = functions;
        preparation.builtin_binding_count = count;
        CHECK(beam_composition_prepare(&preparation, &composition, &error) == BEAM_OK);
    }
    beam_vm_options_init(&options);
    options.composition = composition;
    CHECK(beam_vm_init(system, &options, &vm, &error) == BEAM_OK);
    if (load_bytes) {
        CHECK(beam_module_load(vm, (beam_bytes){installed_beam, sizeof installed_beam}) == BEAM_OK);
        CHECK(beam_module_load(vm, (beam_bytes){installed_beam_format, sizeof installed_beam_format}) == BEAM_OK);
        CHECK(beam_module_load(vm, (beam_bytes){installed_beam_time, sizeof installed_beam_time}) == BEAM_OK);
        CHECK(beam_module_load(vm, (beam_bytes){installed_beam_atomics, sizeof installed_beam_atomics}) == BEAM_OK);
        CHECK(beam_module_load(vm, (beam_bytes){installed_beam_counters, sizeof installed_beam_counters}) == BEAM_OK);
        CHECK(beam_module_load(vm, (beam_bytes){installed_beam_persistent_term, sizeof installed_beam_persistent_term}) == BEAM_OK);
        CHECK(beam_module_load(vm, (beam_bytes){installed_beam_binary, sizeof installed_beam_binary}) == BEAM_OK);
        CHECK(beam_module_load(vm, (beam_bytes){installed_beam_lists, sizeof installed_beam_lists}) == BEAM_OK);
        CHECK(beam_module_load(vm, (beam_bytes){installed_beam_maps, sizeof installed_beam_maps}) == BEAM_OK);
        CHECK(beam_module_load(vm, (beam_bytes){installed_beam_records, sizeof installed_beam_records}) == BEAM_OK);
        CHECK(beam_module_load(vm, (beam_bytes){installed_beam_string, sizeof installed_beam_string}) == BEAM_OK);
        CHECK(beam_module_load(vm, (beam_bytes){installed_beam_unicode, sizeof installed_beam_unicode}) == BEAM_OK);
        CHECK(beam_module_load(vm, (beam_bytes){installed_beam_runtime_unicode_util, sizeof installed_beam_runtime_unicode_util}) == BEAM_OK);
        CHECK(beam_module_load(vm, (beam_bytes){installed_beam_math, sizeof installed_beam_math}) == BEAM_OK);
        CHECK(beam_module_load(vm, (beam_bytes){installed_beam_ddll, sizeof installed_beam_ddll}) == BEAM_OK);
        CHECK(beam_module_load(vm, (beam_bytes){installed_beam_debugger, sizeof installed_beam_debugger}) == BEAM_OK);
    }
    CHECK(beam_module_load(vm, (beam_bytes){bif_selection, sizeof bif_selection}) == BEAM_OK);
    if (load_ordinary)
        CHECK(beam_module_load(vm, (beam_bytes){ordinary_binary, sizeof ordinary_binary}) == BEAM_OK);
    CHECK(beam_vm_start(vm, &error) == BEAM_OK);
    CHECK(returns_ok(vm, function));
    if (!strcmp(function, "modern")) CHECK(returns_ok(vm, "modern_time"));
    if (!strcmp(function, "otp")) CHECK(returns_ok(vm, "otp_time"));
    if (!strcmp(function, "none")) CHECK(returns_ok(vm, "no_time"));
    CHECK(beam_vm_terminate(&vm, NULL) == BEAM_OK);
    CHECK(state_resources_released());
    beam_composition_release(&composition);
    return 0;
}

static int load_bytes_call(beam_vm *vm, const char *function, const unsigned char *bytes, size_t size)
{
    unsigned char *arguments = malloc(size + 12);
    beam_result result = {0};
    const unsigned char ok[] = {131, 119, 2, 'o', 'k'};
    if (!arguments) return 0;
    memcpy(arguments, "\203\154\0\0\0\1\155", 7);
    for (unsigned i = 0; i < 4; ++i) arguments[7 + i] = (unsigned char)(size >> (24 - i * 8));
    memcpy(arguments + 11, bytes, size);
    arguments[11 + size] = 106;
    int matched = beam_vm_call(vm, "bif_selection", function,
        (beam_bytes){arguments, size + 12}, &result) == BEAM_OK &&
        result.kind == BEAM_RETURN && result.value.size == sizeof ok &&
        !memcmp(result.value.data, ok, sizeof ok);
    free(arguments);
    beam_result_release(&result);
    return matched;
}

extern unsigned builtin_ets_resource_live(void);

static int renamed_case(beam_system *system)
{
    beam_error error = {0};
    beam_composition *composition = NULL;
    beam_vm *vm = NULL;
    beam_vm_options options;
    char module[] = "bound.bytes", function[] = "cut", identifier[] = "bytes_split_2";
    beam_builtin_binding bindings[] = {
        {sizeof(beam_builtin_binding), "clock_monotonic_1", "bound.clock", "read", 1},
        {sizeof(beam_builtin_binding), "timer_send_after_3", "bound.clock", "send", 3},
        {sizeof(beam_builtin_binding), "timer_cancel_2", "bound.clock", "cancel", 2},
        {sizeof(beam_builtin_binding), identifier, module, function, 2},
        {sizeof(beam_builtin_binding), "bytes_split_map_3", module, function, 3},
        {sizeof(beam_builtin_binding), "bytes_repeat_modern_2", module, "repeat", 2},
        {sizeof(beam_builtin_binding), "bytes_referenced_size_modern_1", module, "referenced_size", 1},
        {sizeof(beam_builtin_binding), "bytes_from_list_1", module, "flatten", 1},
        {sizeof(beam_builtin_binding), "bytes_find_modern_2", module, "seek", 2},
        {sizeof(beam_builtin_binding), "bytes_find_modern_3", module, "seek", 3},
        {sizeof(beam_builtin_binding), "bytes_find_all_2", module, "ranges", 2},
        {sizeof(beam_builtin_binding), "list_contains_modern_2", module, "has", 2},
        {sizeof(beam_builtin_binding), "list_reverse_modern_2", module, "back", 2},
        {sizeof(beam_builtin_binding), "map_find_modern_2", module, "lookup", 2},
        {sizeof(beam_builtin_binding), "map_put_modern_3", module, "insert", 3},
        {sizeof(beam_builtin_binding), "text_from_utf8_modern_1", module, "valid", 1},
        {sizeof(beam_builtin_binding), "chardata_to_utf8_2", module, "utf8", 2},
        {sizeof(beam_builtin_binding), "chardata_to_codepoints_2", module, "chars", 2},
        {sizeof(beam_builtin_binding), "atomic_array_new_list_modern_2", module, "a_new", 2},
        {sizeof(beam_builtin_binding), "atomic_array_get_2", module, "a_get", 2},
        {sizeof(beam_builtin_binding), "atomic_array_put_3", module, "a_put", 3},
        {sizeof(beam_builtin_binding), "atomic_array_add_3", module, "a_add", 3},
        {sizeof(beam_builtin_binding), "atomic_array_add_get_3", module, "a_add_get", 3},
        {sizeof(beam_builtin_binding), "atomic_array_exchange_3", module, "a_exchange", 3},
        {sizeof(beam_builtin_binding), "atomic_array_compare_exchange_4", module, "a_compare_exchange", 4},
        {sizeof(beam_builtin_binding), "atomic_array_info_1", module, "a_info", 1},
        {sizeof(beam_builtin_binding), "counter_array_new_list_modern_2", module, "c_new", 2},
        {sizeof(beam_builtin_binding), "counter_array_get_modern_2", module, "c_get", 2},
        {sizeof(beam_builtin_binding), "counter_array_put_modern_3", module, "c_put", 3},
        {sizeof(beam_builtin_binding), "counter_array_add_modern_3", module, "c_add", 3},
        {sizeof(beam_builtin_binding), "counter_array_info_modern_1", module, "c_info", 1},
        {sizeof(beam_builtin_binding), "persistent_value_put_2", module, "p_put", 2},
        {sizeof(beam_builtin_binding), "persistent_value_put_new_2", module, "p_put_new", 2},
        {sizeof(beam_builtin_binding), "persistent_value_get_0", module, "p_get", 0},
        {sizeof(beam_builtin_binding), "persistent_value_get_1", module, "p_get", 1},
        {sizeof(beam_builtin_binding), "persistent_value_get_2", module, "p_get", 2},
        {sizeof(beam_builtin_binding), "persistent_value_erase_1", module, "p_erase", 1},
        {sizeof(beam_builtin_binding), "persistent_value_info_0", module, "p_info", 0},
        {sizeof(beam_builtin_binding), "term_table_new_list_2", "bound.tables", "t_new", 2},
        {sizeof(beam_builtin_binding), "term_table_insert_2", "bound.tables", "t_insert", 2},
        {sizeof(beam_builtin_binding), "term_table_lookup_2", "bound.tables", "t_lookup", 2},
        {sizeof(beam_builtin_binding), "term_table_info_2", "bound.tables", "t_info", 2},
        {sizeof(beam_builtin_binding), "term_table_select_2", "bound.tables", "t_select", 2},
        {sizeof(beam_builtin_binding), "term_table_select_3", "bound.tables", "t_chunk", 3},
        {sizeof(beam_builtin_binding), "term_table_match_2", "bound.tables", "t_match", 2},
        {sizeof(beam_builtin_binding), "term_table_delete_1", "bound.tables", "t_drop", 1},
        {sizeof(beam_builtin_binding), "term_table_match_spec_compile_1", "bound.tables", "t_compile", 1},
        {sizeof(beam_builtin_binding), "term_table_match_spec_run_r_3", "bound.tables", "t_run", 3},};
    beam_nif_binding tracer = {sizeof tracer, "builtin_tracer", beam_build_id(), tracer_initialize};
    beam_composition_options preparation = {.struct_size = sizeof preparation,
        .builtin_binding_count = sizeof bindings / sizeof *bindings, .builtin_bindings = bindings,
        .binding_count = 1, .bindings = &tracer};
    beam_module_spec modules[] = {
        {"builtin_tracer", {builtin_tracer, sizeof builtin_tracer}, "memory:tracer"},
        {"bound.bytes", {bound_bytes_missing, sizeof bound_bytes_missing}, "memory:incomplete"}};
    preparation.module_count = 2;
    preparation.modules = modules;
    CHECK(beam_composition_prepare(&preparation, &composition, &error) == BEAM_OK);
    beam_vm_options_init(&options);
    options.composition = composition;
    CHECK(beam_vm_init(system, &options, &vm, &error) == BEAM_INVALID_BEAM && !vm);
    beam_composition_release(&composition);
    preparation.module_count = 1;
    CHECK(beam_composition_prepare(&preparation, &composition, &error) == BEAM_OK);
    memset(module, 'x', sizeof module - 1);
    memset(function, 'x', sizeof function - 1);
    memset(identifier, 'x', sizeof identifier - 1);
    beam_vm_options_init(&options);
    options.schedulers = options.schedulers_online = 1;
    options.composition = composition;
    CHECK(beam_vm_init(system, &options, &vm, &error) == BEAM_OK);
    beam_composition_release(&composition);
    CHECK(beam_module_load(vm, (beam_bytes){bound_bytes_missing, sizeof bound_bytes_missing}) == BEAM_INVALID_BEAM);
    CHECK(beam_module_load(vm, (beam_bytes){bound_bytes_1, sizeof bound_bytes_1}) == BEAM_OK);
    CHECK(beam_module_load(vm, (beam_bytes){bif_selection, sizeof bif_selection}) == BEAM_OK);
    CHECK(beam_vm_start(vm, &error) == BEAM_OK);
    CHECK(beam_vm_bind_nif(vm, "builtin_tracer", &error) == BEAM_OK);
    CHECK(returns_ok(vm, "renamed"));
    CHECK(returns_ok(vm, "custom_time"));
    CHECK(returns_ok(vm, "search_yields"));
    CHECK(returns_ok(vm, "ets_partial"));
    CHECK(returns_ok(vm, "tables_trace"));
    CHECK(beam_module_load(vm, (beam_bytes){bound_tables, sizeof bound_tables}) == BEAM_OK);
    CHECK(returns_ok(vm, "tables_loaded"));
    CHECK(returns_ok(vm, "tables_trace"));
    CHECK(beam_module_unload(vm, "bound.tables", &error) == BEAM_OK);
    CHECK(returns_ok(vm, "tables_trace"));
    CHECK(beam_module_load(vm, (beam_bytes){bound_tables_onload, sizeof bound_tables_onload}) == BEAM_UNSUPPORTED);
    CHECK(load_bytes_call(vm, "tables_onload", bound_tables_onload, sizeof bound_tables_onload));
    CHECK(returns_ok(vm, "tables_loaded_onload"));
    CHECK(beam_module_unload(vm, "bound.tables", &error) == BEAM_OK);

    CHECK(beam_module_load(vm, (beam_bytes){bound_bytes_missing, sizeof bound_bytes_missing}) == BEAM_INVALID_BEAM);
    CHECK(returns_ok(vm, "retained"));
    CHECK(load_bytes_call(vm, "reload", bound_bytes_missing, sizeof bound_bytes_missing));
    CHECK(beam_module_load(vm, (beam_bytes){bound_bytes_2, sizeof bound_bytes_2}) == BEAM_ALREADY_LOADED);
    CHECK(load_bytes_call(vm, "reload", bound_bytes_2, sizeof bound_bytes_2));
    CHECK(returns_ok(vm, "reloaded"));
    CHECK(beam_module_unload(vm, "bound.bytes", &error) == BEAM_OK);
    CHECK(returns_ok(vm, "unloaded"));
    CHECK(beam_module_load(vm, (beam_bytes){bound_bytes_2, sizeof bound_bytes_2}) == BEAM_OK);
    CHECK(returns_ok(vm, "reloaded"));
    CHECK(returns_ok(vm, "ets_abandon"));
    CHECK(returns_ok(vm, "abandon"));
    CHECK(builtin_ets_resource_live() > 0);
    CHECK(beam_vm_terminate(&vm, NULL) == BEAM_OK);
    CHECK(builtin_ets_resource_live() == 0);
    CHECK(state_resources_released());
    return 0;
}

static int core_selection_validation(beam_system *system)
{
    beam_error error = {0};
    beam_composition *composition = NULL;
    char module[] = "erlang", function[] = "convert_time_unit";
    beam_core_export exports[] = {
        {sizeof(beam_core_export), module, function, 3},
        {sizeof(beam_core_export), module, function, 3}};
    beam_builtin_binding native = {sizeof native, "clock_timestamp_0", "erlang", "timestamp", 0};
    beam_composition_options prep = {.struct_size=sizeof prep,
        .core_export_count=2,.core_exports=exports};
    CHECK(beam_composition_prepare(&prep,&composition,&error)==BEAM_INVALID_ARGUMENT && !composition);
    prep.core_export_count=1; exports[0].arity=2;
    CHECK(beam_composition_prepare(&prep,&composition,&error)==BEAM_INVALID_ARGUMENT && !composition);
    exports[0].arity=3; exports[0].struct_size=0;
    CHECK(beam_composition_prepare(&prep,&composition,&error)==BEAM_INVALID_ARGUMENT && !composition);
    exports[0].struct_size=sizeof exports[0];prep.core_exports=NULL;
    CHECK(beam_composition_prepare(&prep,&composition,&error)==BEAM_INVALID_ARGUMENT && !composition);
    prep.core_exports=exports;prep.builtin_binding_count=1;prep.builtin_bindings=&native;
    native.function="error";
    CHECK(beam_composition_prepare(&prep,&composition,&error)==BEAM_INVALID_ARGUMENT && !composition);
    native.function="monotonic_time";
    CHECK(beam_composition_prepare(&prep,&composition,&error)==BEAM_INVALID_ARGUMENT && !composition);
    native.function="timestamp";
    CHECK(beam_composition_prepare(&prep,&composition,&error)==BEAM_OK);
    memset(module,'x',sizeof module-1);memset(function,'x',sizeof function-1);
    beam_vm *vm=NULL;beam_vm_options options;beam_vm_options_init(&options);
    options.composition=composition;
    CHECK(beam_vm_init(system,&options,&vm,&error)==BEAM_OK);
    beam_composition_release(&composition);
    CHECK(beam_module_load(vm,(beam_bytes){bif_selection,sizeof bif_selection})==BEAM_OK);
    CHECK(beam_vm_start(vm,&error)==BEAM_OK);
    CHECK(returns_ok(vm,"partial_time"));
    CHECK(beam_vm_terminate(&vm,NULL)==BEAM_OK);
    CHECK(state_resources_released());
    return 0;
}

static int nif_selection_case(beam_system *system)
{
    beam_error error = {0};
    beam_nif_binding nif = {sizeof nif, "builtin_tracer", beam_build_id(), tracer_initialize};
    beam_module_spec module = {
        "builtin_tracer", {builtin_tracer, sizeof builtin_tracer}, "memory:tracer"};
    beam_builtin_binding conflict = {
        sizeof conflict, "vm_self_0", "builtin_tracer", "resources", 0};
    const unsigned char args[] = {131, 106};
    const unsigned char zero[] = {131, 97, 0};
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        beam_composition_options input = {.struct_size = sizeof input,
            .module_count = 1, .modules = &module, .binding_count = 1, .bindings = &nif,
            .builtin_binding_count = attempt == 1, .builtin_bindings = &conflict};
        beam_composition *composition = NULL;
        beam_vm *vm = NULL;
        beam_vm_options options;
        CHECK(beam_composition_prepare(&input, &composition, &error) == BEAM_OK);
        beam_vm_options_init(&options);
        options.composition = composition;
        CHECK(beam_vm_init(system, &options, &vm, &error) == BEAM_OK);
        beam_composition_release(&composition);
        CHECK(beam_vm_start(vm, &error) == BEAM_OK);
        beam_status status = beam_vm_bind_nif(vm, "builtin_tracer", &error);
        if (attempt == 1) {
            CHECK(status == BEAM_SYSTEM_ERROR);
            CHECK(strstr(error.message, "selected native implementation") != NULL);
        } else {
            beam_result result = {0};
            CHECK(status == BEAM_OK);
            CHECK(beam_vm_call(vm, "builtin_tracer", "resources",
                (beam_bytes){args, sizeof args}, &result) == BEAM_OK);
            CHECK(result.kind == BEAM_RETURN && result.value.size == sizeof zero &&
                !memcmp(result.value.data, zero, sizeof zero));
            beam_result_release(&result);
        }
        CHECK(beam_vm_terminate(&vm, NULL) == BEAM_OK);
        CHECK(builtin_ets_resource_live() == 0 && state_resources_released());
    }
    return 0;
}

static int native_returns_ok(beam_vm *vm, const char *function, beam_bytes arguments)
{
    beam_result result = {0};
    const unsigned char ok[] = {131,119,2,'o','k'};
    beam_status status = beam_vm_call(vm,"native_calls",function,arguments,&result);
    int valid = status == BEAM_OK && result.kind == BEAM_RETURN &&
        result.value.size == sizeof ok && !memcmp(result.value.data,ok,sizeof ok);
    if (!valid) {
        fprintf(stderr,"native_calls:%s: status=%d kind=%d ETF=",function,status,result.kind);
        for (size_t i=0;i<result.value.size && i<4096;++i)
            fprintf(stderr,"%02x",result.value.data[i]);
        fputc('\n',stderr);
    }
    beam_result_release(&result);
    return valid;
}

static unsigned char *native_atom(unsigned char *at, const char *value)
{
    size_t length = strlen(value);
    if (length > 255) abort();
    *at++=119; *at++=(unsigned char)length;
    memcpy(at,value,length);
    return at+length;
}

static int native_snapshot_exports(beam_vm *vm, const beam_builtin_binding *bindings,
                                    size_t count, int selected)
{
    beam_module_query query;
    beam_module_query_init(&query);
    query.scopes = BEAM_MODULE_SCOPE_LOADED | BEAM_MODULE_SCOPE_CALLABLE;
    query.fields = BEAM_MODULE_FIELD_EXPORTS;
    beam_module_snapshot snapshot = {0};
    if (beam_module_inspect_all(vm,&query,&snapshot) != BEAM_OK) return 0;
    int valid = 1;
    for (size_t i=1; i<snapshot.count; ++i) {
        beam_module_name a=snapshot.items[i-1].name,b=snapshot.items[i].name;
        size_t size=a.size<b.size?a.size:b.size;
        int order=memcmp(a.data,b.data,size);
        if (order>0 || (!order && a.size>=b.size)) valid=0;
    }
    for (size_t i=0; i<count && valid; ++i) {
        const beam_module_record *module=NULL;
        size_t module_size=strlen(bindings[i].module),function_size=strlen(bindings[i].function);
        for(size_t j=0;j<snapshot.count;++j)
            if(snapshot.items[j].name.size==module_size &&
               !memcmp(snapshot.items[j].name.data,bindings[i].module,module_size)) {
                module=&snapshot.items[j];break;
            }
        int found=0;
        if(module) for(size_t j=0;j<module->exports.count;++j)
            if(module->exports.items[j].arity==bindings[i].arity &&
               module->exports.items[j].name.size==function_size &&
               !memcmp(module->exports.items[j].name.data,bindings[i].function,function_size)) found=1;
        if(found!=selected) valid=0;
        if(selected && module && !(module->flags &
            (BEAM_MODULE_CURRENT|BEAM_MODULE_OLD|BEAM_MODULE_ON_LOAD))) {
            beam_module_snapshot absent={0};
            beam_module_query loaded;beam_module_query_init(&loaded);
            if(module->current.loaded_code_bytes || module->current.function_table_count ||
               beam_module_inspect(vm,module->name,&loaded,&absent)!=BEAM_NOT_FOUND ||
               absent.count || absent.items) valid=0;
            beam_module_snapshot_release(&absent);
        }
    }
    beam_module_snapshot_release(&snapshot);
    return valid;
}

static int native_exports(beam_vm *vm, const beam_builtin_binding *bindings,
                           size_t count, int selected)
{
    unsigned char *arguments = malloc(32+count*800), *at=arguments;
    if (!at) return 0;
    *at++=131; *at++=108; *at++=0; *at++=0; *at++=0; *at++=2;
    *at++=108;
    *at++=(unsigned char)(count>>24); *at++=(unsigned char)(count>>16);
    *at++=(unsigned char)(count>>8); *at++=(unsigned char)count;
    for (size_t i=0;i<count;++i) {
        *at++=104; *at++=3;
        at=native_atom(at,bindings[i].module);
        at=native_atom(at,bindings[i].function);
        *at++=97; *at++=(unsigned char)bindings[i].arity;
    }
    *at++=106;
    at=native_atom(at,selected?"true":"false");
    *at++=106;
    int valid=native_returns_ok(vm,"exports",(beam_bytes){arguments,(size_t)(at-arguments)});
    free(arguments);
    return valid && native_snapshot_exports(vm,bindings,count,selected);
}

static int native_selection_case(beam_system *system,
        const beam_builtin_binding *all, size_t all_count)
{
    beam_error error = {0};
#define NATIVE(ID,F,A) {sizeof(beam_builtin_binding),ID,"chosen",F,A}
    const beam_builtin_binding custom[] = {
        NATIVE("vm_is_float_1","float",1), NATIVE("vm_whereis_1","whereis",1),
        NATIVE("vm_erase_1","erase",1), NATIVE("vm_put_2","put",2),
        NATIVE("vm_erts_debug_dirty_cpu_2","dirty_cpu",2),
        NATIVE("vm_erts_debug_dirty_io_2","dirty_io",2),
        NATIVE("vm_self_0","self",0), NATIVE("vm_apply_2","invoke_fun",2),
        NATIVE("vm_error_1","fail",1), NATIVE("vm_throw_1","raise",1),
        NATIVE("vm_exit_1","stop",1), NATIVE("vm_math_log_1","log",1),
        NATIVE("vm_apply_3","invoke",3), NATIVE("vm_yield_0","relax",0),
        NATIVE("vm_hibernate_0","sleep",0)};
    const beam_builtin_binding wrong[] = {NATIVE("vm_is_integer_1","float",1)};
#undef NATIVE
    const struct { const char *profile; const beam_builtin_binding *entries; size_t count; } cases[] = {
        {"canonical",otp_bindings,sizeof otp_bindings/sizeof *otp_bindings},
        {"modern",modern_bindings,sizeof modern_bindings/sizeof *modern_bindings},
        {"custom",custom,sizeof custom/sizeof *custom},
        {"wrong",wrong,1}, {"ordinary",NULL,0}, {"empty",NULL,0},
        {"canonical",otp_bindings,sizeof otp_bindings/sizeof *otp_bindings}};
    for (size_t i=0;i<sizeof cases/sizeof *cases;++i) {
        beam_vm *vm=NULL;
        beam_composition *composition=NULL;
        beam_nif_binding tracer = {sizeof tracer,"builtin_tracer",beam_build_id(),tracer_initialize};
        beam_module_spec module = {"builtin_tracer",{builtin_tracer,sizeof builtin_tracer},"memory:native-tracer"};
        const beam_core_export canonical_exports[] = {
            {sizeof(beam_core_export),"erlang","trace",3},
            {sizeof(beam_core_export),"erlang","trace_pattern",2},
            {sizeof(beam_core_export),"erlang","trace_pattern",3}};
        beam_composition_options input = {.struct_size=sizeof input,
            .core_export_count = !strcmp(cases[i].profile,"canonical") ? 3 : 0,
            .core_exports = canonical_exports,
            .builtin_bindings=cases[i].entries,.builtin_binding_count=cases[i].count,
            .binding_count=1,.bindings=&tracer,.module_count=1,.modules=&module};
        CHECK(beam_composition_prepare(&input,&composition,&error)==BEAM_OK);
        beam_vm_options options;
        beam_vm_options_init(&options); options.composition=composition;
        CHECK(beam_vm_init(system,&options,&vm,&error)==BEAM_OK);
        beam_composition_release(&composition);
        CHECK(beam_module_load(vm,(beam_bytes){native_calls,sizeof native_calls})==BEAM_OK);
        CHECK(beam_module_load(vm,(beam_bytes){native_modern,sizeof native_modern})==BEAM_OK);
        if (!strcmp(cases[i].profile,"ordinary"))
            CHECK(beam_module_load(vm,(beam_bytes){chosen,sizeof chosen})==BEAM_OK);
        if (cases[i].count) CHECK(native_snapshot_exports(vm,cases[i].entries,cases[i].count,1));
        else CHECK(native_snapshot_exports(vm,all,all_count,0));
        CHECK(beam_vm_start(vm,&error)==BEAM_OK);
        CHECK(beam_vm_bind_nif(vm,"builtin_tracer",&error)==BEAM_OK);
        if (cases[i].count) CHECK(native_exports(vm,cases[i].entries,cases[i].count,1));
        else CHECK(native_exports(vm,all,all_count,0));
        unsigned char arguments[32]={131,108,0,0,0,1};
        unsigned char *end=native_atom(arguments+6,cases[i].profile); *end++=106;
        CHECK(native_returns_ok(vm,"run",(beam_bytes){arguments,(size_t)(end-arguments)}));
        if (!strcmp(cases[i].profile,"custom")) {
            CHECK(beam_module_load(vm,(beam_bytes){chosen_provider,sizeof chosen_provider})==BEAM_OK);
            CHECK(native_exports(vm,custom,sizeof custom/sizeof *custom,1));
            CHECK(native_returns_ok(vm,"run",(beam_bytes){arguments,(size_t)(end-arguments)}));
            CHECK(beam_module_unload(vm,"chosen",&error)==BEAM_OK);
            CHECK(native_exports(vm,custom,sizeof custom/sizeof *custom,1));
            CHECK(native_returns_ok(vm,"run",(beam_bytes){arguments,(size_t)(end-arguments)}));
        }
        CHECK(beam_vm_terminate(&vm,NULL)==BEAM_OK);
        CHECK(builtin_ets_resource_live()==0 && state_resources_released());
    }
    return 0;
}

static int tracing_validation(beam_system *system)
{
    beam_error error = {0};
    beam_composition *composition = NULL;
    beam_composition_options p = {.struct_size=sizeof p};
    p.default_tracer_module = "";
    CHECK(beam_composition_prepare(&p,&composition,&error)==BEAM_INVALID_ARGUMENT && !composition);
    p.default_tracer_module = "\xff";
    CHECK(beam_composition_prepare(&p,&composition,&error)==BEAM_INVALID_ARGUMENT && !composition);
    p.default_tracer_module = NULL;
    p.system_worker_entry_count = 1;
    CHECK(beam_composition_prepare(&p,&composition,&error)==BEAM_INVALID_ARGUMENT && !composition);
    beam_system_worker_entry entry = {sizeof entry, BEAM_SYSTEM_WORKER_TRACE_CLEANER, "tracing_contract", "absent"};
    p.system_worker_entries = &entry;
    CHECK(beam_composition_prepare(&p,&composition,&error)==BEAM_INVALID_ARGUMENT && !composition);
    beam_module_spec fixture = {"tracing_contract",{tracing_contract,sizeof tracing_contract},"memory:worker-validation"};
    p.preload_count = 1; p.preloads = &fixture;
    entry.worker = (beam_system_worker)0;
    CHECK(beam_composition_prepare(&p,&composition,&error)==BEAM_INVALID_ARGUMENT && !composition);
    entry.worker = BEAM_SYSTEM_WORKER_TRACE_CLEANER;
    entry.struct_size = 0;
    CHECK(beam_composition_prepare(&p,&composition,&error)==BEAM_INVALID_ARGUMENT && !composition);
    entry.struct_size = sizeof entry;
    p.system_worker_entry_count = 2;
    CHECK(beam_composition_prepare(&p,&composition,&error)==BEAM_INVALID_ARGUMENT && !composition);
    p.system_worker_entry_count = 1;
    CHECK(beam_composition_prepare(&p,&composition,&error)==BEAM_OK);
    beam_vm_options options; beam_vm_options_init(&options); options.composition = composition;
    beam_vm *vm = NULL;
    CHECK(beam_vm_init(system,&options,&vm,&error)==BEAM_NOT_FOUND && !vm);
    CHECK(state_resources_released());
    beam_composition_release(&composition);
    p.system_worker_entry_count = 0;
    beam_builtin_binding obsolete = {sizeof obsolete,"vm_erts_internal_trace_session_create_3","beam_internal","trace_session_create",3};
    p.builtin_binding_count=1; p.builtin_bindings=&obsolete;
    CHECK(beam_composition_prepare(&p,&composition,&error)==BEAM_INVALID_ARGUMENT && !composition);
    obsolete.implementation="vm_erts_internal_system_monitor_3";obsolete.function="system_monitor";
    CHECK(beam_composition_prepare(&p,&composition,&error)==BEAM_INVALID_ARGUMENT && !composition);
    return 0;
}

static int tracing_case(beam_system *system)
{
    beam_error error = {0};
    for (int pass = 0; pass < 3; ++pass) {
        beam_library *library = NULL;
        beam_library_inputs inputs = {.struct_size = sizeof inputs};
        CHECK(beam_library_from_static(beam_tracing_library(), &library, &error) == BEAM_OK);
        CHECK(beam_library_get_inputs(library, &inputs) == BEAM_OK);
        CHECK(inputs.module_count == 2 && inputs.nif_count == 1);
        beam_module_spec modules[3];
        memcpy(modules, inputs.modules, inputs.module_count * sizeof *modules);
        beam_module_spec fixture = {"tracing_contract", {tracing_contract,sizeof tracing_contract}, "memory:tracing-contract"};
        modules[2] = fixture;
        char default_module[] = "beam_trace";
        char worker_module[] = "tracing_contract", worker_function[] = "cleaner_start";
        beam_system_worker_entry entry = {sizeof entry, BEAM_SYSTEM_WORKER_TRACE_CLEANER, worker_module, worker_function};
        beam_composition_options preparation = {.struct_size = sizeof preparation,
            .module_count = pass == 2 ? 2 : 3, .modules = modules,
            .preload_count = pass == 2, .preloads = &fixture,
            .system_worker_entry_count = pass == 2, .system_worker_entries = &entry,
            .default_tracer_module = pass ? default_module : NULL,
            .builtin_binding_count = sizeof modern_bindings / sizeof *modern_bindings,
            .builtin_bindings = modern_bindings,
            .builtin_nif_count = inputs.nif_count, .builtin_nifs = inputs.nifs,
            .native_owner_count = 1, .native_owners = &inputs.owner};
        beam_composition *composition = NULL;
        CHECK(beam_composition_prepare(&preparation, &composition, &error) == BEAM_OK);
        memset(default_module, 'x', sizeof default_module - 1);
        memset(worker_module, 'x', sizeof worker_module - 1);
        memset(worker_function, 'x', sizeof worker_function - 1);
        beam_library_release(&library);
        beam_vm_options options; beam_vm_options_init(&options);
        options.composition = composition;
        beam_vm *vm = NULL;
        CHECK(beam_vm_init(system, &options, &vm, &error) == BEAM_OK);
        beam_composition_release(&composition);
        CHECK(beam_vm_start(vm, &error) == BEAM_OK);
        beam_result result = {0};
        const unsigned char args[] = {131,106};
        const unsigned char ok[] = {131,119,2,'o','k'};
        unsigned char core_args[sizeof private_core + 12] = {131,108,0,0,0,1,109};
        for (int j=0;j<4;++j) core_args[7+j]=(unsigned char)(sizeof private_core >> (24-8*j));
        memcpy(core_args+11,private_core,sizeof private_core);
        core_args[sizeof core_args-1]=106;
        const char *functions[] = {"missing_backend", "activate", pass ? "run" : "custom_backend", pass == 2 ? "core_override" : "core_default", "core_binary"};
        for (size_t i = 0; i < sizeof functions / sizeof *functions; ++i) {
            const char *module = "tracing_contract";
            beam_bytes input = i == 4 ? (beam_bytes){core_args,sizeof core_args} : (beam_bytes){args,sizeof args};
            CHECK(beam_vm_call(vm, module, functions[i], input, &result) == BEAM_OK);
            if (result.kind != BEAM_RETURN || result.value.size != sizeof ok || memcmp(result.value.data,ok,sizeof ok)) {
                fprintf(stderr, "tracing %s:%s ETF=", module, functions[i]);
                for (size_t j=0;j<result.value.size && j<4096;++j) fprintf(stderr,"%02x",result.value.data[j]);
                fputc('\n',stderr);return 1;
            }
            beam_result_release(&result);
        }
        CHECK(beam_vm_terminate(&vm, NULL) == BEAM_OK);
        CHECK(state_resources_released());
    }
    return 0;
}

int main(void)
{
    beam_system *system = NULL;
    beam_error error = {0};
    beam_builtin_binding mixed[sizeof otp_bindings / sizeof *otp_bindings +
                               sizeof modern_bindings / sizeof *modern_bindings];
    size_t mixed_count = sizeof modern_bindings / sizeof *modern_bindings;
    CHECK(beam_init(&system, &error) == BEAM_OK);
    memcpy(mixed, modern_bindings, sizeof modern_bindings);
    /* Shared implementations use the modern destination; add distinct contracts once. */
    for (size_t i=0;i<sizeof otp_bindings/sizeof *otp_bindings;++i) {
        size_t j;
        for(j=0;j<mixed_count;++j)
            if(!strcmp(mixed[j].implementation,otp_bindings[i].implementation)) break;
        if(j==mixed_count) mixed[mixed_count++]=otp_bindings[i];
    }
    CHECK(mixed_count >= sizeof modern_bindings / sizeof *modern_bindings);
    CHECK(native_selection_case(system,mixed,mixed_count) == 0);
    CHECK(nif_selection_case(system) == 0);
    CHECK(tracing_validation(system) == 0);
    CHECK(tracing_case(system) == 0);
    CHECK(core_selection_validation(system) == 0);
    CHECK(run_case(system, modern_bindings, sizeof modern_bindings / sizeof *modern_bindings, 1, 1, 0, "modern") == 0);
    CHECK(run_case(system, otp_bindings, sizeof otp_bindings / sizeof *otp_bindings, 1, 0, 0, "otp") == 0);
    for (size_t i=0;i<mixed_count;++i)
        if (!strcmp(mixed[i].implementation,"bytes_split_list_3") ||
            !strcmp(mixed[i].implementation,"bytes_match_list_3")) mixed[i].module="binary";
    CHECK(run_case(system, mixed, mixed_count, 1, 1, 0, "mixed") == 0);
    CHECK(run_case(system, NULL, 0, 1, 1, 0, "none") == 0);
    CHECK(run_case(system, NULL, 0, 0, 1, 0, "none") == 0);
    CHECK(run_case(system, modern_bindings, sizeof modern_bindings / sizeof *modern_bindings, 1, 1, 1, "ordinary") == 0);
    CHECK(run_case(system, modern_bindings, sizeof modern_bindings / sizeof *modern_bindings, 1, 1, 0, "large") == 0);
    CHECK(renamed_case(system) == 0);
    CHECK(run_case(system, otp_bindings, sizeof otp_bindings / sizeof *otp_bindings, 1, 0, 0, "otp") == 0);
    CHECK(run_case(system, modern_bindings, sizeof modern_bindings / sizeof *modern_bindings, 1, 1, 0, "modern") == 0);
    CHECK(tracing_case(system) == 0);
    CHECK(beam_terminate(&system) == BEAM_OK);
    fputs("Bound builtin implementations completed; C continued.\n", stdout);
    return 0;
}
