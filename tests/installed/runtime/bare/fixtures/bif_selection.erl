%% SPDX-License-Identifier: Apache-2.0
-module(bif_selection).
%% Test observation and control remain independent of public selections.
-compile({beam_private_native_mfas,[
    {beam,apply,2},{beam,apply,3},{beam,byte_size,1},
    {beam,call_on_load_function,1},{beam,erase,0},{beam,erase,1},
    {beam,error,1},{beam,error,2},{beam,error,3},
    {beam,exit,1},{beam,exit,2},{beam,exit,3},
    {beam,finish_after_on_load,2},{beam,finish_loading,1},{beam,function_exported,3},
    {beam,get,0},{beam,get,1},{beam,has_prepared_code_on_load,1},
    {beam,iolist_to_binary,1},{beam,is_builtin,3},{beam,is_integer,1},
    {beam,is_integer,3},{beam,is_reference,1},{beam,is_tuple,1},
    {beam,length,1},{beam,make_fun,3},{beam,make_ref,0},
    {beam,map_get,2},{beam,max,2},{beam,min,2},
    {beam,module_loaded,1},{beam,monitor,2},{beam,monitor,3},
    {beam,process_info,1},{beam,process_info,2},{beam,put,2},
    {beam,register,2},{beam,resume_process,1},{beam,self,0},
    {beam,send,2},{beam,send,3},{beam,spawn,3},
    {beam,system_info,1},{beam,unregister,1},
    {beam,yield,0},{erlang,apply,2},{erlang,apply,3},
    {erlang,byte_size,1},{erlang,call_on_load_function,1},{erlang,erase,0},
    {erlang,erase,1},{erlang,error,1},{erlang,error,2},
    {erlang,error,3},{erlang,exit,1},{erlang,exit,2},
    {erlang,exit,3},{erlang,finish_after_on_load,2},{erlang,finish_loading,1},
    {erlang,function_exported,3},{erlang,get,0},{erlang,get,1},
    {erlang,has_prepared_code_on_load,1},{erlang,iolist_to_binary,1},{erlang,is_builtin,3},
    {erlang,is_integer,1},{erlang,is_integer,3},{erlang,is_reference,1},
    {erlang,is_tuple,1},{erlang,length,1},{erlang,make_fun,3},
    {erlang,make_ref,0},{erlang,map_get,2},{erlang,max,2},
    {erlang,min,2},{erlang,module_loaded,1},{erlang,monitor,2},
    {erlang,monitor,3},{erlang,process_info,1},{erlang,process_info,2},
    {erlang,put,2},{erlang,register,2},{erlang,resume_process,1},
    {erlang,self,0},{erlang,send,2},{erlang,send,3},
    {erlang,spawn,3},{erlang,system_info,1},{erlang,trace_info,2},
    {erlang,unregister,1},{erlang,yield,0},{erts_internal,garbage_collect,1},
    {erts_internal,prepare_loading,2},{erts_internal,suspend_process,2},{erts_internal,trace,3},
    {erts_internal,trace,4},{erts_internal,trace_info,3},{erts_internal,trace_pattern,3},
    {erts_internal,trace_pattern,4}]}).
-export([partial_time/0,modern_time/0,otp_time/0,no_time/0,custom_time/0,tables_trace/0,tables_loaded/0,tables_onload/1,tables_loaded_onload/0,ets_partial/0, ets_abandon/0, modern/0, otp/0, mixed/0, none/0, ordinary/0, large/0, renamed/0, retained/0, reloaded/0, unloaded/0, reload/1, abandon/0, search_yields/0]).

modern() ->
    reverse_error(modern),
    ok = composition(true, false),
    true = beam_lists:member(b, [a,b]),
    [b,a|tail] = beam_lists:reverse([a,b], tail),
    {ok,1} = beam_maps:find(a, #{a=>1}),
    #{a:=1} = beam_maps:put(a, 1, #{}),
    <<"ok">> = beam_unicode:characters_to_binary(<<"ok">>),
    false = erlang:is_builtin('bound.bytes', cut, 2),
    false = erlang:is_builtin('bound.bytes', seek, 2),
    true = erlang:is_builtin(beam_binary, split, 2),
    true = erlang:is_builtin(beam_binary, split, 3),
    false = erlang:is_builtin(binary, split, 2),
    false = erlang:is_builtin(binary, split, 3),
    false = erlang:is_builtin(erlang, display, 1),
    [<<"b">>, <<"nana">>] = beam_binary:split(<<"banana">>, <<"a">>),
    [<<"ban">>, <<"na">>] = beam_binary:split(<<"banana">>, <<"a">>, [{scope,{2,3}}]),
    [<<"b">>, <<"n">>, <<"n">>] =
        beam_binary:split(<<"banana">>, <<"a">>, [global,trim]),
    [<<>>] = beam_binary:split(<<>>, <<"x">>),
    [] = beam_binary:split(<<>>, <<"x">>, [trim_all]),
    badarg = try beam_binary:split(x, <<"a">>) catch error:badarg -> badarg end,
    badarg = try beam_binary:split(<<1:1>>, <<"a">>) catch error:badarg -> badarg end,
    badarg = try beam_binary:split(<<"a">>, <<"a">>, [badoption]) catch error:badarg -> badarg end,
    badarg = try beam_binary:split(<<"a">>, <<"a">>, #{unknown => true}) catch error:badarg -> badarg end,
    true = names_public_arguments(),
    false = erlang:module_loaded(beam_ets),
    T = apply(beam_ets,new,[native_only,[set]]),
    true = beam_ets:insert(T,{key,value}),
    Lookup = fun beam_ets:lookup/2,
    [{key,value}] = Lookup(T,key),
    [{key,value}] = apply(beam_ets,lookup,[T,key]),
    true = beam_ets:delete(T),
    try apply(beam_ets,lookup,[T,key]) of _ -> error(expected_stale_table)
    catch error:badarg:LookupError -> [{beam_ets,lookup,[T,key],_}|_] = LookupError end,
    undef = try binary:split(<<"ab">>, <<"a">>) catch error:undef -> undef end,
    undef = try apply(binary, split, [<<"ab">>, <<"a">>]) catch error:undef -> undef end,
    ok.

otp() ->
    reverse_error(canonical),
    ok = composition(false, true),
    false = erlang:is_builtin('bound.bytes', cut, 2),
    false = erlang:is_builtin('bound.bytes', seek, 2),
    false = erlang:is_builtin(beam_binary, split, 2),
    true = erlang:is_builtin(binary, split, 2),
    true = erlang:is_builtin(binary, split, 3),
    true = erlang:is_builtin(erlang, display, 1),
    [<<"b">>, <<"nana">>] = binary:split(<<"banana">>, <<"a">>),
    [<<"b">>, <<"nana">>] = binary:split(<<"banana">>, [<<"a">>], []),
    undef = try beam_binary:split(<<"a">>, <<"b">>) catch error:undef -> undef end,
    ok.

mixed() ->
    ok = composition(true, mixed),
    true = erlang:is_builtin(beam_binary, split, 2),
    false = erlang:is_builtin(binary, split, 2),
    Parts = beam_binary:split(<<"banana">>, <<"a">>),
    Parts = binary:split(<<"banana">>, <<"a">>, []),
    Atomic = beam_atomics:new(2, []),
    ok = beam_atomics:put(Atomic, 1, 12),
    12 = beam_atomics:get(Atomic, 1),
    13 = beam_atomics:add_get(Atomic, 1, 1),
    13 = beam_atomics:get(Atomic, 1),
    Counter = beam_counters:new(1, [atomics]),
    {atomics, Raw} = Counter,
    ok = beam_atomics:put(Raw, 1, 21),
    21 = beam_counters:get(Counter, 1),
    ok = beam_persistent_term:put(shared, Atomic),
    Atomic = beam_persistent_term:get(shared),
    ok = beam_persistent_term:put_new(shared, Atomic),
    true = beam_persistent_term:erase(shared),
    Modern = beam_binary:compile_pattern([<<"aba">>, <<"ababa">>]),
    {2, 5} = binary:match(<<"xxababayy">>, Modern, []),
    Otp = beam_binary:compile_pattern(<<"aba">>),
    {2, 3} = beam_binary:match(<<"xxababayy">>, Otp),
    [{2, 3}] = beam_binary:matches(<<"xxababayy">>, Otp),
    true = beam_binary:referenced_byte_size(<<1:1>>) >= 0,
    undef = try binary:referenced_byte_size(<<1:1>>) catch error:undef -> undef end,
    Huge = 1 bsl 200,
    badarg = try beam_binary:copy(<<>>, Huge) catch error:badarg -> badarg end,
    undef = try binary:copy(<<>>, Huge) catch error:undef -> undef end,
    ok.

%% Long improper input forces the two retained continuation policies to differ.
reverse_error(Policy) ->
    Original=long_improper(1000000,bad_tail),
    try case Policy of
            modern -> beam_lists:reverse(Original, tail);
            canonical -> lists:reverse(Original,tail)
        end of _ -> error(expected_reverse_error)
    catch error:badarg:Stack ->
        [{M,reverse,Args,_}|_]=Stack,
        case Policy of
            modern -> beam_lists=M, true=(Args=/=[Original,tail]);
            canonical -> lists=M, true=(Args=/=[Original,tail])
        end
    end,
    ok.
long_improper(0,Tail) -> Tail;
long_improper(N,Tail) -> [N|long_improper(N-1,Tail)].

none() ->
    #{1:=true,2:=true}=#{K => true || K <- [1,2,1]},
    #{1:=2,2:=3}=#{K => K+1 || K <- [1,2,1]},
    #{}=#{K => true || K <- []},
    ok = composition(false, false),
    [] = beam_lists:map(fun(X) -> X end, []),
    initial = beam_lists:foldl(fun(_, Acc) -> Acc end, initial, []),
    [] = beam_string:to_graphemes([]),
    undef = try beam_unicode:characters_to_binary([<<"x">>]) catch error:undef -> undef end,
    undef = try beam_atomics:new(1, []) catch error:undef -> undef end,
    undef = try beam_counters:new(1, []) catch error:undef -> undef end,
    undef = try beam_persistent_term:get() catch error:undef -> undef end,
    ok = unbound(modern_entries()),
    undef = try apply(beam_lists, member, [x,x]) catch error:undef -> undef end,
    undef = try apply(beam_lists, reverse, [x,x]) catch error:undef -> undef end,
    undef = try apply(beam_maps, find, [x,x]) catch error:undef -> undef end,
    undef = try apply(beam_maps, put, [x,x,x]) catch error:undef -> undef end,
    undef = try apply(beam_unicode, characters_to_binary, [x]) catch error:undef -> undef end,
    false = erlang:is_builtin(beam_binary, split, 2),
    false = erlang:is_builtin(binary, split, 2),
    false = erlang:is_builtin(erlang, display, 1),
    false = contains({binary, split, 2}, erlang:system_info(snifs)),
    false = contains({beam_binary, split, 2}, erlang:system_info(snifs)),
    false = contains({'bound.bytes', cut, 2}, erlang:system_info(snifs)),
    undef = try beam_binary:split(<<"a">>, <<"b">>) catch error:undef -> undef end,
    undef = try binary:split(<<"a">>, <<"b">>) catch error:undef -> undef end,
    ok.

ordinary() ->
    false = erlang:is_builtin(binary, split, 2),
    ordinary = binary:split(<<"ab">>, <<"a">>),
    ok.

large() ->
    Subject = beam_binary:copy(<<",a">>, 40000),
    40000 = length(beam_binary:split(Subject, <<",">>, [global,trim_all])),
    ok.

names_public_arguments() ->
    try beam_binary:split(x, <<"a">>) of
        _ -> false
    catch error:badarg:Stack -> names(Stack)
    end.

names([{beam_binary, split, [x, <<"a">>], _}|_]) -> true;
names([{beam_binary, split, _, _}|Rest]) -> names(Rest);
names([_|Rest]) -> names(Rest);
names([]) -> false.

renamed() ->
    <<>> = 'bound.bytes':repeat(<<>>,1 bsl 200),
    <<"abab">> = 'bound.bytes':repeat(<<"ab">>,2),
    2 = 'bound.bytes':referenced_size(<<"ab">>),
    badarg = try 'bound.bytes':referenced_size(<<1:1>>) catch error:badarg -> badarg end,
    {error,#{kind:=invalid_data}} = 'bound.bytes':valid(<<237,160,128>>),
    {error,#{kind:=invalid_data}} = 'bound.bytes':valid(<<226,130>>),
    Original=long_improper(1000000,bad_tail),
    try 'bound.bytes':back(Original,tail) of _ -> error(expected_reverse_error)
    catch error:badarg:ReverseStack ->
        [{'bound.bytes',back,[Original,tail],_}|_] = ReverseStack
    end,
    ok = renamed_state(),
    ok = persistent_traps(),
    true = 'bound.bytes':has([a], a),
    [b,a|tail] = apply('bound.bytes', back, [[a,b],tail]),
    Lookup = fun 'bound.bytes':lookup/2,
    {ok,1} = Lookup(#{a=>1}, a),
    #{a:=1} = 'bound.bytes':insert(#{}, a, 1),
    {ok,<<"ok">>} = 'bound.bytes':valid(<<"ok">>),
    false = erlang:module_loaded(unicode),
    <<16#20AC/utf8>> = 'bound.bytes':utf8(<<16#20AC:16>>, utf16),
    [16#20AC] = apply('bound.bytes', chars, [<<16#20AC:32/little>>, {utf32,little}]),
    {incomplete,<<>>,<<0>>} = 'bound.bytes':utf8(<<0>>, utf16),

    true = erlang:is_builtin('bound.bytes', seek, 2),
    true = erlang:is_builtin('bound.bytes', seek, 3),
    {ok, {1, 1}} = 'bound.bytes':seek(<<"banana">>, <<"a">>),
    {ok, {3, 1}} = apply('bound.bytes', seek, [<<"banana">>, <<"a">>, #{scope => {2, 4}}]),
    Find = fun 'bound.bytes':seek/2,
    not_found = Find(<<"banana">>, <<"z">>),
    try 'bound.bytes':seek(<<"x">>, <<"x">>, #{scope => {0, -1}}) of
        _ -> error(expected_search_badarg)
    catch error:badarg:SearchStack ->
        [{'bound.bytes', seek, [<<"x">>, <<"x">>, #{scope := {0, -1}}], _}|_] = SearchStack
    end,
    true = erlang:is_builtin('bound.bytes', cut, 2),
    true = erlang:is_builtin('bound.bytes', cut, 3),
    false = erlang:is_builtin(binary, split, 2),
    false = erlang:is_builtin(beam_binary, split, 2),
    [<<"b">>, <<"nana">>] = 'bound.bytes':cut(<<"banana">>, <<"a">>),
    [<<"b">>, <<"nana">>] = apply('bound.bytes', cut, [<<"banana">>, <<"a">>]),
    F = fun 'bound.bytes':cut/2,
    [<<"b">>, <<"nana">>] = F(<<"banana">>, <<"a">>),
    try 'bound.bytes':cut(x, <<"a">>) of
        _ -> error(expected_badarg)
    catch error:badarg:Stack ->
        [{'bound.bytes', cut, [x, <<"a">>], _}|_] = Stack,
        ok
    end,
    try 'bound.bytes':cut(<<"x">>, <<"a">>, [global]) of
        _ -> error(expected_options_badarg)
    catch error:badarg:OptionsStack ->
        [{'bound.bytes', cut, [<<"x">>, <<"a">>, [global]], _}|_] = OptionsStack,
        ok
    end,
    200000 = byte_size('bound.bytes':flatten(repeat_piece(100000))),
    true = contains({'bound.bytes', cut, 2}, erlang:system_info(snifs)),
    false = contains({binary, split, 2}, erlang:system_info(snifs)),
    false = contains({beam_binary, split, 2}, erlang:system_info(snifs)),
    1 = 'bound.bytes':version(),
    Parent = self(),
    Tracer = erlang:spawn(fun() -> receive Event -> Parent ! {observed, Event} end end),
    1 = erts_internal:trace_pattern({'bound.bytes', cut, 2}, true, []),
    1 = erts_internal:trace(self(), true, [call, {tracer, builtin_tracer, Tracer}]),
    [<<"b">>, <<"nana">>] = 'bound.bytes':cut(<<"banana">>, <<"a">>),
    1 = erts_internal:trace(self(), false, [call]),
    1 = erts_internal:trace_pattern({'bound.bytes', cut, 2}, false, []),
    receive {observed, {trace, Parent, call, {'bound.bytes', cut, [<<"banana">>, <<"a">>]}}} -> ok
    after 5000 -> error(bound_trace_timeout)
    end,
    Subject = erlang:iolist_to_binary(repeat_piece(40000)),
    40000 = length('bound.bytes':cut(Subject, <<",">>, #{global => true, trim => all})),
    ok.

retained() ->
    1 = 'bound.bytes':version(),
    [<<"b">>, <<"nana">>] = 'bound.bytes':cut(<<"banana">>, <<"a">>),
    ok.

reloaded() ->
    2 = 'bound.bytes':version(),
    [<<"b">>, <<"nana">>] = 'bound.bytes':cut(<<"banana">>, <<"a">>),
    ok.

unloaded() ->
    false = erlang:module_loaded('bound.bytes'),
    true = erlang:is_builtin('bound.bytes',cut,2),
    Parts = [<<"b">>,<<"nana">>],
    Parts = 'bound.bytes':cut(<<"banana">>,<<"a">>),
    Parts = apply('bound.bytes',cut,[<<"banana">>,<<"a">>]),
    Cut = fun 'bound.bytes':cut/2,
    Parts = Cut(<<"banana">>,<<"a">>),
    undef = try 'bound.bytes':version() catch error:undef -> undef end,
    ok.

contains(X, [X|_]) -> true;
contains(X, [_|Rest]) -> contains(X, Rest);
contains(_, []) -> false.

repeat_piece(0) -> [];
repeat_piece(N) -> [<<",a">>|repeat_piece(N - 1)].

%% Return with a live bound continuation so VM termination owns its cleanup.
abandon() ->
    Parent = self(),
    Subject = <<0:(16 * 1024 * 1024 * 8)>>,
    Pid = erlang:spawn(fun() ->
        Parent ! {bound_started, self()},
        Result = 'bound.bytes':cut(Subject, <<0>>, #{global => true}),
        Parent ! {bound_completed, self(), length(Result)}
    end),
    receive {bound_started, Pid} -> ok after 5000 -> error(bound_start_timeout) end,
    ok = bound_active(Pid, 100),
    Search = erlang:spawn(fun() ->
        Parent ! {search_started, self()},
        _ = 'bound.bytes':ranges(Subject, <<0>>),
        Parent ! {search_finished, self()}
    end),
    receive {search_started, Search} -> ok after 5000 -> error(search_start_timeout) end,
    ok = search_active(Search, ranges, 100),
    Unicode = erlang:spawn(fun() -> 'bound.bytes':chars(Subject, utf16) end),
    ok = search_active(Unicode, chars, 100),
    List = bound_list(1000000, []),
    Reversal = erlang:spawn(fun() -> 'bound.bytes':back(List, []) end),
    ok = search_active(Reversal, back, 100),
    ArrayOptions = bound_options(2000000, []),
    Array = erlang:spawn(fun() -> 'bound.bytes':a_new(1, ArrayOptions) end),
    ok = search_active(Array, a_new, 1000),
    {_Persistent,_Monitor} = persistent_holder(p_info),
    ok.

bound_active(_, 0) -> error(bound_never_became_active);
bound_active(Pid, Attempts) ->
    receive {bound_completed, Pid, _} -> error(bound_completed_before_teardown)
    after 0 -> ok end,
    case process_info(Pid, [current_function, reductions]) of
        [{current_function, {M, F, _}}, {reductions, R}]
          when R >= 1000,
               (M =:= 'bound.bytes' andalso F =:= cut) orelse
               (M =:= erlang andalso F =:= binary_find_trap) -> ok;
        undefined -> error(bound_owner_disappeared);
        _ -> receive after 1 -> ok end, bound_active(Pid, Attempts - 1)
    end.

reload(Bytes) ->
    1 = 'bound.bytes':version(),
    case erlang:prepare_loading('bound.bytes', Bytes) of
        {error, badfile} -> retained();
        Prepared when is_reference(Prepared) ->
            ok = erlang:finish_loading([Prepared]),
            reloaded()
    end.

modern_entries() -> otp_entries().

otp_entries() ->
    [{compile_pattern,1},{match,2},{match,3},{matches,2},{matches,3},
     {longest_common_prefix,1},{longest_common_suffix,1},{first,1},{last,1},{at,2},
     {part,2},{part,3},{list_to_bin,1},{copy,1},{copy,2},{referenced_byte_size,1},
     {encode_unsigned,1},{encode_unsigned,2},{decode_unsigned,1},{decode_unsigned,2},
     {split,2},{split,3}].

ets_entries() ->
    [{internal_request_all,0},
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
     {update_element,4}].

composition(Modern, Otp) ->
    ok = entries(beam_binary, modern_entries(), case Otp of mixed -> mixed_modern; _ -> Modern end),
    ok = entries(binary, otp_entries(), Otp),
    ok = entries(beam_ets, ets_entries(), Modern),
    ok = entries(ets, ets_entries(), Otp),
    Any = Modern orelse Otp =/= false,
    Canonical = Otp =:= true,
    Any = erlang:is_builtin(erts_internal, ets_info_binary, 1),
    Canonical = erlang:is_builtin(erlang, match_spec_test, 3),
    Modern = erlang:is_builtin(beam, match_spec_test, 3),
    ok = entries(beam_lists, [{member,2},{reverse,2},{keymember,3},{keysearch,3},{keyfind,3}], Modern),
    ok = entries(beam_maps, [{find,2},{get,2},{from_list,1},{from_keys,2},{is_key,2},{keys,1},{merge,2},{put,3},{remove,2},{update,3},{values,1},{take,2}], Modern),
    ok = entries(beam_unicode, [{characters_to_binary,2},{characters_to_list,2},{bin_is_7bit,1}], Modern),
    ok = entries(beam_string,[{list_to_float,1}],Modern),
    ok = entries(lists, [{member,2},{reverse,2},{keymember,3},{keysearch,3},{keyfind,3}], Otp),
    ok = entries(maps, [{find,2},{get,2},{from_list,1},{is_key,2},{keys,1},{merge,2},{put,3},{remove,2},{update,3},{values,1},{take,2},{from_keys,2}], Otp),
    ok = entries(string, [{list_to_float,1}], Otp),
    ok = entries(unicode, [{characters_to_binary,2},{characters_to_list,2},{bin_is_7bit,1}], Otp),
    ok = entries(beam_atomics, [{new,2},{get,2},{put,3},{add,3},{add_get,3},
        {exchange,3},{compare_exchange,4},{info,1}], Modern),
    ok = entries(beam_counters, [{new,2},{get,2},{add,3},{put,3},{info,1}], Modern),
    ok = entries(beam_persistent_term, [{put,2},{put_new,2},{get,0},{get,1},{get,2},{erase,1},{info,0}], Modern),
    ok = entries(atomics, [{get,2},{put,3},{add,3},{add_get,3},{exchange,3},{compare_exchange,4},{info,1}], Otp),
    ok = entries(persistent_term, [{put,2},{put_new,2},{get,0},{get,1},{get,2},{erase,1},{info,0}], Otp),
    false = erlang:is_builtin(atomics, new, 2),
    false = erlang:is_builtin(atomics, sub, 3),
    false = erlang:is_builtin(atomics, sub_get, 3),
    false = erlang:is_builtin(counters, new, 2),
    false = erlang:is_builtin(counters, get, 2),
    false = erlang:is_builtin(counters, add, 3),
    false = erlang:is_builtin(counters, sub, 3),
    false = erlang:is_builtin(counters, put, 3),
    false = erlang:is_builtin(counters, info, 1),
    Any = erlang:is_builtin(erts_internal, atomics_new, 2),
    Any = erlang:is_builtin(erts_internal, counters_new, 1),
    Any = erlang:is_builtin(erts_internal, counters_get, 2),
    Any = erlang:is_builtin(erts_internal, counters_add, 3),
    Any = erlang:is_builtin(erts_internal, counters_put, 3),
    Any = erlang:is_builtin(erts_internal, counters_info, 1),
    ok = entries(erts_internal,
        [{bytes_copy,2},{bytes_referenced_size,1},{bytes_compile_pattern,1},
         {bytes_match,3},{bytes_matches,3},{bytes_common_prefix_size,1},
         {bytes_common_suffix_size,1},{bytes_encode_unsigned,2},{bytes_decode_unsigned,2}], false),
    ok.

entries(_, [], _) -> ok;
entries(Module, [{Function,Arity}|Rest], Installed) ->
    Expected = selected(Module, Function, Arity, Installed),
    Expected = erlang:is_builtin(Module, Function, Arity),
    Expected = contains({Module,Function,Arity}, erlang:system_info(snifs)),
    case Module of
        erts_internal -> false = erlang:function_exported(Module, Function, Arity);
        _ -> ok
    end,
    entries(Module, Rest, Installed).

selected(beam_binary,F,A,mixed_modern) -> not contains({F,A},[{split,3},{match,3}]);
selected(binary,F,A,mixed) -> contains({F,A},[{split,3},{match,3}]);
selected(ets, _, _, mixed) -> false;
selected(atomics, _, _, mixed) -> false;
selected(persistent_term, _, _, mixed) -> false;
selected(_, _, _, mixed) -> false;
selected(_, _, _, Expected) -> Expected.

unbound([]) -> ok;
unbound([{Function,Arity}|Rest]) ->
    Arguments = case Arity of 1 -> [x]; 2 -> [x,x]; 3 -> [x,x,x] end,
    undef = try apply(beam_binary, Function, Arguments) catch error:undef -> undef end,
    unbound(Rest).

%% Observe a live native continuation before validating its eventual result.
search_yields() ->
    Zeros = <<0:(64 * 1024 * 1024 * 8)>>,
    Hit = <<Zeros/binary, 1, 2>>,
    Size = byte_size(Zeros),
    ok = yielded_search(seek, [Hit, <<1,2>>],
        fun() -> 'bound.bytes':seek(Hit, <<1,2>>) end, {ok,{Size,2}}),
    ok = yielded_search(seek, [Zeros, <<1,2>>],
        fun() -> 'bound.bytes':seek(Zeros, <<1,2>>) end, not_found),
    Patterns = [<<1,2>>, <<1,2,3>>],
    ok = yielded_search(seek, [Hit, Patterns],
        fun() -> 'bound.bytes':seek(Hit, Patterns) end, {ok,{Size,2}}),
    ok = yielded_search(seek, [Zeros, Patterns],
        fun() -> 'bound.bytes':seek(Zeros, Patterns) end, not_found),
    Global = <<0:(1024 * 1024 * 8)>>,
    ok = yielded_search(ranges, [Global, <<0>>],
        fun() -> range_count('bound.bytes':ranges(Global, <<0>>), 0) end, byte_size(Global)),
    List = bound_list(1000000, []),
    ok = yielded_search(has, [List,missing], fun() -> 'bound.bytes':has(List,missing) end, false),
    ok = yielded_search(back, [List,[]], fun() -> length('bound.bytes':back(List,[])) end, 1000000),
    ok = yielded_search(valid, [Zeros], fun() -> 'bound.bytes':valid(Zeros) end, {ok,Zeros}),
    ok = traced_bound(lookup, [#{a=>1},a], fun() -> 'bound.bytes':lookup(#{a=>1},a) end, {ok,1}, false),
    ok = traced_bound(insert, [#{},a,1], fun() -> 'bound.bytes':insert(#{},a,1) end, #{a=>1}, false),
    BadUnicode = [Global|bad],
    ok = yielded_search(utf8, [BadUnicode,utf16],
        fun() -> bound_badarg(utf8,[BadUnicode,utf16]) end, ok),
    ok = yielded_search(chars, [Global,utf16],
        fun() -> length('bound.bytes':chars(Global,utf16)) end, byte_size(Global) div 2),
    Nested = bound_nest(100000, [16#110000]),
    Rest = bound_nest(100001, [16#110000]),
    ok = yielded_search(utf8, [Nested,utf16],
        fun() -> 'bound.bytes':utf8(Nested,utf16) end, {error,<<>>,Rest}),
    ok = kill_bound(fun() -> 'bound.bytes':valid(Zeros) end, valid),
    ok = kill_bound(fun() -> 'bound.bytes':utf8(Zeros,utf16) end, utf8),
    ok = kill_bound(fun() -> 'bound.bytes':chars(Zeros,utf32) end, chars),
    ok.

bound_list(0, Tail) -> Tail;
bound_list(N, Tail) -> bound_list(N-1, [N|Tail]).

bound_nest(0, Term) -> Term;
bound_nest(N, Term) -> bound_nest(N-1, [Term]).

bound_badarg(Function, Arguments) ->
    try apply('bound.bytes',Function,Arguments) of
        _ -> error(expected_bound_badarg)
    catch error:badarg:Stack ->
        case Stack of
            [{'bound.bytes',Function,Actual,_}|_] ->
                true = Actual =:= Arguments,
                ok;
            _ -> error(unexpected_bound_error_identity)
        end
    end.

kill_bound(Work, Function) ->
    {Pid,Mon} = erlang:spawn_monitor(fun() -> Work() end),
    ok = search_active(Pid, Function, 100),
    exit(Pid, kill),
    receive {'DOWN',Mon,process,Pid,killed} -> ok
    after 5000 -> error(bound_kill_timeout) end.


yielded_search(Function, Arguments, Work, Expected) ->
    yielded_search('bound.bytes', Function, Arguments, Work, Expected).

yielded_search(Module, Function, Arguments, Work, Expected) ->
    traced_bound(Module, Function, Arguments, Work, Expected, true).

traced_bound(Function, Arguments, Work, Expected, Observe) ->
    traced_bound('bound.bytes', Function, Arguments, Work, Expected, Observe).

traced_bound(Module, Function, Arguments, Work, Expected, Observe) ->
    Arity = length(Arguments),
    Parent = self(),
    Tracer = erlang:spawn(fun() -> receive Event -> Parent ! {observed_search, Event} end end),
    {Pid, Monitor} = erlang:spawn_monitor(fun() ->
        receive go -> ok end,
        Parent ! {search_started, self()},
        Expected = Work(),
        Parent ! {search_finished, self()}
    end),
    1 = erts_internal:trace_pattern({Module, Function, Arity}, true, []),
    1 = erts_internal:trace(Pid, true, [call, {tracer, builtin_tracer, Tracer}]),
    Pid ! go,
    receive {search_started, Pid} -> ok after 5000 -> error(search_start_timeout) end,
    case Observe of true -> ok = search_active(Pid, Function, 100); false -> ok end,
    receive {observed_search, {trace, Pid, call, {Module, Function, Arguments}}} -> ok
    after 5000 -> error(search_trace_timeout) end,
    receive {search_finished, Pid} -> ok after 5000 -> error(search_finish_timeout) end,
    receive {'DOWN', Monitor, process, Pid, normal} -> ok
    after 5000 -> error(search_owner_timeout) end,
    1 = erts_internal:trace_pattern({Module, Function, Arity}, false, []),
    ok.

search_active(_, _, 0) -> error(search_never_yielded);
search_active(Pid, Function, Attempts) ->
    receive {search_finished, Pid} -> error(search_finished_before_observation)
    after 0 -> ok end,
    case process_info(Pid, [current_function, reductions]) of
        [{current_function, {Module, Name, _}}, {reductions, R}]
          when R >= 1000,
               ((Module =:= 'bound.bytes' orelse Module =:= 'bound.tables') andalso Name =:= Function) orelse
               (Module =:= erlang andalso (Name =:= binary_find_trap orelse Name =:= unicode_decode_continue)) orelse
               (Module =:= erts_internal andalso (Name =:= term_table_select_continue orelse
                                                 Name =:= term_table_binary_info_continue)) -> ok;
        undefined -> error(search_owner_disappeared);
        _ -> receive after 1 -> ok end, search_active(Pid, Function, Attempts - 1)
    end.

range_count([], Count) -> Count;
range_count([{Count,1}|Rest], Count) -> range_count(Rest, Count + 1).

renamed_state() ->
    A = 'bound.bytes':a_new(2, []),
    ok = apply('bound.bytes', a_put, [A,1,8]),
    Get = fun 'bound.bytes':a_get/2,
    8 = Get(A, 1),
    ok = 'bound.bytes':a_add(A, 1, 2),
    11 = 'bound.bytes':a_add_get(A, 1, 1),
    11 = 'bound.bytes':a_exchange(A, 1, 12),
    ok = 'bound.bytes':a_compare_exchange(A, 1, 12, 13),
    13 = 'bound.bytes':a_exchange(A, 1, 14),
    ok = 'bound.bytes':a_compare_exchange(A, 1, 14, 15),
    16 = 'bound.bytes':a_add_get(A, 1, 1),
    ok = 'bound.bytes':a_add(A, 1, 1),
    17 = 'bound.bytes':a_get(A, 1),
    #{size:=2} = 'bound.bytes':a_info(A),
    ok = 'bound.bytes':a_put(A, 2, 1),
    C = 'bound.bytes':c_new(1, [atomics]),
    ok = 'bound.bytes':c_put(C, 1, 2),
    ok = 'bound.bytes':c_add(C, 1, 3),
    5 = 'bound.bytes':c_get(C, 1),
    #{size:=1,min:=-9223372036854775808} = 'bound.bytes':c_info(C),
    ok = 'bound.bytes':p_put(key, A),
    A = 'bound.bytes':p_get(key),
    ok = 'bound.bytes':p_put_new(key, A),
    A = 'bound.bytes':p_get(key, missing),
    A = 'bound.bytes':p_get(key),
    A = 'bound.bytes':p_get(key, missing),
    true = contains({key,A}, 'bound.bytes':p_get()),
    #{count:=Count} = 'bound.bytes':p_info(), true = Count > 0,
    true = 'bound.bytes':p_erase(key),
    ok = 'bound.bytes':p_put(key, C),
    ok = 'bound.bytes':p_put_new(key, C),
    true = 'bound.bytes':p_erase(key),
    false = erlang:is_builtin(atomics, get, 2),
    false = erlang:is_builtin(persistent_term, get, 1),
    try 'bound.bytes':a_new(bad, [bad]) of
        _ -> error(expected_array_badopt)
    catch error:badarg:Stack ->
        [{'bound.bytes',a_new,[bad,[bad]],Location}|_] = Stack,
        {error_info,#{cause:=badopt}} = bound_error_info(Location)
    end,
    Options = bound_options(1000000, [bad]),
    Parent = self(),
    {Pid,Mon} = erlang:spawn_monitor(fun() ->
        receive go -> ok end,
        try 'bound.bytes':a_new(bad, Options) of
            _ -> error(expected_late_array_badopt)
        catch error:badarg:LateStack ->
            [{'bound.bytes',a_new,[bad,Actual],Info}|_] = LateStack,
            true = Actual =:= Options,
            {error_info,#{cause:=badopt}} = bound_error_info(Info)
        end,
        Parent ! {array_late_error,self()}
    end),
    Pid ! go,
    ok = search_active(Pid, a_new, 1000),
    receive {array_late_error,Pid} -> ok after 10000 -> error(array_error_timeout) end,
    receive {'DOWN',Mon,process,Pid,normal} -> ok after 5000 -> error(array_exit_timeout) end,
    ok = kill_bound(fun() -> 'bound.bytes':a_new(1, Options) end, a_new),
    ok = bound_trace(a_get, [A,1]),
    ok = bound_trace(c_get, [C,1]),
    ok = bound_trace(p_put, [traced,A]),
    true = 'bound.bytes':p_erase(traced),
    ok.

bound_options(0, Tail) -> Tail;
bound_options(N, Tail) -> bound_options(N-1, [{signed,true}|Tail]).

bound_error_info([Entry={error_info,_}|_]) -> Entry;
bound_error_info([_|Rest]) -> bound_error_info(Rest).

bound_trace(Function, Arguments) ->
    Parent = self(),
    Tracer = erlang:spawn(fun() -> receive Event -> Parent ! {state_trace,Event} end end),
    Arity = length(Arguments),
    1 = erts_internal:trace_pattern({'bound.bytes',Function,Arity}, true, []),
    1 = erts_internal:trace(self(), true, [call,{tracer,builtin_tracer,Tracer}]),
    _ = apply('bound.bytes', Function, Arguments),
    1 = erts_internal:trace(self(), false, [call]),
    1 = erts_internal:trace_pattern({'bound.bytes',Function,Arity}, false, []),
    receive {state_trace,{trace,Parent,call,{'bound.bytes',Function,Arguments}}} -> ok
    after 5000 -> error(bound_state_trace_timeout) end.

%% Hold an observed native enumeration across competing updates. The store
%% permission and native contexts must survive suspension, cancellation and GC.
persistent_traps() ->
    ok = persistent_fill(12000),
    {Holder,HolderMon} = persistent_holder(p_get),
    Parent = self(),
    {Waiter,WaiterMon} = erlang:spawn_monitor(fun() ->
        Parent ! {persistent_waiter,self()},
        ok = bound_badarg(p_put_new, [{state_key,1},different]),
        Parent ! {persistent_badarg,self()}
    end),
    receive {persistent_waiter,Waiter} -> ok after 5000 -> error(store_waiter_start) end,
    ok = persistent_waiting(Waiter, 1000),
    true = erlang:resume_process(Holder),
    receive {persistent_result,Holder,12000} -> ok after 10000 -> error(store_get_timeout) end,
    receive {'DOWN',HolderMon,process,Holder,normal} -> ok after 5000 -> error(store_get_exit) end,
    receive {persistent_badarg,Waiter} -> ok after 10000 -> error(store_badarg_timeout) end,
    receive {'DOWN',WaiterMon,process,Waiter,normal} -> ok after 5000 -> error(store_badarg_exit) end,
    {Info,InfoMon} = persistent_holder(p_info),
    {Cancelled,CancelledMon} = erlang:spawn_monitor(fun() ->
        Parent ! {persistent_waiter,self()},
        'bound.bytes':p_put_new(cancelled_update, {'bound.bytes':a_new(1, []), <<0:524288>>})
    end),
    receive {persistent_waiter,Cancelled} -> ok after 5000 -> error(store_cancel_start) end,
    ok = persistent_waiting(Cancelled, 1000),
    true = exit(Cancelled, kill),
    receive {'DOWN',CancelledMon,process,Cancelled,killed} -> ok after 5000 -> error(store_cancel_kill) end,
    {Successor,SuccessorMon} = erlang:spawn_monitor(fun() ->
        Parent ! {persistent_waiter,self()},
        ok = 'bound.bytes':p_put_new(cancelled_update, recovered),
        Parent ! {persistent_recovered,self()}
    end),
    receive {persistent_waiter,Successor} -> ok after 5000 -> error(store_successor_start) end,
    %% Cancelling a queued update must not release another process's permission.
    ok = persistent_waiting(Successor, 1000),
    true = exit(Info, kill),
    receive {'DOWN',InfoMon,process,Info,killed} -> ok after 5000 -> error(store_info_kill) end,
    receive {persistent_recovered,Successor} -> ok after 10000 -> error(store_recovery_timeout) end,
    receive {'DOWN',SuccessorMon,process,Successor,normal} -> ok after 5000 -> error(store_recovery_exit) end,
    recovered = 'bound.bytes':p_get(cancelled_update),
    true = 'bound.bytes':p_erase(cancelled_update),
    %% An info continuation owns update permission; killing it must release it.
    ok = 'bound.bytes':p_put(after_info_kill, ok),
    true = 'bound.bytes':p_erase(after_info_kill),
    {Getter,GetterMon} = persistent_holder(p_get),
    true = exit(Getter, kill),
    receive {'DOWN',GetterMon,process,Getter,killed} -> ok after 5000 -> error(store_get_kill) end,
    ok = 'bound.bytes':p_put(after_get_kill, ok),
    true = 'bound.bytes':p_erase(after_get_kill),
    ok.

persistent_fill(0) -> ok;
persistent_fill(N) ->
    ok = 'bound.bytes':p_put({state_key,N}, N),
    persistent_fill(N-1).

persistent_holder(Function) ->
    Parent = self(),
    {Pid,Mon} = erlang:spawn_monitor(fun() ->
        Parent ! {persistent_started,self()},
        Value = apply('bound.bytes', Function, []),
        Count = case Function of p_get -> length(Value); p_info -> map_get(count,Value) end,
        Parent ! {persistent_result,self(),Count}
    end),
    receive {persistent_started,Pid} -> ok after 5000 -> error(store_start_timeout) end,
    ok = persistent_active(Pid, Function, 1000),
    true = erlang:suspend_process(Pid),
    {Pid,Mon}.

persistent_active(_, _, 0) -> error(persistent_never_yielded);
persistent_active(Pid, Function, Attempts) ->
    case process_info(Pid, current_function) of
        {current_function,{erlang,Name,_}} when Name =:= persistent_value_get_all_continue;
                                                  Name =:= persistent_value_info_continue -> ok;
        {current_function,{'bound.bytes',Function,_}} -> ok;
        undefined -> error(persistent_finished_before_observation);
        _ -> erlang:yield(), persistent_active(Pid, Function, Attempts-1)
    end.

persistent_waiting(_, 0) -> error(persistent_never_waited);
persistent_waiting(Pid, Attempts) ->
    case process_info(Pid, status) of
        {status,suspended} -> ok;
        undefined -> error(persistent_waiter_disappeared);
        _ -> erlang:yield(), persistent_waiting(Pid, Attempts-1)
    end.

%% Deliberately no select/1 or safe_fixtable/2 binding in this VM.
ets_partial() ->
    false=erlang:is_builtin(ets,new,2),
    false=erlang:is_builtin(beam_ets,new,2),
    false=erlang:module_loaded('bound.tables'),
    false=erlang:is_builtin('bound.tables',t_select,1),
    false=erlang:is_builtin('bound.tables',safe_fixtable,2),
    Rows=ets_rows(40000,[]),
    T='bound.tables':t_new(partial_table,[ordered_set,public]),
    true=apply('bound.tables',t_insert,[T,Rows]),
    Lookup=fun 'bound.tables':t_lookup/2,
    [{1,<<0:1024>>}]=Lookup(T,1),
    Spec=[{'_',[],['$_']}],
    ok=yielded_search('bound.tables',t_select,[T,Spec],fun()->length('bound.tables':t_select(T,Spec)) end,40000),
    ok=yielded_search('bound.tables',t_match,[T,'$1'],fun()->length('bound.tables':t_match(T,'$1')) end,40000),
    ok=yielded_search('bound.tables',t_info,[T,binary],fun()->length('bound.tables':t_info(T,binary)) end,40000),
    {Chunk,Cont}='bound.tables':t_chunk(T,Spec,7),
    7=length(Chunk),true=is_tuple(Cont),
    P='bound.tables':t_new(private_diagnostic,[private]),
    true='bound.tables':t_insert(P,{binary,<<0:1024>>}),
    Parent=self(),
    {Reader,RM}=erlang:spawn_monitor(fun()->
        [{_,128,_}]='bound.tables':t_info(P,binary), Parent!private_info_ok
    end),
    receive private_info_ok->ok after 5000->error(private_info_timeout) end,
    receive {'DOWN',RM,process,Reader,normal}->ok after 5000->error(private_info_exit) end,
    true='bound.tables':t_drop(P),
    try 'bound.tables':t_info(42,binary) of _->error(expected_info_error)
    catch error:badarg:InfoError -> [{'bound.tables',t_info,[42,binary],_}|_]=InfoError, ok end,
    ok=ets_deleted_during_yield(T,Spec),
    true='bound.tables':t_info(T,size)=:=undefined,
    T2='bound.tables':t_new(cancelled_table,[ordered_set,public]),
    true='bound.tables':t_insert(T2,Rows),
    ok=ets_kill_info(T2),
    false='bound.tables':t_info(T2,fixed),
    true='bound.tables':t_drop(T2),
    ok=ets_resource_lifetimes(),
    ok.

ets_rows(0,Acc)->Acc;
ets_rows(N,Acc)->ets_rows(N-1,[{N,<<0:1024>>}|Acc]).

ets_deleted_during_yield(T,Spec)->
    Parent=self(),
    {Pid,Mon}=erlang:spawn_monitor(fun()->
        Parent!{search_started,self()},
        try 'bound.tables':t_select(T,Spec) of _->error(expected_deleted_error)
        catch error:badarg:DeletedError -> [{'bound.tables',t_select,[T,Spec],_}|_]=DeletedError, Parent!deleted_error_ok end
    end),
    receive {search_started,Pid}->ok after 5000->error(deleted_start_timeout) end,
    ok=search_active(Pid,t_select,1000),
    true=erlang:suspend_process(Pid),
    true='bound.tables':t_drop(T),
    true=erlang:resume_process(Pid),
    receive deleted_error_ok->ok after 5000->error(deleted_error_timeout) end,
    receive {'DOWN',Mon,process,Pid,normal}->ok after 5000->error(deleted_exit) end,
    ok.

ets_kill_info(T)->
    Parent=self(),
    {Pid,Mon}=erlang:spawn_monitor(fun()->
        Parent!{search_started,self()},
        _='bound.tables':t_info(T,binary),
        Parent!unexpected_info_completion
    end),
    receive {search_started,Pid}->ok after 5000->error(info_start_timeout) end,
    ok=search_active(Pid,t_info,1000),
    true=exit(Pid,kill),
    receive {'DOWN',Mon,process,Pid,killed}->ok after 5000->error(info_kill_timeout) end,
    ok.


ets_resource_lifetimes()->
    0=builtin_tracer:resources(),Parent=self(),
    {Owner,OM}=erlang:spawn_monitor(fun()->
        T='bound.tables':t_new(resource_table,[public]),
        true='bound.tables':t_insert(T,{held,builtin_tracer:resource()}),
        Parent!{resource_table,self(),T},
        ets_table_wait()
    end),
    T=receive {resource_table,Owner,Table}->Table after 5000->error(resource_table_timeout) end,
    {Program,PM}=erlang:spawn_monitor(fun()->
        Spec=[{'_',[],[{const,builtin_tracer:resource()}]}],
        Compiled='bound.tables':t_compile(Spec),
        ets_program_wait(Compiled,Parent)
    end),
    receive program_ready->ok after 5000->error(program_ready_timeout) end,
    ok=ets_resource_count(2,1000),
    Program!drop,
    receive {'DOWN',PM,process,Program,normal}->ok after 5000->error(program_release_timeout) end,
    ok=ets_resource_count(1,1000),
    true='bound.tables':t_drop(T),
    ok=ets_resource_count(0,1000),
    Owner!stop,
    receive {'DOWN',OM,process,Owner,normal}->ok after 5000->error(table_owner_timeout) end,
    ok.

ets_table_wait()->
    true=garbage_collect(),
    receive stop->ok end.
ets_program_wait(Compiled,Parent)->
    true=garbage_collect(),
    Parent!program_ready,
    receive run->[_]='bound.tables':t_run([ignored],Compiled,[]),ets_program_wait(Compiled,Parent);
            drop->ok end.
ets_resource_count(N,0)->error({resource_count_timeout,N,builtin_tracer:resources()});
ets_resource_count(N,Attempts)->
    case builtin_tracer:resources() of
        N->ok;
        _->erlang:yield(),ets_resource_count(N,Attempts-1)
    end.

ets_abandon()->
    Parent=self(),
    {Owner,OM}=erlang:spawn_monitor(fun()->
        T='bound.tables':t_new(abandoned_ets,[public,ordered_set]),
        true='bound.tables':t_insert(T,ets_rows(100000,[])),
        true='bound.tables':t_insert(T,{retained_resource,builtin_tracer:resource()}),
        Parent!{ets_owner,self(),T},
        ets_table_wait()
    end),
    T=receive {ets_owner,Owner,Table}->Table;
              {'DOWN',OM,process,Owner,Reason}->error({ets_owner_failed,Reason})
      after 5000->error(ets_owner_timeout) end,
    Pid=erlang:spawn(fun()->
        Parent!{search_started,self()},
        _='bound.tables':t_info(T,binary),
        Parent!unexpected_abandoned_completion
    end),
    receive {search_started,Pid}->ok after 5000->error(ets_abandon_start) end,
    ok=search_active(Pid,t_info,1000),
    true=erlang:suspend_process(Pid),
    1=builtin_tracer:resources(),
    ok.

%% First ordinary load and on_load must retire native-only export tracing using
%% the same module accounting as an ordinary reload.
tables_trace() ->
    1=erts_internal:trace_pattern({'bound.tables',t_lookup,2},true,[]),
    {traced,global}=erlang:trace_info({'bound.tables',t_lookup,2},traced),
    ok.

tables_loaded() ->
    1='bound.tables':version(),
    {traced,false}=erlang:trace_info({'bound.tables',t_lookup,2},traced),
    ok.

tables_onload(Bytes) ->
    false=erlang:module_loaded('bound.tables'),
    Ref=erlang:prepare_loading('bound.tables',Bytes),
    true=erlang:has_prepared_code_on_load(Ref),
    {on_load,['bound.tables']}=erlang:finish_loading([Ref]),
    %% The native wrapper is independent of the loader's deferred code pointer.
    T=apply('bound.tables',t_new,[pending_load,[]]),
    true='bound.tables':t_drop(T),
    ok=erlang:call_on_load_function('bound.tables'),
    true=erlang:finish_after_on_load('bound.tables',true),
    ok.

tables_loaded_onload() ->
    2='bound.tables':version(),
    {traced,false}=erlang:trace_info({'bound.tables',t_lookup,2},traced),
    ok.

%% Native clocks and timers share implementations while their exports are selected.
modern_time() ->
    ok = time_exports(beam_time, true),
    ok = time_exports(erlang, false),
    false = erlang:function_exported(erlang, convert_time_unit, 3),
    false = erlang:function_exported(erlang, now, 0),
    undef = try apply(erlang, convert_time_unit, [1,second,millisecond])
            catch error:undef -> undef end,
    ok = time_contract(beam_time),
    infinity = beam_time:deadline_after(infinity),
    infinity = beam_time:remaining(infinity),
    false = beam_time:expired(infinity),
    D = beam_time:deadline_after(10000),
    false = beam_time:expired(D),
    true = beam_time:remaining(D) > 0,
    %% Termination must reclaim a timer that still owns a message/fun payload.
    Receiver = erlang:spawn(fun() -> receive finish -> ok end end),
    Pending = beam_time:send_after(60000, Receiver, {pending, fun modern_time/0}),
    true = beam_time:read_timer(Pending) > 0,
    ok.

otp_time() ->
    ok = time_exports(erlang, true),
    ok = time_exports(beam_time, false),
    true = erlang:function_exported(erlang, convert_time_unit, 3),
    true = erlang:is_builtin(erlang, now, 0),
    true = is_tuple(apply(erlang, now, [])),
    ok = time_contract(erlang).

no_time() ->
    ok = time_exports(erlang, false),
    false = erlang:function_exported(erlang, convert_time_unit, 3),
    false = erlang:function_exported(erlang, now, 0),
    %% Private continuation support remains usable without any time selection.
    -1 = erts_internal:clock_convert_time_unit(-1,millisecond,second),
    ok.

custom_time() ->
    false = erlang:module_loaded('bound.clock'),
    Read = fun 'bound.clock':read/1,
    true = is_integer(Read(1 bsl 100)),
    true = is_integer(apply('bound.clock', read, [perf_counter])),
    Ref = 'bound.clock':send(60000,self(),{pending,fun custom_time/0}),
    ok = 'bound.clock':cancel(Ref,[{async,true}]),
    receive {cancel_timer,Ref,N} when is_integer(N), N >= 0 -> ok
    after 5000 -> error(missing_custom_timer_reply) end,
    false = erlang:function_exported(erlang, send_after, 3),
    Parent = self(),
    {Tracee,Monitor} = erlang:spawn_monitor(fun() ->
        receive run_clock ->
            Value = 'bound.clock':read(native),
            Parent ! {clock_read,self(),Value},
            receive finish -> ok end
        end
    end),
    1 = erts_internal:trace_pattern({'bound.clock',read,1},true,[]),
    1 = erts_internal:trace(Tracee,true,[call,{tracer,builtin_tracer,Parent}]),
    Tracee ! run_clock,
    receive {trace,Tracee,call,{'bound.clock',read,[native]}} -> ok
    after 5000 -> error(missing_bound_clock_trace) end,
    receive {clock_read,Tracee,Value} when is_integer(Value) -> ok
    after 5000 -> error(missing_clock_read) end,
    1 = erts_internal:trace(Tracee,false,[call]),
    1 = erts_internal:trace_pattern({'bound.clock',read,1},false,[]),
    Tracee ! finish,
    receive {'DOWN',Monitor,process,Tracee,normal} -> ok
    after 5000 -> error(missing_clock_reader_down) end,
    ok.

time_exports(Module, Selected) ->
    Funs = [{monotonic_time,0},{monotonic_time,1},{system_time,0},{system_time,1},
            {time_offset,0},{time_offset,1},{timestamp,0},{start_timer,3},{start_timer,4},
            {send_after,3},{send_after,4},{cancel_timer,1},{cancel_timer,2},
            {read_timer,1},{read_timer,2}],
    [begin
         Selected = erlang:is_builtin(Module,F,A),
         Selected = erlang:function_exported(Module,F,A),
         case Module of
             erlang -> Selected = time_member({F,A},erlang:module_info(exports));
             _ -> ok
         end
     end || {F,A} <- Funs],
    case Selected of
        false -> undef = try apply(Module,timestamp,[]) catch error:undef -> undef end;
        true -> ok
    end,
    ok.

time_member(_,[]) -> false;
time_member(X,[X|_]) -> true;
time_member(X,[_|Rest]) -> time_member(X,Rest).

time_contract(M) ->
    Mono = M:monotonic_time(),
    true = M:monotonic_time() >= Mono,
    true = is_integer(M:monotonic_time(millisecond)),
    true = is_integer(M:monotonic_time(perf_counter)),
    true = is_integer(M:monotonic_time(1 bsl 100)),
    true = is_integer(M:system_time()),
    true = is_integer(M:system_time(nanosecond)),
    true = is_integer(M:time_offset()),
    true = is_integer(M:time_offset(microsecond)),
    OffsetBefore = M:time_offset(),
    MonoBefore = M:monotonic_time(),
    System = M:system_time(),
    MonoAfter = M:monotonic_time(),
    OffsetAfter = M:time_offset(),
    true = System >= MonoBefore + erlang:min(OffsetBefore,OffsetAfter),
    true = System =< MonoAfter + erlang:max(OffsetBefore,OffsetAfter),
    {Mega,Sec,Micro} = M:timestamp(),
    true = Mega >= 0 andalso Sec >= 0 andalso Sec < 1000000 andalso
           Micro >= 0 andalso Micro < 1000000,
    -1 = M:convert_time_unit(-1,millisecond,second),
    1000 = M:convert_time_unit(1,second,millisecond),
    1000 = M:convert_time_unit(1,seconds,milli_seconds),
    Huge = 1 bsl 200,
    Huge = M:convert_time_unit(Huge,native,native),
    Huge = M:convert_time_unit(Huge,perf_counter,perf_counter),
    Huge = M:convert_time_unit(1,1,Huge),
    [badarg = try M:convert_time_unit(T,F,U) of _ -> error(expected_badarg)
              catch error:badarg:ConversionStack ->
                  [{M,convert_time_unit,[T,F,U],_}|_] = ConversionStack, badarg
              end ||
        {T,F,U} <- [{a,second,second},{1,0,second},{1,second,-1},{1,bad,second}]],
    Fun = erlang:make_fun(M,timestamp,0),
    true = is_tuple(Fun()),
    true = is_integer(apply(M,monotonic_time,[native])),
    false = M:read_timer(make_ref()),
    false = M:cancel_timer(make_ref()),
    Ref = M:start_timer(0,self(),wrapped),
    receive {timeout,Ref,wrapped} -> ok after 5000 -> error(missing_wrapped_timer) end,
    Abs = M:monotonic_time(millisecond),
    _ = M:send_after(Abs,self(),absolute,[{abs,true}]),
    receive absolute -> ok after 5000 -> error(missing_absolute_timer) end,
    Long = M:start_timer(60000,self(),cancelled,[{abs,false}]),
    true = M:read_timer(Long) > 0,
    ok = M:read_timer(Long,[{async,true}]),
    receive {read_timer,Long,N} when is_integer(N), N > 0 -> ok
    after 5000 -> error(missing_read_reply) end,
    ok = M:cancel_timer(Long,[{async,true}]),
    receive {cancel_timer,Long,N2} when is_integer(N2), N2 > 0 -> ok
    after 5000 -> error(missing_cancel_reply) end,
    false = M:read_timer(Long,[{async,false}]),
    ok = M:cancel_timer(Long,[{info,false}]),
    true = register(bound_timer_receiver,self()),
    _ = M:send_after(0,bound_timer_receiver,registered_timer),
    receive registered_timer -> ok after 5000 -> error(missing_registered_timer) end,
    true = unregister(bound_timer_receiver),
    Dead = erlang:spawn(fun() -> ok end),
    Mon = monitor(process,Dead),
    receive {'DOWN',Mon,process,Dead,_} -> ok after 5000 -> error(missing_down) end,
    DeadTimer = M:send_after(60000,Dead,discarded),
    false = M:read_timer(DeadTimer),
    [badarg = try apply(M,F,Args) of _ -> error(expected_badarg)
              catch error:badarg:Stack ->
                  [{M,F,Args,_}|_] = Stack, badarg
              end || {F,Args} <- [{send_after,[-1,self(),bad]},
                  {start_timer,[0,self(),bad,[{unknown,true}]]},
                  {read_timer,[bad]},{cancel_timer,[bad]},
                  {monotonic_time,[bad]},{system_time,[bad]},{time_offset,[bad]}]],
    ok.

partial_time() ->
    true = erlang:is_builtin(erlang,timestamp,0),
    false = erlang:is_builtin(erlang,monotonic_time,0),
    false = erlang:function_exported(erlang,now,0),
    false = erlang:function_exported(beam_time,timestamp,0),
    true = erlang:function_exported(erlang,convert_time_unit,3),
    1000 = erlang:convert_time_unit(1,second,millisecond),
    true = is_tuple(erlang:timestamp()),
    ok.
