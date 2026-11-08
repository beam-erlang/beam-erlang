%% SPDX-License-Identifier: Apache-2.0
-module(beam).
-compile({beam_private_native_mfas,[{beam,error,2},
                                    {beam,is_function,2},{beam,is_pid,1}]}).
-export([spawn/1, spawn_monitor/1, monitor/1, halt/1]).
-export([apply/2, apply/3, yield/0, hibernate/0]).
-export(['abs'/1,
         'adler32'/1,
         'adler32'/2,
         'adler32_combine'/3,
         'atom_to_list'/1,
         'binary_to_list'/1,
         'binary_to_list'/3,
         'binary_to_term'/1,
         'crc32'/1,
         'crc32'/2,
         'crc32_combine'/3,
         'date'/0,
         'delete_module'/1,
         'display'/1,
         'display_string'/2,
         'element'/2,
         'erase'/0,
         'erase'/1,
         'exit'/1,
         'exit'/2,
         'exit_signal'/2,
         'external_size'/1,
         'external_size'/2,
         'float'/1,
         'float_to_list'/1,
         'float_to_list'/2,
         'fun_info'/2,
         'get'/0,
         'get'/1,
         'get_keys'/1,
         'group_leader'/0,
         'halt'/2,
         'phash'/2,
         'phash2'/1,
         'phash2'/2,
         'hd'/1,
         'integer_to_list'/1,
         'length'/1,
         'link'/1,
         'list_to_atom'/1,
         'list_to_binary'/1,
         'list_to_float'/1,
         'list_to_pid'/1,
         'list_to_port'/1,
         'list_to_ref'/1,
         'list_to_tuple'/1,
         'loaded'/0,
         'localtime'/0,
         'localtime_to_universaltime'/2,
         'make_ref'/0,
         'unique_integer'/0,
         'unique_integer'/1,
         'md5'/1,
         'md5_init'/0,
         'md5_update'/2,
         'md5_final'/1,
         'module_loaded'/1,
         'function_exported'/3,
         'monitor_node'/2,
         'monitor_node'/3,
         'node'/1,
         'node'/0,
         'nodes'/0,
         'nodes'/1,
         'nodes'/2,
         'pid_to_list'/1,
         'ports'/0,
         'pre_loaded'/0,
         'process_flag'/2,
         'process_info'/1,
         'process_info'/2,
         'processes'/0,
         'put'/2,
         'register'/2,
         'registered'/0,
         'round'/1,
         'self'/0,
         'setelement'/3,
         'size'/1,
         'spawn'/3,
         'spawn_link'/3,
         'split_binary'/2,
         'statistics'/1,
         'term_to_binary'/1,
         'term_to_binary'/2,
         'term_to_iovec'/1,
         'term_to_iovec'/2,
         'throw'/1,
         'time'/0,
         'tl'/1,
         'trunc'/1,
         'tuple_to_list'/1,
         'universaltime'/0,
         'universaltime_to_localtime'/1,
         'unlink'/1,
         'unregister'/1,
         'whereis'/1,
         'spawn_opt'/4,
         'setnode'/2,
         'dist_get_stat'/1,
         'dist_ctrl_input_handler'/2,
         'dist_ctrl_put_data'/2,
         'dist_ctrl_get_data'/1,
         'dist_ctrl_get_data_notification'/1,
         'dist_ctrl_get_opt'/2,
         'dist_ctrl_set_opt'/3,
         'spawn_request_abandon'/1,
         'port_set_data'/2,
         'port_get_data'/1,
         'resume_process'/1,
         'bump_reductions'/1,
         'make_tuple'/2,
         'append_element'/2,
         'make_tuple'/3,
         'system_flag'/2,
         'system_info'/1,
         'system_profile'/2,
         'system_profile'/0,
         'ref_to_list'/1,
         'port_to_list'/1,
         'fun_to_list'/1,
         'monitor'/2,
         'demonitor'/1,
         'demonitor'/2,
         'is_process_alive'/1,
         'error'/1,
         'error'/2,
         'raise'/3,
         'is_builtin'/3,
         'and'/2,
         'or'/2,
         'xor'/2,
         'not'/1,
         '>'/2,
         '>='/2,
         '<'/2,
         '=<'/2,
         '=:='/2,
         '=='/2,
         '=/='/2,
         '/='/2,
         '+'/2,
         '-'/2,
         '*'/2,
         '/'/2,
         'div'/2,
         'rem'/2,
         'bor'/2,
         'band'/2,
         'bxor'/2,
         'bsl'/2,
         'bsr'/2,
         'bnot'/1,
         '-'/1,
         '+'/1,
         '!'/2,
         'send'/2,
         'send'/3,
         '++'/2,
         'append'/2,
         '--'/2,
         'subtract'/2,
         'is_atom'/1,
         'is_list'/1,
         'is_tuple'/1,
         'is_float'/1,
         'is_integer'/1,
         'is_number'/1,
         'is_pid'/1,
         'is_port'/1,
         'is_reference'/1,
         'is_binary'/1,
         'is_function'/1,
         'is_function'/2,
         'is_record'/2,
         'is_record'/3,
         'match_spec_test'/3,
         'hibernate'/3,
         'get_module_info'/1,
         'get_module_info'/2,
         'is_boolean'/1,
         'make_fun'/3,
         'iolist_size'/1,
         'iolist_to_binary'/1,
         'list_to_existing_atom'/1,
         'is_bitstring'/1,
         'tuple_size'/1,
         'byte_size'/1,
         'bit_size'/1,
         'list_to_bitstring'/1,
         'bitstring_to_list'/1,
         'decode_packet'/3,
         'atom_to_binary'/2,
         'binary_to_atom'/2,
         'binary_to_existing_atom'/2,
         'load_nif'/2,
         'call_on_load_function'/1,
         'finish_after_on_load'/2,
         'binary_to_term'/2,
         'binary_part'/2,
         'binary_part'/3,
         'nif_error'/1,
         'nif_error'/2,
         'check_old_code'/1,
         'universaltime_to_posixtime'/1,
         'posixtime_to_universaltime'/1,
         'dt_put_tag'/1,
         'dt_get_tag'/0,
         'dt_get_tag_data'/0,
         'dt_spread_tag'/1,
         'dt_restore_tag'/1,
         'dt_prepend_vm_tag_data'/1,
         'dt_append_vm_tag_data'/1,
         'finish_loading'/1,
         'insert_element'/3,
         'delete_element'/2,
         'integer_to_binary'/1,
         'float_to_binary'/1,
         'float_to_binary'/2,
         'binary_to_float'/1,
         'is_map'/1,
         'map_size'/1,
         'fun_info_mfa'/1,
         'get_keys'/0,
         'has_prepared_code_on_load'/1,
         'floor'/1,
         'ceil'/1,
         'iolist_to_iovec'/1,
         'map_get'/2,
         'is_map_key'/2,
         'integer_to_list'/2,
         'integer_to_binary'/2,
         'alias'/1,
         'unalias'/1,
         'monitor'/3,
         'error'/3,
         'min'/2,
         'max'/2,
         'exit'/3,
         'link'/2,
         'is_integer'/3,
         'exit_signal'/3,
         'is_record'/1]).
-beam_native_slots([{'abs',1},
                    {'adler32',1},
                    {'adler32',2},
                    {'adler32_combine',3},
                    {'atom_to_list',1},
                    {'binary_to_list',1},
                    {'binary_to_list',3},
                    {'binary_to_term',1},
                    {'crc32',1},
                    {'crc32',2},
                    {'crc32_combine',3},
                    {'date',0},
                    {'delete_module',1},
                    {'display',1},
                    {'display_string',2},
                    {'element',2},
                    {'erase',0},
                    {'erase',1},
                    {'exit',1},
                    {'exit',2},
                    {'exit_signal',2},
                    {'external_size',1},
                    {'external_size',2},
                    {'float',1},
                    {'float_to_list',1},
                    {'float_to_list',2},
                    {'fun_info',2},
                    {'get',0},
                    {'get',1},
                    {'get_keys',1},
                    {'group_leader',0},
                    {'halt',2},
                    {'phash',2},
                    {'phash2',1},
                    {'phash2',2},
                    {'hd',1},
                    {'integer_to_list',1},
                    {'length',1},
                    {'link',1},
                    {'list_to_atom',1},
                    {'list_to_binary',1},
                    {'list_to_float',1},
                    {'list_to_pid',1},
                    {'list_to_port',1},
                    {'list_to_ref',1},
                    {'list_to_tuple',1},
                    {'loaded',0},
                    {'localtime',0},
                    {'localtime_to_universaltime',2},
                    {'make_ref',0},
                    {'unique_integer',0},
                    {'unique_integer',1},
                    {'md5',1},
                    {'md5_init',0},
                    {'md5_update',2},
                    {'md5_final',1},
                    {'module_loaded',1},
                    {'function_exported',3},
                    {'monitor_node',2},
                    {'monitor_node',3},
                    {'node',1},
                    {'node',0},
                    {'nodes',0},
                    {'nodes',1},
                    {'nodes',2},
                    {'pid_to_list',1},
                    {'ports',0},
                    {'pre_loaded',0},
                    {'process_flag',2},
                    {'process_info',1},
                    {'process_info',2},
                    {'processes',0},
                    {'put',2},
                    {'register',2},
                    {'registered',0},
                    {'round',1},
                    {'self',0},
                    {'setelement',3},
                    {'size',1},
                    {'spawn',3},
                    {'spawn_link',3},
                    {'split_binary',2},
                    {'statistics',1},
                    {'term_to_binary',1},
                    {'term_to_binary',2},
                    {'term_to_iovec',1},
                    {'term_to_iovec',2},
                    {'throw',1},
                    {'time',0},
                    {'tl',1},
                    {'trunc',1},
                    {'tuple_to_list',1},
                    {'universaltime',0},
                    {'universaltime_to_localtime',1},
                    {'unlink',1},
                    {'unregister',1},
                    {'whereis',1},
                    {'spawn_opt',4},
                    {'setnode',2},
                    {'dist_get_stat',1},
                    {'dist_ctrl_input_handler',2},
                    {'dist_ctrl_put_data',2},
                    {'dist_ctrl_get_data',1},
                    {'dist_ctrl_get_data_notification',1},
                    {'dist_ctrl_get_opt',2},
                    {'dist_ctrl_set_opt',3},
                    {'spawn_request_abandon',1},
                    {'port_set_data',2},
                    {'port_get_data',1},
                    {'resume_process',1},
                    {'bump_reductions',1},
                    {'make_tuple',2},
                    {'append_element',2},
                    {'make_tuple',3},
                    {'system_flag',2},
                    {'system_info',1},
                    {'system_profile',2},
                    {'system_profile',0},
                    {'ref_to_list',1},
                    {'port_to_list',1},
                    {'fun_to_list',1},
                    {'monitor',2},
                    {'demonitor',1},
                    {'demonitor',2},
                    {'is_process_alive',1},
                    {'error',1},
                    {'error',2},
                    {'raise',3},
                    {'is_builtin',3},
                    {'and',2},
                    {'or',2},
                    {'xor',2},
                    {'not',1},
                    {'>',2},
                    {'>=',2},
                    {'<',2},
                    {'=<',2},
                    {'=:=',2},
                    {'==',2},
                    {'=/=',2},
                    {'/=',2},
                    {'+',2},
                    {'-',2},
                    {'*',2},
                    {'/',2},
                    {'div',2},
                    {'rem',2},
                    {'bor',2},
                    {'band',2},
                    {'bxor',2},
                    {'bsl',2},
                    {'bsr',2},
                    {'bnot',1},
                    {'-',1},
                    {'+',1},
                    {'!',2},
                    {'send',2},
                    {'send',3},
                    {'++',2},
                    {'append',2},
                    {'--',2},
                    {'subtract',2},
                    {'is_atom',1},
                    {'is_list',1},
                    {'is_tuple',1},
                    {'is_float',1},
                    {'is_integer',1},
                    {'is_number',1},
                    {'is_pid',1},
                    {'is_port',1},
                    {'is_reference',1},
                    {'is_binary',1},
                    {'is_function',1},
                    {'is_function',2},
                    {'is_record',2},
                    {'is_record',3},
                    {'match_spec_test',3},
                    {'hibernate',3},
                    {'get_module_info',1},
                    {'get_module_info',2},
                    {'is_boolean',1},
                    {'make_fun',3},
                    {'iolist_size',1},
                    {'iolist_to_binary',1},
                    {'list_to_existing_atom',1},
                    {'is_bitstring',1},
                    {'tuple_size',1},
                    {'byte_size',1},
                    {'bit_size',1},
                    {'list_to_bitstring',1},
                    {'bitstring_to_list',1},
                    {'decode_packet',3},
                    {'atom_to_binary',2},
                    {'binary_to_atom',2},
                    {'binary_to_existing_atom',2},
                    {'load_nif',2},
                    {'call_on_load_function',1},
                    {'finish_after_on_load',2},
                    {'binary_to_term',2},
                    {'binary_part',2},
                    {'binary_part',3},
                    {'nif_error',1},
                    {'nif_error',2},
                    {'check_old_code',1},
                    {'universaltime_to_posixtime',1},
                    {'posixtime_to_universaltime',1},
                    {'dt_put_tag',1},
                    {'dt_get_tag',0},
                    {'dt_get_tag_data',0},
                    {'dt_spread_tag',1},
                    {'dt_restore_tag',1},
                    {'dt_prepend_vm_tag_data',1},
                    {'dt_append_vm_tag_data',1},
                    {'finish_loading',1},
                    {'insert_element',3},
                    {'delete_element',2},
                    {'integer_to_binary',1},
                    {'float_to_binary',1},
                    {'float_to_binary',2},
                    {'binary_to_float',1},
                    {'is_map',1},
                    {'map_size',1},
                    {'fun_info_mfa',1},
                    {'get_keys',0},
                    {'has_prepared_code_on_load',1},
                    {'floor',1},
                    {'ceil',1},
                    {'iolist_to_iovec',1},
                    {'map_get',2},
                    {'is_map_key',2},
                    {'integer_to_list',2},
                    {'integer_to_binary',2},
                    {'alias',1},
                    {'unalias',1},
                    {'monitor',3},
                    {'error',3},
                    {'min',2},
                    {'max',2},
                    {'exit',3},
                    {'link',2},
                    {'is_integer',3},
                    {'exit_signal',3},
                    {'is_record',1},
                    {apply,2},{apply,3},{yield,0},{hibernate,0}]).

-spec spawn(fun(() -> term())) -> pid().
spawn(Fun) when beam:is_function(Fun,0) ->
    beam:spawn(beam,apply,[Fun,[]]);
spawn(Fun) -> beam:error(badarg,[Fun]).

-spec spawn_monitor(fun(() -> term())) -> {pid(), reference()}.
spawn_monitor(Fun) when beam:is_function(Fun,0) ->
    beam:spawn_opt(beam,apply,[Fun,[]],[monitor]);
spawn_monitor(Fun) -> beam:error(badarg,[Fun]).

-spec monitor(pid()) -> reference().
monitor(Pid) when beam:is_pid(Pid) -> beam:monitor(process,Pid);
monitor(Pid) -> beam:error(badarg,[Pid]).

-spec halt(non_neg_integer() | abort | string()) -> no_return().
halt(Status) ->
    try beam:halt(Status, [])
    catch error:Reason -> beam:error(Reason, [Status])
    end.

%% Native entry declarations; selected calls use the VM's direct dispatch.
-spec 'abs'(term()) -> term().
'abs'(_) -> beam:nif_error(undefined).
-spec 'adler32'(term()) -> term().
'adler32'(_) -> beam:nif_error(undefined).
-spec 'adler32'(term(), term()) -> term().
'adler32'(_, _) -> beam:nif_error(undefined).
-spec 'adler32_combine'(term(), term(), term()) -> term().
'adler32_combine'(_, _, _) -> beam:nif_error(undefined).
-spec 'atom_to_list'(term()) -> term().
'atom_to_list'(_) -> beam:nif_error(undefined).
-spec 'binary_to_list'(term()) -> term().
'binary_to_list'(_) -> beam:nif_error(undefined).
-spec 'binary_to_list'(term(), term(), term()) -> term().
'binary_to_list'(_, _, _) -> beam:nif_error(undefined).
-spec 'binary_to_term'(term()) -> term().
'binary_to_term'(_) -> beam:nif_error(undefined).
-spec 'crc32'(term()) -> term().
'crc32'(_) -> beam:nif_error(undefined).
-spec 'crc32'(term(), term()) -> term().
'crc32'(_, _) -> beam:nif_error(undefined).
-spec 'crc32_combine'(term(), term(), term()) -> term().
'crc32_combine'(_, _, _) -> beam:nif_error(undefined).
-spec 'date'() -> term().
'date'() -> beam:nif_error(undefined).
-spec 'delete_module'(term()) -> term().
'delete_module'(_) -> beam:nif_error(undefined).
-spec 'display'(term()) -> term().
'display'(_) -> beam:nif_error(undefined).
-spec 'display_string'(term(), term()) -> term().
'display_string'(_, _) -> beam:nif_error(undefined).
-spec 'element'(term(), term()) -> term().
'element'(_, _) -> beam:nif_error(undefined).
-spec 'erase'() -> term().
'erase'() -> beam:nif_error(undefined).
-spec 'erase'(term()) -> term().
'erase'(_) -> beam:nif_error(undefined).
-spec 'exit'(term()) -> term().
'exit'(_) -> beam:nif_error(undefined).
-spec 'exit'(term(), term()) -> term().
'exit'(_, _) -> beam:nif_error(undefined).
-spec 'exit_signal'(term(), term()) -> term().
'exit_signal'(_, _) -> beam:nif_error(undefined).
-spec 'external_size'(term()) -> term().
'external_size'(_) -> beam:nif_error(undefined).
-spec 'external_size'(term(), term()) -> term().
'external_size'(_, _) -> beam:nif_error(undefined).
-spec 'float'(term()) -> term().
'float'(_) -> beam:nif_error(undefined).
-spec 'float_to_list'(term()) -> term().
'float_to_list'(_) -> beam:nif_error(undefined).
-spec 'float_to_list'(term(), term()) -> term().
'float_to_list'(_, _) -> beam:nif_error(undefined).
-spec 'fun_info'(term(), term()) -> term().
'fun_info'(_, _) -> beam:nif_error(undefined).
-spec 'get'() -> term().
'get'() -> beam:nif_error(undefined).
-spec 'get'(term()) -> term().
'get'(_) -> beam:nif_error(undefined).
-spec 'get_keys'(term()) -> term().
'get_keys'(_) -> beam:nif_error(undefined).
-spec 'group_leader'() -> term().
'group_leader'() -> beam:nif_error(undefined).
-spec 'halt'(term(), term()) -> term().
'halt'(_, _) -> beam:nif_error(undefined).
-spec 'phash'(term(), term()) -> term().
'phash'(_, _) -> beam:nif_error(undefined).
-spec 'phash2'(term()) -> term().
'phash2'(_) -> beam:nif_error(undefined).
-spec 'phash2'(term(), term()) -> term().
'phash2'(_, _) -> beam:nif_error(undefined).
-spec 'hd'(term()) -> term().
'hd'(_) -> beam:nif_error(undefined).
-spec 'integer_to_list'(term()) -> term().
'integer_to_list'(_) -> beam:nif_error(undefined).
-spec 'length'(term()) -> term().
'length'(_) -> beam:nif_error(undefined).
-spec 'link'(term()) -> term().
'link'(_) -> beam:nif_error(undefined).
-spec 'list_to_atom'(term()) -> term().
'list_to_atom'(_) -> beam:nif_error(undefined).
-spec 'list_to_binary'(term()) -> term().
'list_to_binary'(_) -> beam:nif_error(undefined).
-spec 'list_to_float'(term()) -> term().
'list_to_float'(_) -> beam:nif_error(undefined).
-spec 'list_to_pid'(term()) -> term().
'list_to_pid'(_) -> beam:nif_error(undefined).
-spec 'list_to_port'(term()) -> term().
'list_to_port'(_) -> beam:nif_error(undefined).
-spec 'list_to_ref'(term()) -> term().
'list_to_ref'(_) -> beam:nif_error(undefined).
-spec 'list_to_tuple'(term()) -> term().
'list_to_tuple'(_) -> beam:nif_error(undefined).
-spec 'loaded'() -> term().
'loaded'() -> beam:nif_error(undefined).
-spec 'localtime'() -> term().
'localtime'() -> beam:nif_error(undefined).
-spec 'localtime_to_universaltime'(term(), term()) -> term().
'localtime_to_universaltime'(_, _) -> beam:nif_error(undefined).
-spec 'make_ref'() -> term().
'make_ref'() -> beam:nif_error(undefined).
-spec 'unique_integer'() -> term().
'unique_integer'() -> beam:nif_error(undefined).
-spec 'unique_integer'(term()) -> term().
'unique_integer'(_) -> beam:nif_error(undefined).
-spec 'md5'(term()) -> term().
'md5'(_) -> beam:nif_error(undefined).
-spec 'md5_init'() -> term().
'md5_init'() -> beam:nif_error(undefined).
-spec 'md5_update'(term(), term()) -> term().
'md5_update'(_, _) -> beam:nif_error(undefined).
-spec 'md5_final'(term()) -> term().
'md5_final'(_) -> beam:nif_error(undefined).
-spec 'module_loaded'(term()) -> term().
'module_loaded'(_) -> beam:nif_error(undefined).
-spec 'function_exported'(term(), term(), term()) -> term().
'function_exported'(_, _, _) -> beam:nif_error(undefined).
-spec 'monitor_node'(term(), term()) -> term().
'monitor_node'(_, _) -> beam:nif_error(undefined).
-spec 'monitor_node'(term(), term(), term()) -> term().
'monitor_node'(_, _, _) -> beam:nif_error(undefined).
-spec 'node'(term()) -> term().
'node'(_) -> beam:nif_error(undefined).
-spec 'node'() -> term().
'node'() -> beam:nif_error(undefined).
-spec 'nodes'() -> term().
'nodes'() -> beam:nif_error(undefined).
-spec 'nodes'(term()) -> term().
'nodes'(_) -> beam:nif_error(undefined).
-spec 'nodes'(term(), term()) -> term().
'nodes'(_, _) -> beam:nif_error(undefined).
-spec 'pid_to_list'(term()) -> term().
'pid_to_list'(_) -> beam:nif_error(undefined).
-spec 'ports'() -> term().
'ports'() -> beam:nif_error(undefined).
-spec 'pre_loaded'() -> term().
'pre_loaded'() -> beam:nif_error(undefined).
-spec 'process_flag'(term(), term()) -> term().
'process_flag'(_, _) -> beam:nif_error(undefined).
-spec 'process_info'(term()) -> term().
'process_info'(_) -> beam:nif_error(undefined).
-spec 'process_info'(term(), term()) -> term().
'process_info'(_, _) -> beam:nif_error(undefined).
-spec 'processes'() -> term().
'processes'() -> beam:nif_error(undefined).
-spec 'put'(term(), term()) -> term().
'put'(_, _) -> beam:nif_error(undefined).
-spec 'register'(term(), term()) -> term().
'register'(_, _) -> beam:nif_error(undefined).
-spec 'registered'() -> term().
'registered'() -> beam:nif_error(undefined).
-spec 'round'(term()) -> term().
'round'(_) -> beam:nif_error(undefined).
-spec 'self'() -> term().
'self'() -> beam:nif_error(undefined).
-spec 'setelement'(term(), term(), term()) -> term().
'setelement'(_, _, _) -> beam:nif_error(undefined).
-spec 'size'(term()) -> term().
'size'(_) -> beam:nif_error(undefined).
-spec 'spawn'(term(), term(), term()) -> term().
'spawn'(_, _, _) -> beam:nif_error(undefined).
-spec 'spawn_link'(term(), term(), term()) -> term().
'spawn_link'(_, _, _) -> beam:nif_error(undefined).
-spec 'split_binary'(term(), term()) -> term().
'split_binary'(_, _) -> beam:nif_error(undefined).
-spec 'statistics'(term()) -> term().
'statistics'(_) -> beam:nif_error(undefined).
-spec 'term_to_binary'(term()) -> term().
'term_to_binary'(_) -> beam:nif_error(undefined).
-spec 'term_to_binary'(term(), term()) -> term().
'term_to_binary'(_, _) -> beam:nif_error(undefined).
-spec 'term_to_iovec'(term()) -> term().
'term_to_iovec'(_) -> beam:nif_error(undefined).
-spec 'term_to_iovec'(term(), term()) -> term().
'term_to_iovec'(_, _) -> beam:nif_error(undefined).
-spec 'throw'(term()) -> term().
'throw'(_) -> beam:nif_error(undefined).
-spec 'time'() -> term().
'time'() -> beam:nif_error(undefined).
-spec 'tl'(term()) -> term().
'tl'(_) -> beam:nif_error(undefined).
-spec 'trunc'(term()) -> term().
'trunc'(_) -> beam:nif_error(undefined).
-spec 'tuple_to_list'(term()) -> term().
'tuple_to_list'(_) -> beam:nif_error(undefined).
-spec 'universaltime'() -> term().
'universaltime'() -> beam:nif_error(undefined).
-spec 'universaltime_to_localtime'(term()) -> term().
'universaltime_to_localtime'(_) -> beam:nif_error(undefined).
-spec 'unlink'(term()) -> term().
'unlink'(_) -> beam:nif_error(undefined).
-spec 'unregister'(term()) -> term().
'unregister'(_) -> beam:nif_error(undefined).
-spec 'whereis'(term()) -> term().
'whereis'(_) -> beam:nif_error(undefined).
-spec 'spawn_opt'(term(), term(), term(), term()) -> term().
'spawn_opt'(_, _, _, _) -> beam:nif_error(undefined).
-spec 'setnode'(term(), term()) -> term().
'setnode'(_, _) -> beam:nif_error(undefined).
-spec 'dist_get_stat'(term()) -> term().
'dist_get_stat'(_) -> beam:nif_error(undefined).
-spec 'dist_ctrl_input_handler'(term(), term()) -> term().
'dist_ctrl_input_handler'(_, _) -> beam:nif_error(undefined).
-spec 'dist_ctrl_put_data'(term(), term()) -> term().
'dist_ctrl_put_data'(_, _) -> beam:nif_error(undefined).
-spec 'dist_ctrl_get_data'(term()) -> term().
'dist_ctrl_get_data'(_) -> beam:nif_error(undefined).
-spec 'dist_ctrl_get_data_notification'(term()) -> term().
'dist_ctrl_get_data_notification'(_) -> beam:nif_error(undefined).
-spec 'dist_ctrl_get_opt'(term(), term()) -> term().
'dist_ctrl_get_opt'(_, _) -> beam:nif_error(undefined).
-spec 'dist_ctrl_set_opt'(term(), term(), term()) -> term().
'dist_ctrl_set_opt'(_, _, _) -> beam:nif_error(undefined).
-spec 'spawn_request_abandon'(term()) -> term().
'spawn_request_abandon'(_) -> beam:nif_error(undefined).
-spec 'port_set_data'(term(), term()) -> term().
'port_set_data'(_, _) -> beam:nif_error(undefined).
-spec 'port_get_data'(term()) -> term().
'port_get_data'(_) -> beam:nif_error(undefined).
-spec 'resume_process'(term()) -> term().
'resume_process'(_) -> beam:nif_error(undefined).
-spec 'bump_reductions'(term()) -> term().
'bump_reductions'(_) -> beam:nif_error(undefined).
-spec 'make_tuple'(term(), term()) -> term().
'make_tuple'(_, _) -> beam:nif_error(undefined).
-spec 'append_element'(term(), term()) -> term().
'append_element'(_, _) -> beam:nif_error(undefined).
-spec 'make_tuple'(term(), term(), term()) -> term().
'make_tuple'(_, _, _) -> beam:nif_error(undefined).
-spec 'system_flag'(term(), term()) -> term().
'system_flag'(_, _) -> beam:nif_error(undefined).
-spec 'system_info'(term()) -> term().
'system_info'(_) -> beam:nif_error(undefined).
-spec 'system_profile'(term(), term()) -> term().
'system_profile'(_, _) -> beam:nif_error(undefined).
-spec 'system_profile'() -> term().
'system_profile'() -> beam:nif_error(undefined).
-spec 'ref_to_list'(term()) -> term().
'ref_to_list'(_) -> beam:nif_error(undefined).
-spec 'port_to_list'(term()) -> term().
'port_to_list'(_) -> beam:nif_error(undefined).
-spec 'fun_to_list'(term()) -> term().
'fun_to_list'(_) -> beam:nif_error(undefined).
-spec 'monitor'(term(), term()) -> term().
'monitor'(_, _) -> beam:nif_error(undefined).
-spec 'demonitor'(term()) -> term().
'demonitor'(_) -> beam:nif_error(undefined).
-spec 'demonitor'(term(), term()) -> term().
'demonitor'(_, _) -> beam:nif_error(undefined).
-spec 'is_process_alive'(term()) -> term().
'is_process_alive'(_) -> beam:nif_error(undefined).
-spec 'error'(term()) -> term().
'error'(_) -> beam:nif_error(undefined).
-spec 'error'(term(), term()) -> term().
'error'(_, _) -> beam:nif_error(undefined).
-spec 'raise'(term(), term(), term()) -> term().
'raise'(_, _, _) -> beam:nif_error(undefined).
-spec 'is_builtin'(term(), term(), term()) -> term().
'is_builtin'(_, _, _) -> beam:nif_error(undefined).
-spec 'and'(term(), term()) -> term().
'and'(_, _) -> beam:nif_error(undefined).
-spec 'or'(term(), term()) -> term().
'or'(_, _) -> beam:nif_error(undefined).
-spec 'xor'(term(), term()) -> term().
'xor'(_, _) -> beam:nif_error(undefined).
-spec 'not'(term()) -> term().
'not'(_) -> beam:nif_error(undefined).
-spec '>'(term(), term()) -> term().
'>'(_, _) -> beam:nif_error(undefined).
-spec '>='(term(), term()) -> term().
'>='(_, _) -> beam:nif_error(undefined).
-spec '<'(term(), term()) -> term().
'<'(_, _) -> beam:nif_error(undefined).
-spec '=<'(term(), term()) -> term().
'=<'(_, _) -> beam:nif_error(undefined).
-spec '=:='(term(), term()) -> term().
'=:='(_, _) -> beam:nif_error(undefined).
-spec '=='(term(), term()) -> term().
'=='(_, _) -> beam:nif_error(undefined).
-spec '=/='(term(), term()) -> term().
'=/='(_, _) -> beam:nif_error(undefined).
-spec '/='(term(), term()) -> term().
'/='(_, _) -> beam:nif_error(undefined).
-spec '+'(term(), term()) -> term().
'+'(_, _) -> beam:nif_error(undefined).
-spec '-'(term(), term()) -> term().
'-'(_, _) -> beam:nif_error(undefined).
-spec '*'(term(), term()) -> term().
'*'(_, _) -> beam:nif_error(undefined).
-spec '/'(term(), term()) -> term().
'/'(_, _) -> beam:nif_error(undefined).
-spec 'div'(term(), term()) -> term().
'div'(_, _) -> beam:nif_error(undefined).
-spec 'rem'(term(), term()) -> term().
'rem'(_, _) -> beam:nif_error(undefined).
-spec 'bor'(term(), term()) -> term().
'bor'(_, _) -> beam:nif_error(undefined).
-spec 'band'(term(), term()) -> term().
'band'(_, _) -> beam:nif_error(undefined).
-spec 'bxor'(term(), term()) -> term().
'bxor'(_, _) -> beam:nif_error(undefined).
-spec 'bsl'(term(), term()) -> term().
'bsl'(_, _) -> beam:nif_error(undefined).
-spec 'bsr'(term(), term()) -> term().
'bsr'(_, _) -> beam:nif_error(undefined).
-spec 'bnot'(term()) -> term().
'bnot'(_) -> beam:nif_error(undefined).
-spec '-'(term()) -> term().
'-'(_) -> beam:nif_error(undefined).
-spec '+'(term()) -> term().
'+'(_) -> beam:nif_error(undefined).
-spec '!'(term(), term()) -> term().
'!'(_, _) -> beam:nif_error(undefined).
-spec 'send'(term(), term()) -> term().
'send'(_, _) -> beam:nif_error(undefined).
-spec 'send'(term(), term(), term()) -> term().
'send'(_, _, _) -> beam:nif_error(undefined).
-spec '++'(term(), term()) -> term().
'++'(_, _) -> beam:nif_error(undefined).
-spec 'append'(term(), term()) -> term().
'append'(_, _) -> beam:nif_error(undefined).
-spec '--'(term(), term()) -> term().
'--'(_, _) -> beam:nif_error(undefined).
-spec 'subtract'(term(), term()) -> term().
'subtract'(_, _) -> beam:nif_error(undefined).
-spec 'is_atom'(term()) -> term().
'is_atom'(_) -> beam:nif_error(undefined).
-spec 'is_list'(term()) -> term().
'is_list'(_) -> beam:nif_error(undefined).
-spec 'is_tuple'(term()) -> term().
'is_tuple'(_) -> beam:nif_error(undefined).
-spec 'is_float'(term()) -> term().
'is_float'(_) -> beam:nif_error(undefined).
-spec 'is_integer'(term()) -> term().
'is_integer'(_) -> beam:nif_error(undefined).
-spec 'is_number'(term()) -> term().
'is_number'(_) -> beam:nif_error(undefined).
-spec 'is_pid'(term()) -> term().
'is_pid'(_) -> beam:nif_error(undefined).
-spec 'is_port'(term()) -> term().
'is_port'(_) -> beam:nif_error(undefined).
-spec 'is_reference'(term()) -> term().
'is_reference'(_) -> beam:nif_error(undefined).
-spec 'is_binary'(term()) -> term().
'is_binary'(_) -> beam:nif_error(undefined).
-spec 'is_function'(term()) -> term().
'is_function'(_) -> beam:nif_error(undefined).
-spec 'is_function'(term(), term()) -> term().
'is_function'(_, _) -> beam:nif_error(undefined).
-spec 'is_record'(term(), term()) -> term().
'is_record'(_, _) -> beam:nif_error(undefined).
-spec 'is_record'(term(), term(), term()) -> term().
'is_record'(_, _, _) -> beam:nif_error(undefined).
-spec 'match_spec_test'(term(), term(), term()) -> term().
'match_spec_test'(_, _, _) -> beam:nif_error(undefined).
-spec 'hibernate'(term(), term(), term()) -> term().
'hibernate'(_, _, _) -> beam:nif_error(undefined).
-spec 'get_module_info'(term()) -> term().
'get_module_info'(_) -> beam:nif_error(undefined).
-spec 'get_module_info'(term(), term()) -> term().
'get_module_info'(_, _) -> beam:nif_error(undefined).
-spec 'is_boolean'(term()) -> term().
'is_boolean'(_) -> beam:nif_error(undefined).
-spec 'make_fun'(term(), term(), term()) -> term().
'make_fun'(_, _, _) -> beam:nif_error(undefined).
-spec 'iolist_size'(term()) -> term().
'iolist_size'(_) -> beam:nif_error(undefined).
-spec 'iolist_to_binary'(term()) -> term().
'iolist_to_binary'(_) -> beam:nif_error(undefined).
-spec 'list_to_existing_atom'(term()) -> term().
'list_to_existing_atom'(_) -> beam:nif_error(undefined).
-spec 'is_bitstring'(term()) -> term().
'is_bitstring'(_) -> beam:nif_error(undefined).
-spec 'tuple_size'(term()) -> term().
'tuple_size'(_) -> beam:nif_error(undefined).
-spec 'byte_size'(term()) -> term().
'byte_size'(_) -> beam:nif_error(undefined).
-spec 'bit_size'(term()) -> term().
'bit_size'(_) -> beam:nif_error(undefined).
-spec 'list_to_bitstring'(term()) -> term().
'list_to_bitstring'(_) -> beam:nif_error(undefined).
-spec 'bitstring_to_list'(term()) -> term().
'bitstring_to_list'(_) -> beam:nif_error(undefined).
-spec 'decode_packet'(term(), term(), term()) -> term().
'decode_packet'(_, _, _) -> beam:nif_error(undefined).
-spec 'atom_to_binary'(term(), term()) -> term().
'atom_to_binary'(_, _) -> beam:nif_error(undefined).
-spec 'binary_to_atom'(term(), term()) -> term().
'binary_to_atom'(_, _) -> beam:nif_error(undefined).
-spec 'binary_to_existing_atom'(term(), term()) -> term().
'binary_to_existing_atom'(_, _) -> beam:nif_error(undefined).
-spec 'load_nif'(term(), term()) -> term().
'load_nif'(_, _) -> beam:nif_error(undefined).
-spec 'call_on_load_function'(term()) -> term().
'call_on_load_function'(_) -> beam:nif_error(undefined).
-spec 'finish_after_on_load'(term(), term()) -> term().
'finish_after_on_load'(_, _) -> beam:nif_error(undefined).
-spec 'binary_to_term'(term(), term()) -> term().
'binary_to_term'(_, _) -> beam:nif_error(undefined).
-spec 'binary_part'(term(), term()) -> term().
'binary_part'(_, _) -> beam:nif_error(undefined).
-spec 'binary_part'(term(), term(), term()) -> term().
'binary_part'(_, _, _) -> beam:nif_error(undefined).
-spec 'nif_error'(term()) -> term().
'nif_error'(_) -> beam:nif_error(undefined).
-spec 'nif_error'(term(), term()) -> term().
'nif_error'(_, _) -> beam:nif_error(undefined).
-spec 'check_old_code'(term()) -> term().
'check_old_code'(_) -> beam:nif_error(undefined).
-spec 'universaltime_to_posixtime'(term()) -> term().
'universaltime_to_posixtime'(_) -> beam:nif_error(undefined).
-spec 'posixtime_to_universaltime'(term()) -> term().
'posixtime_to_universaltime'(_) -> beam:nif_error(undefined).
-spec 'dt_put_tag'(term()) -> term().
'dt_put_tag'(_) -> beam:nif_error(undefined).
-spec 'dt_get_tag'() -> term().
'dt_get_tag'() -> beam:nif_error(undefined).
-spec 'dt_get_tag_data'() -> term().
'dt_get_tag_data'() -> beam:nif_error(undefined).
-spec 'dt_spread_tag'(term()) -> term().
'dt_spread_tag'(_) -> beam:nif_error(undefined).
-spec 'dt_restore_tag'(term()) -> term().
'dt_restore_tag'(_) -> beam:nif_error(undefined).
-spec 'dt_prepend_vm_tag_data'(term()) -> term().
'dt_prepend_vm_tag_data'(_) -> beam:nif_error(undefined).
-spec 'dt_append_vm_tag_data'(term()) -> term().
'dt_append_vm_tag_data'(_) -> beam:nif_error(undefined).
-spec 'finish_loading'(term()) -> term().
'finish_loading'(_) -> beam:nif_error(undefined).
-spec 'insert_element'(term(), term(), term()) -> term().
'insert_element'(_, _, _) -> beam:nif_error(undefined).
-spec 'delete_element'(term(), term()) -> term().
'delete_element'(_, _) -> beam:nif_error(undefined).
-spec 'integer_to_binary'(term()) -> term().
'integer_to_binary'(_) -> beam:nif_error(undefined).
-spec 'float_to_binary'(term()) -> term().
'float_to_binary'(_) -> beam:nif_error(undefined).
-spec 'float_to_binary'(term(), term()) -> term().
'float_to_binary'(_, _) -> beam:nif_error(undefined).
-spec 'binary_to_float'(term()) -> term().
'binary_to_float'(_) -> beam:nif_error(undefined).
-spec 'is_map'(term()) -> term().
'is_map'(_) -> beam:nif_error(undefined).
-spec 'map_size'(term()) -> term().
'map_size'(_) -> beam:nif_error(undefined).
-spec 'fun_info_mfa'(term()) -> term().
'fun_info_mfa'(_) -> beam:nif_error(undefined).
-spec 'get_keys'() -> term().
'get_keys'() -> beam:nif_error(undefined).
-spec 'has_prepared_code_on_load'(term()) -> term().
'has_prepared_code_on_load'(_) -> beam:nif_error(undefined).
-spec 'floor'(term()) -> term().
'floor'(_) -> beam:nif_error(undefined).
-spec 'ceil'(term()) -> term().
'ceil'(_) -> beam:nif_error(undefined).
-spec 'iolist_to_iovec'(term()) -> term().
'iolist_to_iovec'(_) -> beam:nif_error(undefined).
-spec 'map_get'(term(), term()) -> term().
'map_get'(_, _) -> beam:nif_error(undefined).
-spec 'is_map_key'(term(), term()) -> term().
'is_map_key'(_, _) -> beam:nif_error(undefined).
-spec 'integer_to_list'(term(), term()) -> term().
'integer_to_list'(_, _) -> beam:nif_error(undefined).
-spec 'integer_to_binary'(term(), term()) -> term().
'integer_to_binary'(_, _) -> beam:nif_error(undefined).
-spec 'alias'(term()) -> term().
'alias'(_) -> beam:nif_error(undefined).
-spec 'unalias'(term()) -> term().
'unalias'(_) -> beam:nif_error(undefined).
-spec 'monitor'(term(), term(), term()) -> term().
'monitor'(_, _, _) -> beam:nif_error(undefined).
-spec 'error'(term(), term(), term()) -> term().
'error'(_, _, _) -> beam:nif_error(undefined).
-spec 'min'(term(), term()) -> term().
'min'(_, _) -> beam:nif_error(undefined).
-spec 'max'(term(), term()) -> term().
'max'(_, _) -> beam:nif_error(undefined).
-spec 'exit'(term(), term(), term()) -> term().
'exit'(_, _, _) -> beam:nif_error(undefined).
-spec 'link'(term(), term()) -> term().
'link'(_, _) -> beam:nif_error(undefined).
-spec 'is_integer'(term(), term(), term()) -> term().
'is_integer'(_, _, _) -> beam:nif_error(undefined).
-spec 'exit_signal'(term(), term(), term()) -> term().
'exit_signal'(_, _, _) -> beam:nif_error(undefined).
-spec 'is_record'(term()) -> term().
'is_record'(_) -> beam:nif_error(undefined).

apply(_, _) -> beam:nif_error(undef).
apply(_, _, _) -> beam:nif_error(undef).
yield() -> beam:nif_error(undef).
hibernate() -> beam:nif_error(undef).
