%% SPDX-License-Identifier: Apache-2.0
%% Fixed build-time projection of owned runtime support, never user programs.
-module(beam_runtime_projection).
-export([parse_transform/2,core_transform/2]).

parse_transform(OriginalForms, _Options) ->
    Forms = baseline_forms(OriginalForms),
    [{attribute,_,module,Module}] = [F || F={attribute,_,module,_} <- Forms],
    Original = original_module(Module),
    Imports = maps:from_list([{{F,A},M} || {attribute,_,import,{M,FAs}} <- Forms,
                                           facade(M), {F,A} <- FAs]),
    Context = #{module=>Original, imports=>Imports, guard=>false, function=>undefined,
                locals=>[{F,A} || {function,_,F,A,_} <- Forms],
                all_imports=>lists:append([FAs || {attribute,_,import,{_,FAs}} <- Forms])},
    Projected = [walk(F,Context) || F <- Forms, keep_form(F)],
    audit(Projected),
    Functions = [{F,A} || {function,_,F,A,_} <- Forms],
    Slots = [FA || FA <- lists:usort(native(Original)++vm_provider_native(Original)),
                   lists:member(FA,Functions)],
    Declarations = [{attribute,0,compile,beam_modern_map_comprehensions}] ++
        [{attribute,0,beam_native_slots,Slots} || Slots =/= []],
    lists:flatmap(fun(Form={attribute,_,module,_}) -> [Form|Declarations];
                     (Form) -> [Form]
                  end,Projected).

baseline_forms(OriginalForms) ->
    Forms = ets_fun_metadata(OriginalForms),
    [{attribute,_,module,Module}] = [F || F={attribute,_,module,_} <- Forms],
    Original = original_module(Module),
    [case Form of
         {attribute,A,export,FAs} ->
             {attribute,A,export,[FA || FA <- FAs, not omitted(Original,FA)]};
         _ -> Form
     end || Form <- Forms, not removed_deprecation(Original,Form)].

removed_deprecation(lists,{attribute,_,deprecated,_}) -> true;
removed_deprecation(_,_) -> false.

omitted(lists,{zf,2}) -> true;
omitted(ets,FA) ->
    %% Four adapter omissions and nine unresolved file/inspection exports are
    %% withheld independently. New source exports require an explicit decision.
    not lists:member(FA,native(ets) ++ ets_ordinary());
omitted(string,FA) -> lists:member(FA,
    [{len,1},{concat,2},{chr,2},{rchr,2},{str,2},{rstr,2},{span,2},{cspan,2},
     {substr,2},{substr,3},{tokens,2},{chars,2},{chars,3},{copies,2},{words,1},
     {words,2},{strip,1},{strip,2},{strip,3},{sub_word,2},{sub_word,3},
     {left,2},{left,3},{right,2},{right,3},{sub_string,2},{sub_string,3},
     {centre,2},{centre,3},{join,2},{to_upper,1},{to_lower,1}]);
omitted(_,_) -> false.

ets_ordinary() ->
    [{all,0},{delete_all_objects,1},{foldl,3},{foldr,3},{fun2ms,1},
     {init_table,2},{match_delete,2},{match_spec_run,2},{repair_continuation,2},
     {select_delete,2},{tab2list,1},{test_ms,2}].

ets_fun_metadata(Forms) ->
    case [original_module(M) || {attribute,_,module,M} <- Forms] of
        [ets] ->
            case lists:any(fun({function,_,fun2ms,1,_}) -> true;
                              (_) -> false end,Forms) of
                true -> Forms ++ [ets_fun_data()];
                false -> Forms
            end;
        _ -> Forms
    end.

ets_fun_data() ->
    %% Same metadata decoding as the owned erl_eval:fun_data/1. Ordinary fun
    %% rejection must not acquire the evaluator in an otherwise base-only VM.
    Text = "beam_ets_fun_data(F) when is_function(F) -> "
           "case erlang:fun_info(F,module) of "
           "{module,erl_eval} -> case erlang:fun_info(F,env) of "
           "{env,[{_A,B,_L,_E,_U,C}]} -> {fun_data,B,C}; "
           "{env,[{_A,B,_L,_E,_U,C,N}]} -> {named_fun_data,B,N,C} end; "
           "_ -> false end; beam_ets_fun_data(_) -> false.",
    {ok,Tokens,_} = erl_scan:string(Text),
    {ok,Form} = erl_parse:parse_form(Tokens),
    Form.

keep_form({attribute,_,import,{Module,_}}) -> not facade(Module);
keep_form(_) -> true.

private_module(binary) -> beam_binary;
private_module(records) -> beam_records;
private_module(ets) -> beam_ets;
private_module(lists) -> beam_lists;
private_module(maps) -> beam_maps;
private_module(string) -> beam_string;
private_module(unicode) -> beam_unicode;
private_module(unicode_util) -> beam_runtime_unicode_util;
private_module(queue) -> beam_runtime_queue;
private_module(sets) -> beam_runtime_sets;
private_module(proplists) -> beam_runtime_proplists;
private_module(orddict) -> beam_runtime_orddict;
private_module(persistent_term) -> beam_persistent_term;
private_module(atomics) -> beam_atomics;
private_module(counters) -> beam_counters;
private_module(math) -> beam_math;
private_module(erl_ddll) -> beam_ddll;
private_module(erl_debugger) -> beam_debugger;
private_module(Module) -> Module.

original_module(beam_binary) -> binary;
original_module(beam_records) -> records;
original_module(beam_ets) -> ets;
original_module(beam_lists) -> lists;
original_module(beam_maps) -> maps;
original_module(beam_string) -> string;
original_module(beam_unicode) -> unicode;
original_module(beam_runtime_unicode_util) -> unicode_util;
original_module(beam_runtime_queue) -> queue;
original_module(beam_runtime_sets) -> sets;
original_module(beam_runtime_proplists) -> proplists;
original_module(beam_runtime_orddict) -> orddict;
original_module(beam_math) -> math;
original_module(beam_ddll) -> erl_ddll;
original_module(beam_debugger) -> erl_debugger;
original_module(Module) -> Module.

facade(Module) -> Module =:= binary orelse private_module(Module) =/= Module.

target(erlang,trace_delivered,1,_) -> {beam_trace,trace_delivered};
target(erlang,seq_trace,2,_) -> {beam_trace,seq_trace};
target(erlang,seq_trace_info,1,_) -> {beam_trace,seq_trace_info};
target(erlang,seq_trace_print,A,_) when A =:= 1; A =:= 2 -> {beam_trace,seq_trace_print};
target(erts_internal,F,A,_) when F =:= trace_session_create, A =:= 3;
                              F =:= trace_session_destroy, A =:= 1;
                              F =:= trace, A =:= 4;
                              F =:= trace_pattern, A =:= 4;
                              F =:= trace_info, A =:= 3 -> {beam_trace,F};
target(erlang,convert_time_unit,3,_) -> {erts_internal,clock_convert_time_unit};
target(erlang,F,A,_) ->
    case clock_native(F,A) of
        true -> {beam_time,F};
        false -> case vm_native(F,A) of true -> {beam,F}; false -> {erlang,F} end
    end;
target(string,F,2,_) when F =:= chr -> {beam_runtime_string_legacy,F};
target(string,to_upper,1,_) -> {beam_runtime_string_legacy,to_upper};
target(maps,get,2,#{guard:=true}) -> {beam,map_get};
target(maps,is_key,2,#{guard:=true}) -> {beam,is_map_key};
target(maps,size,1,_) -> {beam,map_size};
target(M,F,_,_) -> {private_module(M),F}.

clock_native(monotonic_time,0) -> true;
clock_native(monotonic_time,1) -> true;
clock_native(system_time,0) -> true;
clock_native(system_time,1) -> true;
clock_native(time_offset,0) -> true;
clock_native(time_offset,1) -> true;
clock_native(timestamp,0) -> true;
clock_native(start_timer,3) -> true;
clock_native(start_timer,4) -> true;
clock_native(send_after,3) -> true;
clock_native(send_after,4) -> true;
clock_native(cancel_timer,1) -> true;
clock_native(cancel_timer,2) -> true;
clock_native(read_timer,1) -> true;
clock_native(read_timer,2) -> true;
clock_native(_,_) -> false.

native(ets) -> [{internal_request_all,0},
                {new,2},
                {delete,1},
                {delete,2},
                {delete_object,2},
                {first,1},
                {first_lookup,1},
                {is_compiled_ms,1},
                {lookup,2},
                {lookup_element,3},
                {lookup_element,4},
                {info,1},
                {info,2},
                {last,1},
                {last_lookup,1},
                {match,1},
                {match,2},
                {match,3},
                {match_object,1},
                {match_object,2},
                {match_object,3},
                {member,2},
                {next,2},
                {next_lookup,2},
                {prev,2},
                {prev_lookup,2},
                {insert,2},
                {insert_new,2},
                {rename,2},
                {safe_fixtable,2},
                {slot,2},
                {update_counter,3},
                {select,1},
                {select,2},
                {select,3},
                {select_count,2},
                {select_reverse,1},
                {select_reverse,2},
                {select_reverse,3},
                {select_replace,2},
                {match_spec_compile,1},
                {match_spec_run_r,3},
                {update_element,3},
                {give_away,3},
                {setopts,2},
                {take,2},
                {update_counter,4},
                {whereis,1},
                {internal_delete_all,2},
                {internal_select_delete,2},
                {update_element,4}];
native(binary) -> [{compile_pattern,1},{match,2},{match,3},{matches,2},{matches,3},{longest_common_prefix,1},{longest_common_suffix,1},{first,1},{last,1},{at,2},{part,2},{part,3},{list_to_bin,1},{copy,1},{copy,2},{referenced_byte_size,1},{encode_unsigned,1},{encode_unsigned,2},{decode_unsigned,1},{decode_unsigned,2},{split,2},{split,3}];
native(lists) -> [{member,2},{reverse,2},{keymember,3},{keysearch,3},{keyfind,3}];
native(maps) -> [{find,2},{get,2},{from_list,1},{from_keys,2},{is_key,2},{keys,1},
                 {merge,2},{put,3},{remove,2},{take,2},{update,3},{values,1}];
native(string) -> [{list_to_float,1}];
native(unicode) -> [{characters_to_binary,2},{characters_to_list,2},{bin_is_7bit,1}];
native(beam_trace) -> [{trace_session_create,3}, {trace_session_destroy,1}, {trace,4}, {trace_pattern,4}, {trace_info,3}, {trace_delivered,1}, {seq_trace,2}, {seq_trace_info,1}, {seq_trace_print,1}, {seq_trace_print,2}];
native(_) -> [].

walk({attribute,A,module,M},_) -> {attribute,A,module,private_module(M)};
walk({attribute,A,import,{M,FAs}},_) -> {attribute,A,import,{M,FAs}};
walk({function,A,F,N,Clauses},C) ->
    {function,A,F,N,walk(Clauses,C#{function=>{F,N}})};
walk({call,A,{remote,_,{atom,_,erl_eval},{atom,_,fun_data}},Args},
     C=#{module:=ets,function:={fun2ms,1}}) ->
    {call,A,{atom,A,beam_ets_fun_data},walk(Args,C)};
walk({clause,A,[{atom,L,M}],Guards,Body},C=#{module:=erl_stdlib_errors,function:={format_error,2}}) ->
    To=private_module(M),
    {clause,A,[{atom,L,To}],walk(Guards,C#{guard=>true}),walk(Body,C#{guard=>false})};
walk({clause,A,Patterns,Guards,Body},C) ->
    {clause,A,walk(Patterns,C#{guard=>false}),walk(Guards,C#{guard=>true}),
     walk(Body,C#{guard=>false})};
%% Preserve explicit formatter overrides; supply the private support formatter
%% only where the generic VM has no product-specific formatter metadata.
walk({call,A,{remote,R,{atom,MA,maps},{atom,FA,get}},
      [{atom,KA,module},Info,Default]},C=#{module:=erl_error,function:={get_extended_error,2}}) ->
    {call,A,{remote,R,{atom,MA,beam_maps},{atom,FA,get}},
     [{atom,KA,module},walk(Info,C),formatter_case(A,Default,walk(Default,C))]};
walk({call,A,{remote,R,{atom,MA,lists},{atom,FA,keyfind}},
      [{atom,KA,error_info},{integer,IA,1},Info]},C=#{module:=erl_error,function:={get_extended_error,2}}) ->
    Query={call,A,{remote,R,{atom,MA,beam_lists},{atom,FA,keyfind}},
           [{atom,KA,error_info},{integer,IA,1},walk(Info,C)]},
    Synthetic={tuple,A,[{atom,A,error_info},
                       {map,A,[{map_field_assoc,A,{atom,A,module},{atom,A,erl_stdlib_errors}}]}]},
    {'case',A,Query,[{clause,A,[{atom,A,false}],[],
                     [formatter_case(A,{var,A,'M'},Synthetic,{atom,A,false})]},
                    {clause,A,[{var,A,'_BeamRuntimeErrorInfo'}],[],
                     [{var,A,'_BeamRuntimeErrorInfo'}]}]};
%% This argument selects a match-spec dialect, not a destination module.
walk({call,A,{remote,R,{atom,MA,ms_transform},{atom,FA,transform_from_shell}},
      [_,Clauses,Imports]},C=#{module:=ets,function:={fun2ms,1}}) ->
    {call,A,{remote,R,{atom,MA,ms_transform},{atom,FA,transform_from_shell}},
     [{atom,A,ets},walk(Clauses,C),walk(Imports,C)]};
%% Literal module-valued dispatch is projected only in owned runtime sources.
walk({call,A,{remote,R,{atom,MA,erlang},{atom,FA,F}},Args},C)
  when F =:= module_loaded, length(Args) =:= 1;
       (F =:= apply orelse F =:= function_exported orelse F =:= is_builtin), length(Args) =:= 3 ->
    {To,Fun}=target(erlang,F,length(Args),C),
    {call,A,{remote,R,{atom,MA,To},{atom,FA,Fun}},dispatch(F,Args,C)};
walk({call,A,{atom,FA,F},Args},C)
  when F =:= module_loaded, length(Args) =:= 1;
       (F =:= apply orelse F =:= function_exported orelse F =:= is_builtin), length(Args) =:= 3 ->
    {To,Fun}=target(erlang,F,length(Args),C),
    {call,A,{remote,A,{atom,FA,To},{atom,FA,Fun}},dispatch(F,Args,C)};
walk({call,A,{remote,R,{atom,MA,M},{atom,FA,F}},Args},C) ->
    {To,Fun}=target(M,F,length(Args),C),
    {call,A,{remote,R,{atom,MA,To},{atom,FA,Fun}},walk(Args,C)};
walk({call,A,{remote,R,{atom,MA,M},Function},Args},C) ->
    %% Only module identity changes when the implementation selects a function.
    {call,A,{remote,R,{atom,MA,private_module(M)},walk(Function,C)},walk(Args,C)};
walk({call,A,{atom,FA,F},Args},C=#{module:=M,imports:=Imports}) ->
    Arity=length(Args),
    Source=case maps:find({F,Arity},Imports) of
               {ok,Imported}->Imported;
               error->case lists:member({F,Arity},native(M)) of true->M;false->none end
           end,
    case Source of
        none ->
            case owned_auto_target(F,Arity,C) of
                none -> {call,A,{atom,FA,F},walk(Args,C)};
                {To,Fun} -> {call,A,{remote,A,{atom,FA,To},{atom,FA,Fun}},walk(Args,C)}
            end;
        _ -> {To,Fun}=target(Source,F,Arity,C),
             {call,A,{remote,A,{atom,FA,To},{atom,FA,Fun}},walk(Args,C)}
    end;
walk({'fun',A,{function,M,F,N}},C) when is_atom(M),is_atom(F),is_integer(N) ->
    {To,Fun}=target(M,F,N,C), {'fun',A,{function,To,Fun,N}};
walk({'fun',A,{function,{atom,MA,M},{atom,FA,F},{integer,NA,N}}},C) ->
    {To,Fun}=target(M,F,N,C),
    {'fun',A,{function,{atom,MA,To},{atom,FA,Fun},{integer,NA,N}}};
walk({'fun',A,{function,{atom,MA,M},Function,Arity}},C) ->
    {'fun',A,{function,{atom,MA,private_module(M)},walk(Function,C),walk(Arity,C)}};
walk({'fun',A,{function,F,N}}=Original,C=#{module:=M,imports:=Imports}) ->
    case maps:find({F,N},Imports) of
        {ok,Imported} -> {To,Fun}=target(Imported,F,N,C),
                         {'fun',A,{function,{atom,A,To},{atom,A,Fun},{integer,A,N}}};
        error -> case lists:member({F,N},native(M)) of
                     true -> {To,Fun}=target(M,F,N,C),
                             {'fun',A,{function,{atom,A,To},{atom,A,Fun},{integer,A,N}}};
                     false ->
                         case owned_auto_target(F,N,C) of
                             none -> Original;
                             {To,Fun} -> {'fun',A,{function,{atom,A,To},{atom,A,Fun},{integer,A,N}}}
                         end
                 end
    end;
walk({remote_type,A,[{atom,MA,M},Name,Args]},C) ->
    To=private_module(M),
    NewName=Name,
    {remote_type,A,[{atom,MA,To},NewName,walk(Args,C)]};
%% This explicit loaded-helper list is policy, not an encoding/data atom list.
walk({match,A,{var,V,'Modules'},Value},C=#{module:=io_lib,function:={test_modules_loaded,3}}) ->
    {match,A,{var,V,'Modules'},module_list(Value,C)};
walk(Tuple,C) when is_tuple(Tuple) -> list_to_tuple([walk(E,C) || E <- tuple_to_list(Tuple)]);
walk(List,C) when is_list(List) -> [walk(E,C) || E <- List];
walk(Value,_) -> Value.

%% Runtime controllers use existing core helpers without acquiring the ordinary
%% modern base facade. This affects owned source calls, not interpreted user ASTs
%% or the evaluator's generated target profile.
owned_auto_target(F,A,#{locals:=Locals,all_imports:=Imports}) ->
    case lists:member({F,A},Locals) orelse lists:member({F,A},Imports) of
        true -> none;
        false ->
            case {F,A} of
                {spawn,1} -> {erlang,spawn};
                {spawn_monitor,1} -> {erlang,spawn_monitor};
                {convert_time_unit,3} -> {erts_internal,clock_convert_time_unit};
                _ -> none
            end
    end.

module_list({cons,A,{atom,B,M},Tail},C) ->
    {cons,A,{atom,B,private_module(M)},module_list(Tail,C)};
module_list(Other,C) -> walk(Other,C).

formatter_case(A,Module,Default) -> formatter_case(A,Module,{atom,A,erl_stdlib_errors},Default).
formatter_case(A,Module,Value,Default) ->
    Clauses=[{clause,A,[{atom,A,M}],[],[Value]} ||
                M <- [beam_ets,beam_lists,beam_maps,beam_string,beam_unicode,beam_binary,beam_records,beam_math]],
    {'case',A,Module,Clauses++[{clause,A,[{var,A,'_'}],[],[Default]}]}.

%% All binary operations share the complete public destination.
dispatch(_,[{atom,MA,binary},Fun,ArityOrArgs],C) ->
    [{atom,MA,beam_binary},walk(Fun,C),walk(ArityOrArgs,C)];
dispatch(F,[{atom,MA,erlang},{atom,FA,Fun},ArityOrArgs],C) ->
    Arity=case F of
              apply -> case ArityOrArgs of {nil,_}->0;{cons,_,_,_}->literal_length(ArityOrArgs);_->unknown end;
              _ -> case ArityOrArgs of {integer,_,N}->N;_->unknown end
          end,
    {To,Name}=target(erlang,Fun,Arity,C),
    [{atom,MA,To},{atom,FA,Name},walk(ArityOrArgs,C)];
dispatch(module_loaded,[{atom,A,M}],_) -> [{atom,A,private_module(M)}];

dispatch(_,[{atom,A,M}|Args],C) -> [{atom,A,private_module(M)}|walk(Args,C)];
dispatch(_,Args,C) -> walk(Args,C).

literal_length({nil,_}) -> 0;
literal_length({cons,_,_,Tail}) ->
    case literal_length(Tail) of unknown -> unknown; N -> N+1 end;
literal_length(_) -> unknown.

%% Fail closed on remaining executable static facade references. Ordinary atoms
%% and terms representing quoted syntax do not have these abstract-code shapes.
audit({remote,_,{atom,_,M},_}) ->
    case facade(M) of true->error({unresolved_runtime_module,M});false->ok end;
audit({'fun',_,{function,{atom,_,M},_,_}}) ->
    case facade(M) of true->error({unresolved_runtime_module,M});false->ok end;
audit({attribute,_,import,{M,_}}) ->
    case facade(M) of true->error({unresolved_runtime_import,M});false->ok end;
audit(T) when is_tuple(T) -> audit(tuple_to_list(T));
audit(L) when is_list(L) -> lists:foreach(fun audit/1,L);
audit(_) -> ok.

%% Lowering can introduce canonical calls (notably map comprehensions) after
%% parsing. Project those in owned runtime Core as well, retaining native calls.
core_transform(Core, _) -> cerl_trees:map(fun core_node/1,Core).

core_node(Node) ->
    case cerl:type(Node) of
        call ->
            Module=cerl:call_module(Node), Name=cerl:call_name(Node),
            Args=cerl:call_args(Node),
            case cerl:is_c_atom(Module) andalso cerl:is_c_atom(Name) of
                true ->
                    {To,Fun}=case cerl:concrete(Module) of
                                 erlang -> {erlang,cerl:concrete(Name)};
                                 M -> target(M,cerl:concrete(Name),length(Args),#{guard=>false})
                             end,
                    cerl:update_c_call(Node,cerl:set_ann(cerl:c_atom(To),cerl:get_ann(Module)),
                                     cerl:set_ann(cerl:c_atom(Fun),cerl:get_ann(Name)),Args);
                false -> Node
            end;
        _ -> Node
    end.

%% Finite projection of owned calls; module-valued user data stays unchanged.
vm_native('abs',1) -> true;
vm_native('adler32',1) -> true;
vm_native('adler32',2) -> true;
vm_native('adler32_combine',3) -> true;
vm_native('atom_to_list',1) -> true;
vm_native('binary_to_list',1) -> true;
vm_native('binary_to_list',3) -> true;
vm_native('binary_to_term',1) -> true;
vm_native('crc32',1) -> true;
vm_native('crc32',2) -> true;
vm_native('crc32_combine',3) -> true;
vm_native('date',0) -> true;
vm_native('delete_module',1) -> true;
vm_native('display',1) -> true;
vm_native('display_string',2) -> true;
vm_native('element',2) -> true;
vm_native('erase',0) -> true;
vm_native('erase',1) -> true;
vm_native('exit',1) -> true;
vm_native('exit',2) -> true;
vm_native('exit_signal',2) -> true;
vm_native('external_size',1) -> true;
vm_native('external_size',2) -> true;
vm_native('float',1) -> true;
vm_native('float_to_list',1) -> true;
vm_native('float_to_list',2) -> true;
vm_native('fun_info',2) -> true;
vm_native('get',0) -> true;
vm_native('get',1) -> true;
vm_native('get_keys',1) -> true;
vm_native('group_leader',0) -> true;
vm_native('halt',2) -> true;
vm_native('phash',2) -> true;
vm_native('phash2',1) -> true;
vm_native('phash2',2) -> true;
vm_native('hd',1) -> true;
vm_native('integer_to_list',1) -> true;
vm_native('length',1) -> true;
vm_native('link',1) -> true;
vm_native('list_to_atom',1) -> true;
vm_native('list_to_binary',1) -> true;
vm_native('list_to_float',1) -> true;
vm_native('list_to_pid',1) -> true;
vm_native('list_to_port',1) -> true;
vm_native('list_to_ref',1) -> true;
vm_native('list_to_tuple',1) -> true;
vm_native('loaded',0) -> true;
vm_native('localtime',0) -> true;
vm_native('localtime_to_universaltime',2) -> true;
vm_native('make_ref',0) -> true;
vm_native('unique_integer',0) -> true;
vm_native('unique_integer',1) -> true;
vm_native('md5',1) -> true;
vm_native('md5_init',0) -> true;
vm_native('md5_update',2) -> true;
vm_native('md5_final',1) -> true;
vm_native('module_loaded',1) -> true;
vm_native('function_exported',3) -> true;
vm_native('monitor_node',2) -> true;
vm_native('monitor_node',3) -> true;
vm_native('node',1) -> true;
vm_native('node',0) -> true;
vm_native('nodes',0) -> true;
vm_native('nodes',1) -> true;
vm_native('nodes',2) -> true;
vm_native('pid_to_list',1) -> true;
vm_native('ports',0) -> true;
vm_native('pre_loaded',0) -> true;
vm_native('process_flag',2) -> true;
vm_native('process_info',1) -> true;
vm_native('process_info',2) -> true;
vm_native('processes',0) -> true;
vm_native('put',2) -> true;
vm_native('register',2) -> true;
vm_native('registered',0) -> true;
vm_native('round',1) -> true;
vm_native('self',0) -> true;
vm_native('setelement',3) -> true;
vm_native('size',1) -> true;
vm_native('spawn',3) -> true;
vm_native('spawn_link',3) -> true;
vm_native('split_binary',2) -> true;
vm_native('statistics',1) -> true;
vm_native('term_to_binary',1) -> true;
vm_native('term_to_binary',2) -> true;
vm_native('term_to_iovec',1) -> true;
vm_native('term_to_iovec',2) -> true;
vm_native('throw',1) -> true;
vm_native('time',0) -> true;
vm_native('tl',1) -> true;
vm_native('trunc',1) -> true;
vm_native('tuple_to_list',1) -> true;
vm_native('universaltime',0) -> true;
vm_native('universaltime_to_localtime',1) -> true;
vm_native('unlink',1) -> true;
vm_native('unregister',1) -> true;
vm_native('whereis',1) -> true;
vm_native('spawn_opt',4) -> true;
vm_native('setnode',2) -> true;
vm_native('dist_get_stat',1) -> true;
vm_native('dist_ctrl_input_handler',2) -> true;
vm_native('dist_ctrl_put_data',2) -> true;
vm_native('dist_ctrl_get_data',1) -> true;
vm_native('dist_ctrl_get_data_notification',1) -> true;
vm_native('dist_ctrl_get_opt',2) -> true;
vm_native('dist_ctrl_set_opt',3) -> true;
vm_native('spawn_request_abandon',1) -> true;
vm_native('port_set_data',2) -> true;
vm_native('port_get_data',1) -> true;
vm_native('trace_info',2) -> true;
vm_native('trace_delivered',1) -> true;
vm_native('seq_trace',2) -> true;
vm_native('seq_trace_info',1) -> true;
vm_native('seq_trace_print',1) -> true;
vm_native('seq_trace_print',2) -> true;
vm_native('resume_process',1) -> true;
vm_native('bump_reductions',1) -> true;
vm_native('make_tuple',2) -> true;
vm_native('append_element',2) -> true;
vm_native('make_tuple',3) -> true;
vm_native('system_flag',2) -> true;
vm_native('system_info',1) -> true;
vm_native('system_profile',2) -> true;
vm_native('system_profile',0) -> true;
vm_native('ref_to_list',1) -> true;
vm_native('port_to_list',1) -> true;
vm_native('fun_to_list',1) -> true;
vm_native('monitor',2) -> true;
vm_native('demonitor',1) -> true;
vm_native('demonitor',2) -> true;
vm_native('is_process_alive',1) -> true;
vm_native('error',1) -> true;
vm_native('error',2) -> true;
vm_native('raise',3) -> true;
vm_native('is_builtin',3) -> true;
vm_native('and',2) -> true;
vm_native('or',2) -> true;
vm_native('xor',2) -> true;
vm_native('not',1) -> true;
vm_native('>',2) -> true;
vm_native('>=',2) -> true;
vm_native('<',2) -> true;
vm_native('=<',2) -> true;
vm_native('=:=',2) -> true;
vm_native('==',2) -> true;
vm_native('=/=',2) -> true;
vm_native('/=',2) -> true;
vm_native('+',2) -> true;
vm_native('-',2) -> true;
vm_native('*',2) -> true;
vm_native('/',2) -> true;
vm_native('div',2) -> true;
vm_native('rem',2) -> true;
vm_native('bor',2) -> true;
vm_native('band',2) -> true;
vm_native('bxor',2) -> true;
vm_native('bsl',2) -> true;
vm_native('bsr',2) -> true;
vm_native('bnot',1) -> true;
vm_native('-',1) -> true;
vm_native('+',1) -> true;
vm_native('!',2) -> true;
vm_native('send',2) -> true;
vm_native('send',3) -> true;
vm_native('++',2) -> true;
vm_native('append',2) -> true;
vm_native('--',2) -> true;
vm_native('subtract',2) -> true;
vm_native('is_atom',1) -> true;
vm_native('is_list',1) -> true;
vm_native('is_tuple',1) -> true;
vm_native('is_float',1) -> true;
vm_native('is_integer',1) -> true;
vm_native('is_number',1) -> true;
vm_native('is_pid',1) -> true;
vm_native('is_port',1) -> true;
vm_native('is_reference',1) -> true;
vm_native('is_binary',1) -> true;
vm_native('is_function',1) -> true;
vm_native('is_function',2) -> true;
vm_native('is_record',2) -> true;
vm_native('is_record',3) -> true;
vm_native('match_spec_test',3) -> true;
vm_native('hibernate',3) -> true;
vm_native('get_module_info',1) -> true;
vm_native('get_module_info',2) -> true;
vm_native('is_boolean',1) -> true;
vm_native('make_fun',3) -> true;
vm_native('iolist_size',1) -> true;
vm_native('iolist_to_binary',1) -> true;
vm_native('list_to_existing_atom',1) -> true;
vm_native('is_bitstring',1) -> true;
vm_native('tuple_size',1) -> true;
vm_native('byte_size',1) -> true;
vm_native('bit_size',1) -> true;
vm_native('list_to_bitstring',1) -> true;
vm_native('bitstring_to_list',1) -> true;
vm_native('decode_packet',3) -> true;
vm_native('atom_to_binary',2) -> true;
vm_native('binary_to_atom',2) -> true;
vm_native('binary_to_existing_atom',2) -> true;
vm_native('load_nif',2) -> true;
vm_native('call_on_load_function',1) -> true;
vm_native('finish_after_on_load',2) -> true;
vm_native('binary_to_term',2) -> true;
vm_native('binary_part',2) -> true;
vm_native('binary_part',3) -> true;
vm_native('nif_error',1) -> true;
vm_native('nif_error',2) -> true;
vm_native('check_old_code',1) -> true;
vm_native('universaltime_to_posixtime',1) -> true;
vm_native('posixtime_to_universaltime',1) -> true;
vm_native('dt_put_tag',1) -> true;
vm_native('dt_get_tag',0) -> true;
vm_native('dt_get_tag_data',0) -> true;
vm_native('dt_spread_tag',1) -> true;
vm_native('dt_restore_tag',1) -> true;
vm_native('dt_prepend_vm_tag_data',1) -> true;
vm_native('dt_append_vm_tag_data',1) -> true;
vm_native('finish_loading',1) -> true;
vm_native('insert_element',3) -> true;
vm_native('delete_element',2) -> true;
vm_native('integer_to_binary',1) -> true;
vm_native('float_to_binary',1) -> true;
vm_native('float_to_binary',2) -> true;
vm_native('binary_to_float',1) -> true;
vm_native('is_map',1) -> true;
vm_native('map_size',1) -> true;
vm_native('fun_info_mfa',1) -> true;
vm_native('get_keys',0) -> true;
vm_native('has_prepared_code_on_load',1) -> true;
vm_native('floor',1) -> true;
vm_native('ceil',1) -> true;
vm_native('iolist_to_iovec',1) -> true;
vm_native('map_get',2) -> true;
vm_native('is_map_key',2) -> true;
vm_native('integer_to_list',2) -> true;
vm_native('integer_to_binary',2) -> true;
vm_native('alias',1) -> true;
vm_native('unalias',1) -> true;
vm_native('monitor',3) -> true;
vm_native('error',3) -> true;
vm_native('min',2) -> true;
vm_native('max',2) -> true;
vm_native('exit',3) -> true;
vm_native('link',2) -> true;
vm_native('is_integer',3) -> true;
vm_native('exit_signal',3) -> true;
vm_native('is_record',1) -> true;
vm_native('apply',2) -> true;
vm_native('apply',3) -> true;
vm_native('yield',0) -> true;
vm_native('hibernate',0) -> true;
vm_native(_,_) -> false.

%% Native slots in owned providers, independent of whether a host selects them.
vm_provider_native(code) -> [{'coverage_support',0},{'get_coverage_mode',0},{'get_coverage_mode',1},{'get_coverage',2},{'reset_coverage',1},{'set_coverage_mode',1},{'get_debug_info',1}];
vm_provider_native(erl_ddll) -> [{'try_load',3},{'try_unload',2},{'loaded_drivers',0},{'info',2},{'format_error_int',1},{'monitor',2},{'demonitor',1}];
vm_provider_native(erl_debugger) -> [{'supported',0},{'instrumentations',0},{'toggle_instrumentations',1},{'register',1},{'unregister',2},{'whereis',0},{'breakpoint',3},{'breakpoints',1},{'breakpoints',3},{'stack_frames',2},{'peek_stack_frame_slot',4},{'xregs_count',1},{'peek_xreg',3}];
vm_provider_native(error_logger) -> [{'warning_map',0}];
vm_provider_native(erts_debug) -> [{'disassemble',1},{'breakpoint',2},{'same',2},{'flat_size',1},{'get_internal_state',1},{'set_internal_state',2},{'dist_ext_to_term',2},{'instructions',0},{'interpreter_size',0},{'dirty_cpu',2},{'dirty_io',2},{'dirty',3},{'lcnt_control',2},{'lcnt_control',1},{'lcnt_collect',0},{'lcnt_clear',0},{'map_info',1},{'size_shared',1},{'copy_shared',2},{'unaligned_bitstring',2}];
vm_provider_native(file) -> [{'native_name_encoding',0}];
vm_provider_native(io) -> [{'printable_range',0}];
vm_provider_native(math) -> [{'cos',1},{'cosh',1},{'sin',1},{'sinh',1},{'tan',1},{'tanh',1},{'acos',1},{'acosh',1},{'asin',1},{'asinh',1},{'atan',1},{'atanh',1},{'erf',1},{'erfc',1},{'exp',1},{'log',1},{'log2',1},{'log10',1},{'sqrt',1},{'atan2',2},{'pow',2},{'floor',1},{'ceil',1},{'fmod',2}];
vm_provider_native(net_kernel) -> [{'dflag_unicode_io',1}];
vm_provider_native(os) -> [{'getenv',1},{'putenv',2},{'unsetenv',1},{'getpid',0},{'timestamp',0},{'system_time',0},{'system_time',1},{'perf_counter',0},{'set_signal',2},{'env',0}];
vm_provider_native(records) -> [{'get_module',1},{'get_name',1},{'get',2},{'get_field_names',1},{'is_exported',1},{'create',4},{'update',4},{'get_definition',2}];
vm_provider_native(_) -> [].
