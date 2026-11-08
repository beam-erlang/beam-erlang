# Native VM callable catalog

Hosts select implementation IDs through `beam_builtin_binding`. Each ID has one
public destination per VM. The same record selects regular, heavy, guard/operator
and instruction-backed contracts; there is no additional public BIF category.
Selected calls use native dispatch, with no forwarding wrapper or per-call
implementation registry. Zero bindings publish no native callable exports.

The catalog contains 573 contracts. The 432 general VM contracts appear below.
The [binding reference](reference.md#optional-native-bif-bindings) documents the
141 clock, timer, byte, collection, array and persistent-store contracts and their
argument/result policies. Their existing identifiers remain unchanged.

For `erlang` operations in these tables, modern hosts use the same function and
arity under `beam`; canonical compiler and Erlang/OTP hosts use `erlang`. The binary, list, map, record, string and Unicode families use their upstream
function names under the corresponding `beam_` module in modern compositions.
Other modules retain the shown identities.
These are host defaults, not VM-owned API names. Hosts can choose other names,
subject to the core namespace protections in the binding reference.

New C-backed IDs were assigned explicitly from their resolved C entry names.
Subsequent C renames do not rename an ID. C entries and specialization traits
below describe implementation details, not callable C interfaces. Calling class
and language guard eligibility are independent: a guard/operator calling class
does not by itself permit a qualified guard call. Dirty-test scheduling applies
only to builds configured with that test policy.

## Instruction-backed contracts

These IDs name existing VM instructions. Their selected exports use the ordinary
native dispatch path and remain independent of ordinary module code lifetimes.

| ID | Function | Entry | Behavior |
| --- | --- | --- | --- |
| `vm_apply_2` | `apply/2` | `i_apply_fun_only` | Apply a function to an argument list. |
| `vm_apply_3` | `apply/3` | `i_apply_only` | Apply an MFA to an argument list. |
| `vm_yield_0` | `yield/0` | `i_yield` | Yield and return `true`. |
| `vm_hibernate_0` | `hibernate/0` | `i_hibernate` | Hibernate until awakened, then return `ok` in the existing continuation. |

Canonical `is_builtin/3` reporting continues to classify `hibernate/0` as false.

## code

| ID | Function | Calling class | Scheduling | Guard eligible | C entry | Traits |
| --- | --- | --- | --- | --- | --- | --- |
| `vm_code_coverage_support_0` | `coverage_support/0` | regular | normal | no | `code_coverage_support_0` | — |
| `vm_code_get_coverage_2` | `get_coverage/2` | regular | normal | no | `code_get_coverage_2` | — |
| `vm_code_get_coverage_mode_0` | `get_coverage_mode/0` | regular | normal | no | `code_get_coverage_mode_0` | — |
| `vm_code_get_coverage_mode_1` | `get_coverage_mode/1` | regular | normal | no | `code_get_coverage_mode_1` | — |
| `vm_code_get_debug_info_1` | `get_debug_info/1` | regular | normal | no | `code_get_debug_info_1` | — |
| `vm_code_reset_coverage_1` | `reset_coverage/1` | regular | normal | no | `code_reset_coverage_1` | — |
| `vm_code_set_coverage_mode_1` | `set_coverage_mode/1` | regular | normal | no | `code_set_coverage_mode_1` | — |

## erl_ddll

| ID | Function | Calling class | Scheduling | Guard eligible | C entry | Traits |
| --- | --- | --- | --- | --- | --- | --- |
| `vm_erl_ddll_demonitor_1` | `demonitor/1` | regular | normal | no | `erl_ddll_demonitor_1` | — |
| `vm_erl_ddll_format_error_int_1` | `format_error_int/1` | regular | normal | no | `erl_ddll_format_error_int_1` | — |
| `vm_erl_ddll_info_2` | `info/2` | regular | normal | no | `erl_ddll_info_2` | — |
| `vm_erl_ddll_loaded_drivers_0` | `loaded_drivers/0` | regular | normal | no | `erl_ddll_loaded_drivers_0` | — |
| `vm_erl_ddll_monitor_2` | `monitor/2` | regular | normal | no | `erl_ddll_monitor_2` | — |
| `vm_erl_ddll_try_load_3` | `try_load/3` | regular | normal | no | `erl_ddll_try_load_3` | — |
| `vm_erl_ddll_try_unload_2` | `try_unload/2` | regular | normal | no | `erl_ddll_try_unload_2` | — |

## erl_debugger

| ID | Function | Calling class | Scheduling | Guard eligible | C entry | Traits |
| --- | --- | --- | --- | --- | --- | --- |
| `vm_erl_debugger_breakpoint_3` | `breakpoint/3` | regular | normal | no | `erl_debugger_breakpoint_3` | — |
| `vm_erl_debugger_breakpoints_1` | `breakpoints/1` | regular | normal | no | `erl_debugger_breakpoints_1` | — |
| `vm_erl_debugger_breakpoints_3` | `breakpoints/3` | regular | normal | no | `erl_debugger_breakpoints_3` | — |
| `vm_erl_debugger_instrumentations_0` | `instrumentations/0` | regular | normal | no | `erl_debugger_instrumentations_0` | — |
| `vm_erl_debugger_peek_stack_frame_slot_4` | `peek_stack_frame_slot/4` | regular | normal | no | `erl_debugger_peek_stack_frame_slot_4` | — |
| `vm_erl_debugger_peek_xreg_3` | `peek_xreg/3` | regular | normal | no | `erl_debugger_peek_xreg_3` | — |
| `vm_erl_debugger_register_1` | `register/1` | regular | normal | no | `erl_debugger_register_1` | — |
| `vm_erl_debugger_stack_frames_2` | `stack_frames/2` | regular | normal | no | `erl_debugger_stack_frames_2` | — |
| `vm_erl_debugger_supported_0` | `supported/0` | regular | normal | no | `erl_debugger_supported_0` | — |
| `vm_erl_debugger_toggle_instrumentations_1` | `toggle_instrumentations/1` | regular | normal | no | `erl_debugger_toggle_instrumentations_1` | — |
| `vm_erl_debugger_unregister_2` | `unregister/2` | regular | normal | no | `erl_debugger_unregister_2` | — |
| `vm_erl_debugger_whereis_0` | `whereis/0` | regular | normal | no | `erl_debugger_whereis_0` | — |
| `vm_erl_debugger_xregs_count_1` | `xregs_count/1` | regular | normal | no | `erl_debugger_xregs_count_1` | — |

## erlang: guard and operator

Default module: `beam` for modern hosts; `erlang` for canonical hosts.

| ID | Function | Calling class | Scheduling | Guard eligible | C entry | Traits |
| --- | --- | --- | --- | --- | --- | --- |
| `vm_stimes_2` | `*/2` | guard/operator | normal | yes | `stimes_2` | — |
| `vm_splus_1` | `+/1` | guard/operator | normal | yes | `splus_1` | — |
| `vm_splus_2` | `+/2` | guard/operator | normal | yes | `splus_2` | — |
| `vm_sminus_1` | `-/1` | guard/operator | normal | yes | `sminus_1` | — |
| `vm_sminus_2` | `-/2` | guard/operator | normal | yes | `sminus_2` | — |
| `vm_div_2` | `//2` | guard/operator | normal | yes | `div_2` | — |
| `vm_sneqeq_2` | `/=/2` | guard/operator | normal | yes | `sneqeq_2` | NOFAIL |
| `vm_slt_2` | `</2` | guard/operator | normal | yes | `slt_2` | NOFAIL, LT |
| `vm_sneq_2` | `=/=/2` | guard/operator | normal | yes | `sneq_2` | NOFAIL, NE_EXACT |
| `vm_seq_2` | `=:=/2` | guard/operator | normal | yes | `seq_2` | NOFAIL, EQ_EXACT |
| `vm_sle_2` | `=</2` | guard/operator | normal | yes | `sle_2` | NOFAIL |
| `vm_seqeq_2` | `==/2` | guard/operator | normal | yes | `seqeq_2` | NOFAIL |
| `vm_sgt_2` | `>/2` | guard/operator | normal | yes | `sgt_2` | NOFAIL |
| `vm_sge_2` | `>=/2` | guard/operator | normal | yes | `sge_2` | NOFAIL, GE |
| `vm_abs_1` | `abs/1` | guard/operator | normal | yes | `abs_1` | — |
| `vm_and_2` | `and/2` | guard/operator | normal | yes | `and_2` | — |
| `vm_band_2` | `band/2` | guard/operator | normal | yes | `band_2` | — |
| `vm_binary_part_2` | `binary_part/2` | guard/operator | normal | yes | `binary_part_2` | — |
| `vm_binary_part_3` | `binary_part/3` | guard/operator | normal | yes | `binary_part_3` | — |
| `vm_binary_to_atom_2` | `binary_to_atom/2` | guard/operator | normal | no | `binary_to_atom_2` | — |
| `vm_binary_to_existing_atom_2` | `binary_to_existing_atom/2` | guard/operator | normal | no | `binary_to_existing_atom_2` | — |
| `vm_bit_size_1` | `bit_size/1` | guard/operator | normal | yes | `bit_size_1` | — |
| `vm_bnot_1` | `bnot/1` | guard/operator | normal | yes | `bnot_1` | — |
| `vm_bor_2` | `bor/2` | guard/operator | normal | yes | `bor_2` | — |
| `vm_bsl_2` | `bsl/2` | guard/operator | normal | yes | `bsl_2` | — |
| `vm_bsr_2` | `bsr/2` | guard/operator | normal | yes | `bsr_2` | — |
| `vm_bxor_2` | `bxor/2` | guard/operator | normal | yes | `bxor_2` | — |
| `vm_byte_size_1` | `byte_size/1` | guard/operator | normal | yes | `byte_size_1` | — |
| `vm_ceil_1` | `ceil/1` | guard/operator | normal | yes | `ceil_1` | — |
| `vm_intdiv_2` | `div/2` | guard/operator | normal | yes | `intdiv_2` | — |
| `vm_element_2` | `element/2` | guard/operator | normal | yes | `element_2` | — |
| `vm_float_1` | `float/1` | guard/operator | normal | yes | `float_1` | — |
| `vm_floor_1` | `floor/1` | guard/operator | normal | yes | `floor_1` | — |
| `vm_hd_1` | `hd/1` | guard/operator | normal | yes | `hd_1` | — |
| `vm_is_atom_1` | `is_atom/1` | guard/operator | normal | yes | `is_atom_1` | NOFAIL |
| `vm_is_binary_1` | `is_binary/1` | guard/operator | normal | yes | `is_binary_1` | NOFAIL |
| `vm_is_bitstring_1` | `is_bitstring/1` | guard/operator | normal | yes | `is_bitstring_1` | NOFAIL |
| `vm_is_boolean_1` | `is_boolean/1` | guard/operator | normal | yes | `is_boolean_1` | NOFAIL |
| `vm_is_float_1` | `is_float/1` | guard/operator | normal | yes | `is_float_1` | NOFAIL |
| `vm_is_function_1` | `is_function/1` | guard/operator | normal | yes | `is_function_1` | NOFAIL |
| `vm_is_function_2` | `is_function/2` | guard/operator | normal | yes | `is_function_2` | — |
| `vm_is_integer_1` | `is_integer/1` | guard/operator | normal | yes | `is_integer_1` | NOFAIL |
| `vm_is_integer_3` | `is_integer/3` | guard/operator | normal | yes | `is_integer_3` | — |
| `vm_is_list_1` | `is_list/1` | guard/operator | normal | yes | `is_list_1` | NOFAIL |
| `vm_is_map_1` | `is_map/1` | guard/operator | normal | yes | `is_map_1` | NOFAIL |
| `vm_is_map_key_2` | `is_map_key/2` | guard/operator | normal | yes | `is_map_key_2` | — |
| `vm_is_number_1` | `is_number/1` | guard/operator | normal | yes | `is_number_1` | NOFAIL |
| `vm_is_pid_1` | `is_pid/1` | guard/operator | normal | yes | `is_pid_1` | NOFAIL |
| `vm_is_port_1` | `is_port/1` | guard/operator | normal | yes | `is_port_1` | NOFAIL |
| `vm_is_record_1` | `is_record/1` | guard/operator | normal | yes | `is_record_1` | — |
| `vm_is_record_2` | `is_record/2` | guard/operator | normal | yes | `is_record_2` | — |
| `vm_is_record_3` | `is_record/3` | guard/operator | normal | yes | `is_record_3` | — |
| `vm_is_reference_1` | `is_reference/1` | guard/operator | normal | yes | `is_reference_1` | NOFAIL |
| `vm_is_tuple_1` | `is_tuple/1` | guard/operator | normal | yes | `is_tuple_1` | NOFAIL |
| `vm_length_1` | `length/1` | guard/operator | normal | yes | `length_1` | — |
| `vm_list_to_atom_1` | `list_to_atom/1` | guard/operator | normal | no | `list_to_atom_1` | — |
| `vm_list_to_existing_atom_1` | `list_to_existing_atom/1` | guard/operator | normal | no | `list_to_existing_atom_1` | — |
| `vm_map_get_2` | `map_get/2` | guard/operator | normal | yes | `map_get_2` | — |
| `vm_map_size_1` | `map_size/1` | guard/operator | normal | yes | `map_size_1` | — |
| `vm_max_2` | `max/2` | guard/operator | normal | yes | `max_2` | — |
| `vm_min_2` | `min/2` | guard/operator | normal | yes | `min_2` | — |
| `vm_node_0` | `node/0` | guard/operator | normal | yes | `node_0` | — |
| `vm_node_1` | `node/1` | guard/operator | normal | yes | `node_1` | — |
| `vm_not_1` | `not/1` | guard/operator | normal | yes | `not_1` | — |
| `vm_or_2` | `or/2` | guard/operator | normal | yes | `or_2` | — |
| `vm_rem_2` | `rem/2` | guard/operator | normal | yes | `rem_2` | — |
| `vm_round_1` | `round/1` | guard/operator | normal | yes | `round_1` | — |
| `vm_self_0` | `self/0` | guard/operator | normal | yes | `self_0` | — |
| `vm_size_1` | `size/1` | guard/operator | normal | yes | `size_1` | — |
| `vm_tl_1` | `tl/1` | guard/operator | normal | yes | `tl_1` | — |
| `vm_trunc_1` | `trunc/1` | guard/operator | normal | yes | `trunc_1` | — |
| `vm_tuple_size_1` | `tuple_size/1` | guard/operator | normal | yes | `tuple_size_1` | — |
| `vm_xor_2` | `xor/2` | guard/operator | normal | yes | `xor_2` | — |

## erlang: heavy

Default module: `beam` for modern hosts; `erlang` for canonical hosts.

| ID | Function | Calling class | Scheduling | Guard eligible | C entry | Traits |
| --- | --- | --- | --- | --- | --- | --- |
| `vm_erase_1` | `erase/1` | heavy | normal | no | `erase_1` | — |
| `vm_hibernate_3` | `hibernate/3` | heavy | normal | no | `hibernate_3` | — |
| `vm_process_info_1` | `process_info/1` | heavy | normal | no | `process_info_1` | — |
| `vm_process_info_2` | `process_info/2` | heavy | normal | no | `process_info_2` | — |
| `vm_put_2` | `put/2` | heavy | normal | no | `put_2` | — |

## erlang: regular

Default module: `beam` for modern hosts; `erlang` for canonical hosts.

| ID | Function | Calling class | Scheduling | Guard eligible | C entry | Traits |
| --- | --- | --- | --- | --- | --- | --- |
| `vm_ebif_bang_2` | `!/2` | regular | normal | no | `ebif_bang_2` | — |
| `vm_ebif_plusplus_2` | `++/2` | regular | normal | no | `ebif_plusplus_2` | — |
| `vm_ebif_minusminus_2` | `--/2` | regular | normal | no | `ebif_minusminus_2` | — |
| `vm_adler32_1` | `adler32/1` | regular | normal | no | `adler32_1` | — |
| `vm_adler32_2` | `adler32/2` | regular | normal | no | `adler32_2` | — |
| `vm_adler32_combine_3` | `adler32_combine/3` | regular | normal | no | `adler32_combine_3` | — |
| `vm_alias_1` | `alias/1` | regular | normal | no | `alias_1` | — |
| `vm_append_2` | `append/2` | regular | normal | no | `append_2` | — |
| `vm_append_element_2` | `append_element/2` | regular | dirty CPU in dirty-test builds | no | `append_element_2` | — |
| `vm_atom_to_binary_2` | `atom_to_binary/2` | regular | normal | no | `atom_to_binary_2` | — |
| `vm_atom_to_list_1` | `atom_to_list/1` | regular | dirty CPU in dirty-test builds | no | `atom_to_list_1` | — |
| `vm_binary_to_float_1` | `binary_to_float/1` | regular | dirty CPU in dirty-test builds | no | `binary_to_float_1` | — |
| `vm_binary_to_list_1` | `binary_to_list/1` | regular | normal | no | `binary_to_list_1` | — |
| `vm_binary_to_list_3` | `binary_to_list/3` | regular | normal | no | `binary_to_list_3` | — |
| `vm_binary_to_term_1` | `binary_to_term/1` | regular | normal | no | `binary_to_term_1` | — |
| `vm_binary_to_term_2` | `binary_to_term/2` | regular | normal | no | `binary_to_term_2` | — |
| `vm_bitstring_to_list_1` | `bitstring_to_list/1` | regular | normal | no | `bitstring_to_list_1` | — |
| `vm_bump_reductions_1` | `bump_reductions/1` | regular | normal | no | `bump_reductions_1` | — |
| `vm_call_on_load_function_1` | `call_on_load_function/1` | regular | normal | no | `call_on_load_function_1` | — |
| `vm_check_old_code_1` | `check_old_code/1` | regular | normal | no | `check_old_code_1` | — |
| `vm_crc32_1` | `crc32/1` | regular | normal | no | `crc32_1` | — |
| `vm_crc32_2` | `crc32/2` | regular | normal | no | `crc32_2` | — |
| `vm_crc32_combine_3` | `crc32_combine/3` | regular | normal | no | `crc32_combine_3` | — |
| `vm_date_0` | `date/0` | regular | normal | no | `date_0` | — |
| `vm_decode_packet_3` | `decode_packet/3` | regular | normal | no | `decode_packet_3` | — |
| `vm_delete_element_2` | `delete_element/2` | regular | dirty CPU in dirty-test builds | no | `delete_element_2` | — |
| `vm_delete_module_1` | `delete_module/1` | regular | normal | no | `delete_module_1` | — |
| `vm_demonitor_1` | `demonitor/1` | regular | normal | no | `demonitor_1` | — |
| `vm_demonitor_2` | `demonitor/2` | regular | normal | no | `demonitor_2` | — |
| `vm_display_1` | `display/1` | regular | dirty CPU in dirty-test builds | no | `display_1` | — |
| `vm_display_string_2` | `display_string/2` | regular | dirty CPU | no | `display_string_2` | — |
| `vm_dist_ctrl_get_data_1` | `dist_ctrl_get_data/1` | regular | normal | no | `dist_ctrl_get_data_1` | — |
| `vm_dist_ctrl_get_data_notification_1` | `dist_ctrl_get_data_notification/1` | regular | normal | no | `dist_ctrl_get_data_notification_1` | — |
| `vm_dist_ctrl_get_opt_2` | `dist_ctrl_get_opt/2` | regular | normal | no | `dist_ctrl_get_opt_2` | — |
| `vm_dist_ctrl_input_handler_2` | `dist_ctrl_input_handler/2` | regular | normal | no | `dist_ctrl_input_handler_2` | — |
| `vm_dist_ctrl_put_data_2` | `dist_ctrl_put_data/2` | regular | normal | no | `dist_ctrl_put_data_2` | — |
| `vm_dist_ctrl_set_opt_3` | `dist_ctrl_set_opt/3` | regular | normal | no | `dist_ctrl_set_opt_3` | — |
| `vm_dist_get_stat_1` | `dist_get_stat/1` | regular | normal | no | `dist_get_stat_1` | — |
| `vm_dt_append_vm_tag_data_1` | `dt_append_vm_tag_data/1` | regular | normal | no | `dt_append_vm_tag_data_1` | — |
| `vm_dt_get_tag_0` | `dt_get_tag/0` | regular | normal | no | `dt_get_tag_0` | — |
| `vm_dt_get_tag_data_0` | `dt_get_tag_data/0` | regular | normal | no | `dt_get_tag_data_0` | — |
| `vm_dt_prepend_vm_tag_data_1` | `dt_prepend_vm_tag_data/1` | regular | normal | no | `dt_prepend_vm_tag_data_1` | — |
| `vm_dt_put_tag_1` | `dt_put_tag/1` | regular | normal | no | `dt_put_tag_1` | — |
| `vm_dt_restore_tag_1` | `dt_restore_tag/1` | regular | normal | no | `dt_restore_tag_1` | — |
| `vm_dt_spread_tag_1` | `dt_spread_tag/1` | regular | normal | no | `dt_spread_tag_1` | — |
| `vm_erase_0` | `erase/0` | regular | normal | no | `erase_0` | — |
| `vm_error_1` | `error/1` | regular | normal | no | `error_1` | NO_RETURN, HIDE_EXCEPTION_FRAME |
| `vm_error_2` | `error/2` | regular | normal | no | `error_2` | NO_RETURN, HIDE_EXCEPTION_FRAME |
| `vm_error_3` | `error/3` | regular | normal | no | `error_3` | NO_RETURN, HIDE_EXCEPTION_FRAME |
| `vm_exit_1` | `exit/1` | regular | normal | no | `exit_1` | NO_RETURN, HIDE_EXCEPTION_FRAME |
| `vm_exit_2` | `exit/2` | regular | normal | no | `exit_2` | — |
| `vm_exit_3` | `exit/3` | regular | normal | no | `exit_3` | — |
| `vm_exit_signal_2` | `exit_signal/2` | regular | normal | no | `exit_signal_2` | — |
| `vm_exit_signal_3` | `exit_signal/3` | regular | normal | no | `exit_signal_3` | — |
| `vm_external_size_1` | `external_size/1` | regular | normal | no | `external_size_1` | — |
| `vm_external_size_2` | `external_size/2` | regular | normal | no | `external_size_2` | — |
| `vm_finish_after_on_load_2` | `finish_after_on_load/2` | regular | normal | no | `finish_after_on_load_2` | — |
| `vm_finish_loading_1` | `finish_loading/1` | regular | normal | no | `finish_loading_1` | — |
| `vm_float_to_binary_1` | `float_to_binary/1` | regular | dirty CPU in dirty-test builds | no | `float_to_binary_1` | — |
| `vm_float_to_binary_2` | `float_to_binary/2` | regular | dirty CPU in dirty-test builds | no | `float_to_binary_2` | — |
| `vm_float_to_list_1` | `float_to_list/1` | regular | dirty CPU in dirty-test builds | no | `float_to_list_1` | — |
| `vm_float_to_list_2` | `float_to_list/2` | regular | dirty CPU in dirty-test builds | no | `float_to_list_2` | — |
| `vm_fun_info_2` | `fun_info/2` | regular | normal | no | `fun_info_2` | — |
| `vm_fun_info_mfa_1` | `fun_info_mfa/1` | regular | normal | no | `fun_info_mfa_1` | — |
| `vm_fun_to_list_1` | `fun_to_list/1` | regular | normal | no | `fun_to_list_1` | — |
| `vm_function_exported_3` | `function_exported/3` | regular | normal | no | `function_exported_3` | — |
| `vm_get_0` | `get/0` | regular | normal | no | `get_0` | — |
| `vm_get_1` | `get/1` | regular | normal | no | `get_1` | — |
| `vm_get_keys_0` | `get_keys/0` | regular | normal | no | `get_keys_0` | — |
| `vm_get_keys_1` | `get_keys/1` | regular | normal | no | `get_keys_1` | — |
| `vm_get_module_info_1` | `get_module_info/1` | regular | normal | no | `get_module_info_1` | — |
| `vm_get_module_info_2` | `get_module_info/2` | regular | normal | no | `get_module_info_2` | — |
| `vm_group_leader_0` | `group_leader/0` | regular | normal | no | `group_leader_0` | — |
| `vm_halt_2` | `halt/2` | regular | normal | no | `halt_2` | — |
| `vm_has_prepared_code_on_load_1` | `has_prepared_code_on_load/1` | regular | normal | no | `has_prepared_code_on_load_1` | — |
| `vm_insert_element_3` | `insert_element/3` | regular | dirty CPU in dirty-test builds | no | `insert_element_3` | — |
| `vm_integer_to_binary_1` | `integer_to_binary/1` | regular | normal | no | `integer_to_binary_1` | — |
| `vm_integer_to_binary_2` | `integer_to_binary/2` | regular | normal | no | `integer_to_binary_2` | — |
| `vm_integer_to_list_1` | `integer_to_list/1` | regular | dirty CPU in dirty-test builds | no | `integer_to_list_1` | — |
| `vm_integer_to_list_2` | `integer_to_list/2` | regular | normal | no | `integer_to_list_2` | — |
| `vm_iolist_size_1` | `iolist_size/1` | regular | normal | no | `iolist_size_1` | — |
| `vm_iolist_to_binary_1` | `iolist_to_binary/1` | regular | normal | no | `iolist_to_binary_1` | — |
| `vm_iolist_to_iovec_1` | `iolist_to_iovec/1` | regular | normal | no | `iolist_to_iovec_1` | — |
| `vm_is_builtin_3` | `is_builtin/3` | regular | normal | no | `is_builtin_3` | — |
| `vm_is_process_alive_1` | `is_process_alive/1` | regular | normal | no | `is_process_alive_1` | — |
| `vm_link_1` | `link/1` | regular | normal | no | `link_1` | — |
| `vm_link_2` | `link/2` | regular | normal | no | `link_2` | — |
| `vm_list_to_binary_1` | `list_to_binary/1` | regular | normal | no | `list_to_binary_1` | — |
| `vm_list_to_bitstring_1` | `list_to_bitstring/1` | regular | normal | no | `list_to_bitstring_1` | — |
| `vm_list_to_float_1` | `list_to_float/1` | regular | dirty CPU in dirty-test builds | no | `list_to_float_1` | — |
| `vm_list_to_pid_1` | `list_to_pid/1` | regular | normal | no | `list_to_pid_1` | — |
| `vm_list_to_port_1` | `list_to_port/1` | regular | normal | no | `list_to_port_1` | — |
| `vm_list_to_ref_1` | `list_to_ref/1` | regular | normal | no | `list_to_ref_1` | — |
| `vm_list_to_tuple_1` | `list_to_tuple/1` | regular | dirty CPU in dirty-test builds | no | `list_to_tuple_1` | — |
| `vm_load_nif_2` | `load_nif/2` | regular | normal | no | `load_nif_2` | — |
| `vm_loaded_0` | `loaded/0` | regular | normal | no | `loaded_0` | — |
| `vm_localtime_0` | `localtime/0` | regular | normal | no | `localtime_0` | — |
| `vm_localtime_to_universaltime_2` | `localtime_to_universaltime/2` | regular | normal | no | `localtime_to_universaltime_2` | — |
| `vm_make_fun_3` | `make_fun/3` | regular | normal | no | `make_fun_3` | — |
| `vm_make_ref_0` | `make_ref/0` | regular | normal | no | `make_ref_0` | — |
| `vm_make_tuple_2` | `make_tuple/2` | regular | dirty CPU in dirty-test builds | no | `make_tuple_2` | — |
| `vm_make_tuple_3` | `make_tuple/3` | regular | dirty CPU in dirty-test builds | no | `make_tuple_3` | — |
| `vm_match_spec_test_3` | `match_spec_test/3` | regular | normal | no | `match_spec_test_3` | — |
| `vm_md5_1` | `md5/1` | regular | normal | no | `md5_1` | — |
| `vm_md5_final_1` | `md5_final/1` | regular | normal | no | `md5_final_1` | — |
| `vm_md5_init_0` | `md5_init/0` | regular | normal | no | `md5_init_0` | — |
| `vm_md5_update_2` | `md5_update/2` | regular | normal | no | `md5_update_2` | — |
| `vm_module_loaded_1` | `module_loaded/1` | regular | normal | no | `module_loaded_1` | — |
| `vm_monitor_2` | `monitor/2` | regular | normal | no | `monitor_2` | — |
| `vm_monitor_3` | `monitor/3` | regular | normal | no | `monitor_3` | — |
| `vm_monitor_node_2` | `monitor_node/2` | regular | normal | no | `monitor_node_2` | — |
| `vm_monitor_node_3` | `monitor_node/3` | regular | normal | no | `monitor_node_3` | — |
| `vm_nif_error_1` | `nif_error/1` | regular | normal | no | `nif_error_1` | NO_RETURN |
| `vm_nif_error_2` | `nif_error/2` | regular | normal | no | `nif_error_2` | NO_RETURN |
| `vm_nodes_0` | `nodes/0` | regular | normal | no | `nodes_0` | — |
| `vm_nodes_1` | `nodes/1` | regular | normal | no | `nodes_1` | — |
| `vm_nodes_2` | `nodes/2` | regular | normal | no | `nodes_2` | — |
| `vm_phash_2` | `phash/2` | regular | normal | no | `phash_2` | — |
| `vm_phash2_1` | `phash2/1` | regular | normal | no | `phash2_1` | — |
| `vm_phash2_2` | `phash2/2` | regular | normal | no | `phash2_2` | — |
| `vm_pid_to_list_1` | `pid_to_list/1` | regular | normal | no | `pid_to_list_1` | — |
| `vm_port_get_data_1` | `port_get_data/1` | regular | normal | no | `port_get_data_1` | — |
| `vm_port_set_data_2` | `port_set_data/2` | regular | normal | no | `port_set_data_2` | — |
| `vm_port_to_list_1` | `port_to_list/1` | regular | normal | no | `port_to_list_1` | — |
| `vm_ports_0` | `ports/0` | regular | normal | no | `ports_0` | — |
| `vm_posixtime_to_universaltime_1` | `posixtime_to_universaltime/1` | regular | normal | no | `posixtime_to_universaltime_1` | — |
| `vm_pre_loaded_0` | `pre_loaded/0` | regular | normal | no | `pre_loaded_0` | — |
| `vm_process_flag_2` | `process_flag/2` | regular | normal | no | `process_flag_2` | — |
| `vm_processes_0` | `processes/0` | regular | normal | no | `processes_0` | — |
| `vm_raise_3` | `raise/3` | regular | normal | no | `raise_3` | — |
| `vm_ref_to_list_1` | `ref_to_list/1` | regular | normal | no | `ref_to_list_1` | — |
| `vm_register_2` | `register/2` | regular | normal | no | `register_2` | — |
| `vm_registered_0` | `registered/0` | regular | normal | no | `registered_0` | — |
| `vm_resume_process_1` | `resume_process/1` | regular | normal | no | `resume_process_1` | — |
| `vm_send_2` | `send/2` | regular | normal | no | `send_2` | — |
| `vm_send_3` | `send/3` | regular | normal | no | `send_3` | — |
| `vm_seq_trace_2` | `seq_trace/2` | regular | normal | no | `seq_trace_2` | — |
| `vm_seq_trace_info_1` | `seq_trace_info/1` | regular | normal | no | `seq_trace_info_1` | — |
| `vm_seq_trace_print_1` | `seq_trace_print/1` | regular | normal | no | `seq_trace_print_1` | — |
| `vm_seq_trace_print_2` | `seq_trace_print/2` | regular | normal | no | `seq_trace_print_2` | — |
| `vm_setelement_3` | `setelement/3` | regular | normal | no | `setelement_3` | — |
| `vm_setnode_2` | `setnode/2` | regular | normal | no | `setnode_2` | — |
| `vm_spawn_3` | `spawn/3` | regular | normal | no | `spawn_3` | — |
| `vm_spawn_link_3` | `spawn_link/3` | regular | normal | no | `spawn_link_3` | — |
| `vm_spawn_opt_4` | `spawn_opt/4` | regular | normal | no | `spawn_opt_4` | — |
| `vm_spawn_request_abandon_1` | `spawn_request_abandon/1` | regular | normal | no | `spawn_request_abandon_1` | — |
| `vm_split_binary_2` | `split_binary/2` | regular | normal | no | `split_binary_2` | — |
| `vm_statistics_1` | `statistics/1` | regular | normal | no | `statistics_1` | — |
| `vm_subtract_2` | `subtract/2` | regular | normal | no | `subtract_2` | — |
| `vm_system_flag_2` | `system_flag/2` | regular | normal | no | `system_flag_2` | — |
| `vm_system_info_1` | `system_info/1` | regular | normal | no | `system_info_1` | — |
| `vm_system_profile_0` | `system_profile/0` | regular | normal | no | `system_profile_0` | — |
| `vm_system_profile_2` | `system_profile/2` | regular | normal | no | `system_profile_2` | — |
| `vm_term_to_binary_1` | `term_to_binary/1` | regular | normal | no | `term_to_binary_1` | — |
| `vm_term_to_binary_2` | `term_to_binary/2` | regular | normal | no | `term_to_binary_2` | — |
| `vm_term_to_iovec_1` | `term_to_iovec/1` | regular | normal | no | `term_to_iovec_1` | — |
| `vm_term_to_iovec_2` | `term_to_iovec/2` | regular | normal | no | `term_to_iovec_2` | — |
| `vm_throw_1` | `throw/1` | regular | normal | no | `throw_1` | NO_RETURN, HIDE_EXCEPTION_FRAME |
| `vm_time_0` | `time/0` | regular | normal | no | `time_0` | — |
| `vm_trace_delivered_1` | `trace_delivered/1` | regular | normal | no | `trace_delivered_1` | — |
| `vm_trace_info_2` | `trace_info/2` | regular | normal | no | `trace_info_2` | — |
| `vm_tuple_to_list_1` | `tuple_to_list/1` | regular | dirty CPU in dirty-test builds | no | `tuple_to_list_1` | — |
| `vm_unalias_1` | `unalias/1` | regular | normal | no | `unalias_1` | — |
| `vm_unique_integer_0` | `unique_integer/0` | regular | normal | no | `unique_integer_0` | — |
| `vm_unique_integer_1` | `unique_integer/1` | regular | normal | no | `unique_integer_1` | — |
| `vm_universaltime_0` | `universaltime/0` | regular | normal | no | `universaltime_0` | — |
| `vm_universaltime_to_localtime_1` | `universaltime_to_localtime/1` | regular | normal | no | `universaltime_to_localtime_1` | — |
| `vm_universaltime_to_posixtime_1` | `universaltime_to_posixtime/1` | regular | normal | no | `universaltime_to_posixtime_1` | — |
| `vm_unlink_1` | `unlink/1` | regular | normal | no | `unlink_1` | — |
| `vm_unregister_1` | `unregister/1` | regular | normal | no | `unregister_1` | — |
| `vm_whereis_1` | `whereis/1` | regular | normal | no | `whereis_1` | — |

## error_logger

| ID | Function | Calling class | Scheduling | Guard eligible | C entry | Traits |
| --- | --- | --- | --- | --- | --- | --- |
| `vm_error_logger_warning_map_0` | `warning_map/0` | regular | normal | no | `error_logger_warning_map_0` | — |

## erts_debug

| ID | Function | Calling class | Scheduling | Guard eligible | C entry | Traits |
| --- | --- | --- | --- | --- | --- | --- |
| `vm_erts_debug_breakpoint_2` | `breakpoint/2` | regular | normal | no | `erts_debug_breakpoint_2` | — |
| `vm_erts_debug_copy_shared_2` | `copy_shared/2` | regular | normal | no | `erts_debug_copy_shared_2` | — |
| `vm_erts_debug_dirty_3` | `dirty/3` | regular | normal | no | `erts_debug_dirty_3` | — |
| `vm_erts_debug_dirty_cpu_2` | `dirty_cpu/2` | regular | dirty CPU | no | `erts_debug_dirty_cpu_2` | — |
| `vm_erts_debug_dirty_io_2` | `dirty_io/2` | regular | dirty I/O | no | `erts_debug_dirty_io_2` | — |
| `vm_erts_debug_disassemble_1` | `disassemble/1` | regular | normal | no | `erts_debug_disassemble_1` | — |
| `vm_erts_debug_dist_ext_to_term_2` | `dist_ext_to_term/2` | regular | normal | no | `erts_debug_dist_ext_to_term_2` | — |
| `vm_erts_debug_flat_size_1` | `flat_size/1` | regular | normal | no | `erts_debug_flat_size_1` | — |
| `vm_erts_debug_get_internal_state_1` | `get_internal_state/1` | regular | normal | no | `erts_debug_get_internal_state_1` | — |
| `vm_erts_debug_instructions_0` | `instructions/0` | regular | normal | no | `erts_debug_instructions_0` | — |
| `vm_erts_debug_interpreter_size_0` | `interpreter_size/0` | regular | normal | no | `erts_debug_interpreter_size_0` | — |
| `vm_erts_debug_lcnt_clear_0` | `lcnt_clear/0` | regular | dirty CPU | no | `erts_debug_lcnt_clear_0` | — |
| `vm_erts_debug_lcnt_collect_0` | `lcnt_collect/0` | regular | dirty CPU | no | `erts_debug_lcnt_collect_0` | — |
| `vm_erts_debug_lcnt_control_1` | `lcnt_control/1` | regular | normal | no | `erts_debug_lcnt_control_1` | — |
| `vm_erts_debug_lcnt_control_2` | `lcnt_control/2` | regular | dirty CPU | no | `erts_debug_lcnt_control_2` | — |
| `vm_erts_debug_map_info_1` | `map_info/1` | regular | normal | no | `erts_debug_map_info_1` | — |
| `vm_erts_debug_same_2` | `same/2` | regular | normal | no | `erts_debug_same_2` | — |
| `vm_erts_debug_set_internal_state_2` | `set_internal_state/2` | regular | normal | no | `erts_debug_set_internal_state_2` | — |
| `vm_erts_debug_size_shared_1` | `size_shared/1` | regular | normal | no | `erts_debug_size_shared_1` | — |
| `vm_erts_debug_unaligned_bitstring_2` | `unaligned_bitstring/2` | regular | normal | no | `erts_debug_unaligned_bitstring_2` | — |

## erts_internal

| ID | Function | Calling class | Scheduling | Guard eligible | C entry | Traits |
| --- | --- | --- | --- | --- | --- | --- |
| `vm_erts_internal_abort_pending_connection_2` | `abort_pending_connection/2` | regular | normal | no | `erts_internal_abort_pending_connection_2` | — |
| `vm_erts_internal_atomics_new_2` | `atomics_new/2` | regular | normal | no | `erts_internal_atomics_new_2` | — |
| `vm_erts_internal_beamfile_chunk_2` | `beamfile_chunk/2` | regular | normal | no | `erts_internal_beamfile_chunk_2` | — |
| `vm_erts_internal_beamfile_gunzip_1` | `beamfile_gunzip/1` | regular | normal | no | `erts_internal_beamfile_gunzip_1` | — |
| `vm_erts_internal_beamfile_module_md5_1` | `beamfile_module_md5/1` | regular | normal | no | `erts_internal_beamfile_module_md5_1` | — |
| `vm_erts_internal_binary_to_integer_2` | `binary_to_integer/2` | regular | normal | no | `erts_internal_binary_to_integer_2` | — |
| `vm_erts_internal_check_dirty_process_code_2` | `check_dirty_process_code/2` | regular | normal | no | `erts_internal_check_dirty_process_code_2` | — |
| `vm_erts_internal_check_process_code_1` | `check_process_code/1` | heavy | normal | no | `erts_internal_check_process_code_1` | — |
| `vm_erts_internal_cmp_term_2` | `cmp_term/2` | regular | normal | no | `erts_internal_cmp_term_2` | — |
| `vm_erts_internal_counters_add_3` | `counters_add/3` | regular | normal | no | `erts_internal_counters_add_3` | — |
| `vm_erts_internal_counters_get_2` | `counters_get/2` | regular | normal | no | `erts_internal_counters_get_2` | — |
| `vm_erts_internal_counters_info_1` | `counters_info/1` | regular | normal | no | `erts_internal_counters_info_1` | — |
| `vm_erts_internal_counters_new_1` | `counters_new/1` | regular | normal | no | `erts_internal_counters_new_1` | — |
| `vm_erts_internal_counters_put_3` | `counters_put/3` | regular | normal | no | `erts_internal_counters_put_3` | — |
| `vm_erts_internal_create_dist_channel_3` | `create_dist_channel/3` | regular | normal | no | `erts_internal_create_dist_channel_3` | — |
| `vm_erts_internal_dirty_process_handle_signals_1` | `dirty_process_handle_signals/1` | regular | normal | no | `erts_internal_dirty_process_handle_signals_1` | — |
| `vm_erts_internal_dist_spawn_request_4` | `dist_spawn_request/4` | regular | normal | no | `erts_internal_dist_spawn_request_4` | — |
| `vm_erts_internal_dynamic_node_name_0` | `dynamic_node_name/0` | regular | normal | no | `erts_internal_dynamic_node_name_0` | — |
| `vm_erts_internal_dynamic_node_name_1` | `dynamic_node_name/1` | regular | normal | no | `erts_internal_dynamic_node_name_1` | — |
| `vm_erts_internal_erase_persistent_terms_0` | `erase_persistent_terms/0` | regular | normal | no | `erts_internal_erase_persistent_terms_0` | — |
| `vm_erts_internal_ets_info_binary_1` | `ets_info_binary/1` | regular | normal | no | `erts_internal_ets_info_binary_1` | — |
| `vm_erts_internal_ets_lookup_binary_info_2` | `ets_lookup_binary_info/2` | regular | normal | no | `erts_internal_ets_lookup_binary_info_2` | — |
| `vm_erts_internal_ets_raw_first_1` | `ets_raw_first/1` | regular | normal | no | `erts_internal_ets_raw_first_1` | — |
| `vm_erts_internal_ets_raw_next_2` | `ets_raw_next/2` | regular | normal | no | `erts_internal_ets_raw_next_2` | — |
| `vm_erts_internal_ets_super_user_1` | `ets_super_user/1` | regular | normal | no | `erts_internal_ets_super_user_1` | — |
| `vm_erts_internal_filename_from_native_1` | `filename_from_native/1` | regular | normal | no | `erts_internal_filename_from_native_1` | — |
| `vm_erts_internal_filename_is_translatable_1` | `filename_is_translatable/1` | regular | normal | no | `erts_internal_filename_is_translatable_1` | — |
| `vm_erts_internal_filename_normalize_utf8_1` | `filename_normalize_utf8/1` | regular | normal | no | `erts_internal_filename_normalize_utf8_1` | — |
| `vm_erts_internal_filename_to_native_1` | `filename_to_native/1` | regular | normal | no | `erts_internal_filename_to_native_1` | — |
| `vm_erts_internal_garbage_collect_1` | `garbage_collect/1` | heavy | normal | no | `erts_internal_garbage_collect_1` | — |
| `vm_erts_internal_gather_alloc_histograms_1` | `gather_alloc_histograms/1` | regular | normal | no | `erts_internal_gather_alloc_histograms_1` | — |
| `vm_erts_internal_gather_carrier_info_1` | `gather_carrier_info/1` | regular | normal | no | `erts_internal_gather_carrier_info_1` | — |
| `vm_erts_internal_get_creation_0` | `get_creation/0` | regular | normal | no | `erts_internal_get_creation_0` | — |
| `vm_erts_internal_get_dflags_0` | `get_dflags/0` | regular | normal | no | `erts_internal_get_dflags_0` | — |
| `vm_erts_internal_group_leader_2` | `group_leader/2` | regular | normal | no | `erts_internal_group_leader_2` | — |
| `vm_erts_internal_group_leader_3` | `group_leader/3` | regular | normal | no | `erts_internal_group_leader_3` | — |
| `vm_erts_internal_host_request_1` | `host_request/1` | regular | normal | no | `erts_internal_host_request_1` | — |
| `vm_erts_internal_is_process_alive_2` | `is_process_alive/2` | regular | normal | no | `erts_internal_is_process_alive_2` | — |
| `vm_erts_internal_is_process_executing_dirty_1` | `is_process_executing_dirty/1` | regular | normal | no | `erts_internal_is_process_executing_dirty_1` | — |
| `vm_erts_internal_is_system_process_1` | `is_system_process/1` | regular | normal | no | `erts_internal_is_system_process_1` | — |
| `vm_erts_internal_list_to_integer_2` | `list_to_integer/2` | regular | normal | no | `erts_internal_list_to_integer_2` | — |
| `vm_erts_internal_map_hashmap_children_1` | `map_hashmap_children/1` | regular | normal | no | `erts_internal_map_hashmap_children_1` | — |
| `vm_erts_internal_map_next_3` | `map_next/3` | regular | normal | no | `erts_internal_map_next_3` | — |
| `vm_erts_internal_map_to_tuple_keys_1` | `map_to_tuple_keys/1` | regular | normal | no | `erts_internal_map_to_tuple_keys_1` | — |
| `vm_erts_internal_new_connection_1` | `new_connection/1` | regular | normal | no | `erts_internal_new_connection_1` | — |
| `vm_erts_internal_no_aux_work_threads_0` | `no_aux_work_threads/0` | regular | normal | no | `erts_internal_no_aux_work_threads_0` | — |
| `vm_erts_internal_notify_breakpoint_hit_3` | `notify_breakpoint_hit/3` | regular | normal | no | `erts_internal_notify_breakpoint_hit_3` | — |
| `vm_erts_internal_open_port_2` | `open_port/2` | regular | normal | no | `erts_internal_open_port_2` | — |
| `vm_erts_internal_perf_counter_unit_0` | `perf_counter_unit/0` | regular | normal | no | `erts_internal_perf_counter_unit_0` | — |
| `vm_erts_internal_port_call_3` | `port_call/3` | regular | normal | no | `erts_internal_port_call_3` | — |
| `vm_erts_internal_port_close_1` | `port_close/1` | regular | normal | no | `erts_internal_port_close_1` | — |
| `vm_erts_internal_port_command_3` | `port_command/3` | regular | normal | no | `erts_internal_port_command_3` | — |
| `vm_erts_internal_port_connect_2` | `port_connect/2` | regular | normal | no | `erts_internal_port_connect_2` | — |
| `vm_erts_internal_port_control_3` | `port_control/3` | regular | normal | no | `erts_internal_port_control_3` | — |
| `vm_erts_internal_port_info_1` | `port_info/1` | regular | normal | no | `erts_internal_port_info_1` | — |
| `vm_erts_internal_port_info_2` | `port_info/2` | regular | normal | no | `erts_internal_port_info_2` | — |
| `vm_erts_internal_prepare_loading_2` | `prepare_loading/2` | regular | normal | no | `erts_internal_prepare_loading_2` | — |
| `vm_erts_internal_process_display_2` | `process_display/2` | regular | normal | no | `erts_internal_process_display_2` | — |
| `vm_erts_internal_process_flag_3` | `process_flag/3` | regular | normal | no | `erts_internal_process_flag_3` | — |
| `vm_erts_internal_processes_next_1` | `processes_next/1` | regular | normal | no | `erts_internal_processes_next_1` | — |
| `vm_erts_internal_purge_module_2` | `purge_module/2` | regular | normal | no | `erts_internal_purge_module_2` | — |
| `vm_erts_internal_request_failure_2` | `request_failure/2` | regular | normal | no | `erts_internal_request_failure_2` | — |
| `vm_erts_internal_request_system_task_3` | `request_system_task/3` | regular | normal | no | `erts_internal_request_system_task_3` | — |
| `vm_erts_internal_request_system_task_4` | `request_system_task/4` | regular | normal | no | `erts_internal_request_system_task_4` | — |
| `vm_erts_internal_scheduler_wall_time_1` | `scheduler_wall_time/1` | regular | normal | no | `erts_internal_scheduler_wall_time_1` | — |
| `vm_erts_internal_spawn_request_4` | `spawn_request/4` | regular | normal | no | `erts_internal_spawn_request_4` | — |
| `vm_erts_internal_spawn_system_process_3` | `spawn_system_process/3` | regular | normal | no | `erts_internal_spawn_system_process_3` | — |
| `vm_erts_internal_suspend_process_2` | `suspend_process/2` | regular | normal | no | `erts_internal_suspend_process_2` | — |
| `vm_erts_internal_system_check_1` | `system_check/1` | regular | normal | no | `erts_internal_system_check_1` | — |
| `vm_erts_internal_system_monitor_1` | `system_monitor/1` | regular | normal | no | `erts_internal_system_monitor_1` | — |
| `vm_erts_internal_system_monitor_3` | `system_monitor/3` | regular | normal | no | `erts_internal_system_monitor_3` | — |
| `vm_erts_internal_term_to_string_2` | `term_to_string/2` | regular | normal | no | `erts_internal_term_to_string_2` | — |
| `vm_erts_internal_term_type_1` | `term_type/1` | regular | normal | no | `erts_internal_term_type_1` | — |
| `vm_erts_internal_time_unit_0` | `time_unit/0` | regular | normal | no | `erts_internal_time_unit_0` | — |
| `vm_erts_internal_trace_3` | `trace/3` | regular | normal | no | `erts_internal_trace_3` | — |
| `vm_erts_internal_trace_4` | `trace/4` | regular | normal | no | `erts_internal_trace_4` | — |
| `vm_erts_internal_trace_info_3` | `trace_info/3` | regular | normal | no | `erts_internal_trace_info_3` | — |
| `vm_erts_internal_trace_pattern_3` | `trace_pattern/3` | regular | normal | no | `erts_internal_trace_pattern_3` | — |
| `vm_erts_internal_trace_pattern_4` | `trace_pattern/4` | regular | normal | no | `erts_internal_trace_pattern_4` | — |
| `vm_erts_internal_trace_session_create_3` | `trace_session_create/3` | regular | normal | no | `erts_internal_trace_session_create_3` | — |
| `vm_erts_internal_trace_session_destroy_1` | `trace_session_destroy/1` | regular | normal | no | `erts_internal_trace_session_destroy_1` | — |
| `vm_erts_internal_vm_context_0` | `vm_context/0` | regular | normal | no | `erts_internal_vm_context_0` | — |

## erts_literal_area_collector

| ID | Function | Calling class | Scheduling | Guard eligible | C entry | Traits |
| --- | --- | --- | --- | --- | --- | --- |
| `vm_erts_literal_area_collector_release_area_switch_0` | `release_area_switch/0` | regular | normal | no | `erts_literal_area_collector_release_area_switch_0` | — |
| `vm_erts_literal_area_collector_send_copy_request_3` | `send_copy_request/3` | regular | normal | no | `erts_literal_area_collector_send_copy_request_3` | — |

## erts_trace_cleaner

| ID | Function | Calling class | Scheduling | Guard eligible | C entry | Traits |
| --- | --- | --- | --- | --- | --- | --- |
| `vm_erts_trace_cleaner_check_0` | `check/0` | regular | normal | no | `erts_trace_cleaner_check_0` | — |
| `vm_erts_trace_cleaner_send_trace_clean_signal_1` | `send_trace_clean_signal/1` | regular | normal | no | `erts_trace_cleaner_send_trace_clean_signal_1` | — |

## file

| ID | Function | Calling class | Scheduling | Guard eligible | C entry | Traits |
| --- | --- | --- | --- | --- | --- | --- |
| `vm_file_native_name_encoding_0` | `native_name_encoding/0` | regular | normal | no | `file_native_name_encoding_0` | — |

## io

| ID | Function | Calling class | Scheduling | Guard eligible | C entry | Traits |
| --- | --- | --- | --- | --- | --- | --- |
| `vm_io_printable_range_0` | `printable_range/0` | regular | normal | no | `io_printable_range_0` | — |

## math

| ID | Function | Calling class | Scheduling | Guard eligible | C entry | Traits |
| --- | --- | --- | --- | --- | --- | --- |
| `vm_math_acos_1` | `acos/1` | regular | normal | no | `math_acos_1` | — |
| `vm_math_acosh_1` | `acosh/1` | regular | normal | no | `math_acosh_1` | — |
| `vm_math_asin_1` | `asin/1` | regular | normal | no | `math_asin_1` | — |
| `vm_math_asinh_1` | `asinh/1` | regular | normal | no | `math_asinh_1` | — |
| `vm_math_atan_1` | `atan/1` | regular | normal | no | `math_atan_1` | — |
| `vm_math_atan2_2` | `atan2/2` | regular | normal | no | `math_atan2_2` | — |
| `vm_math_atanh_1` | `atanh/1` | regular | normal | no | `math_atanh_1` | — |
| `vm_math_ceil_1` | `ceil/1` | regular | normal | no | `math_ceil_1` | — |
| `vm_math_cos_1` | `cos/1` | regular | normal | no | `math_cos_1` | — |
| `vm_math_cosh_1` | `cosh/1` | regular | normal | no | `math_cosh_1` | — |
| `vm_math_erf_1` | `erf/1` | regular | normal | no | `math_erf_1` | — |
| `vm_math_erfc_1` | `erfc/1` | regular | normal | no | `math_erfc_1` | — |
| `vm_math_exp_1` | `exp/1` | regular | normal | no | `math_exp_1` | — |
| `vm_math_floor_1` | `floor/1` | regular | normal | no | `math_floor_1` | — |
| `vm_math_fmod_2` | `fmod/2` | regular | normal | no | `math_fmod_2` | — |
| `vm_math_log_1` | `log/1` | regular | normal | no | `math_log_1` | — |
| `vm_math_log10_1` | `log10/1` | regular | normal | no | `math_log10_1` | — |
| `vm_math_log2_1` | `log2/1` | regular | normal | no | `math_log2_1` | — |
| `vm_math_pow_2` | `pow/2` | regular | normal | no | `math_pow_2` | — |
| `vm_math_sin_1` | `sin/1` | regular | normal | no | `math_sin_1` | — |
| `vm_math_sinh_1` | `sinh/1` | regular | normal | no | `math_sinh_1` | — |
| `vm_math_sqrt_1` | `sqrt/1` | regular | normal | no | `math_sqrt_1` | — |
| `vm_math_tan_1` | `tan/1` | regular | normal | no | `math_tan_1` | — |
| `vm_math_tanh_1` | `tanh/1` | regular | normal | no | `math_tanh_1` | — |

## net_kernel

| ID | Function | Calling class | Scheduling | Guard eligible | C entry | Traits |
| --- | --- | --- | --- | --- | --- | --- |
| `vm_net_kernel_dflag_unicode_io_1` | `dflag_unicode_io/1` | regular | normal | no | `net_kernel_dflag_unicode_io_1` | — |

## os

| ID | Function | Calling class | Scheduling | Guard eligible | C entry | Traits |
| --- | --- | --- | --- | --- | --- | --- |
| `vm_os_env_0` | `env/0` | regular | normal | no | `os_env_0` | — |
| `vm_os_getenv_1` | `getenv/1` | regular | normal | no | `os_getenv_1` | — |
| `vm_os_getpid_0` | `getpid/0` | regular | normal | no | `os_getpid_0` | — |
| `vm_os_perf_counter_0` | `perf_counter/0` | regular | normal | no | `os_perf_counter_0` | — |
| `vm_os_putenv_2` | `putenv/2` | regular | normal | no | `os_putenv_2` | — |
| `vm_os_set_signal_2` | `set_signal/2` | regular | normal | no | `os_set_signal_2` | — |
| `vm_os_system_time_0` | `system_time/0` | regular | normal | no | `os_system_time_0` | — |
| `vm_os_system_time_1` | `system_time/1` | regular | normal | no | `os_system_time_1` | — |
| `vm_os_timestamp_0` | `timestamp/0` | regular | normal | no | `os_timestamp_0` | — |
| `vm_os_unsetenv_1` | `unsetenv/1` | regular | normal | no | `os_unsetenv_1` | — |

## records

| ID | Function | Calling class | Scheduling | Guard eligible | C entry | Traits |
| --- | --- | --- | --- | --- | --- | --- |
| `vm_records_create_4` | `create/4` | regular | normal | no | `records_create_4` | — |
| `vm_records_get_2` | `get/2` | regular | normal | no | `records_get_2` | — |
| `vm_records_get_definition_2` | `get_definition/2` | regular | normal | no | `records_get_definition_2` | — |
| `vm_records_get_field_names_1` | `get_field_names/1` | regular | normal | no | `records_get_field_names_1` | — |
| `vm_records_get_module_1` | `get_module/1` | regular | normal | no | `records_get_module_1` | — |
| `vm_records_get_name_1` | `get_name/1` | regular | normal | no | `records_get_name_1` | — |
| `vm_records_is_exported_1` | `is_exported/1` | regular | normal | no | `records_is_exported_1` | — |
| `vm_records_update_4` | `update/4` | regular | normal | no | `records_update_4` | — |

