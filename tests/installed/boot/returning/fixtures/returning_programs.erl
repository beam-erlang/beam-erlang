%% SPDX-License-Identifier: Apache-2.0
%% Acceptance programs for the returning boot. Loaded explicitly, never embedded.
-module(returning_programs).
-record #contract_definition{field=default}.
-export_record([contract_definition]).
-export([ret_context/1, finish_then_halt/1, service_then_halt/1, return_then_halt/1, exception_then_halt/1, library/1, ret/1, boom/1, finish/1, halt/1, kill_output/1, cleanup/1,
         greeting/1, counter/1, counter_fault/1, code_client/1, code_missing/1, code_bad/1,
         file_report/2, file_edges/2, file_omitted/1, file_owner_death/1,
         echo/1, echo_partial/1, echo_peer/1, echo_owner/1, echo_fault/1, echo_abandon/1,
         record_service/1, record_fault/1, record_unreadable/1,
         zlib_explicit/1, zlib_abandon/1, zlib_persist/1, zlib_owner/1,
         output_cases/1, omitted/1, services/1, preloaded/1]).

library(_) ->
    true = preloaded_ok(),
    false = beam:module_loaded(beam_boot),
    false = beam:module_loaded(beam_file),
    false = beam:module_loaded(beam_socket),
    {error, <<>>, <<16#80>>} = beam_unicode:characters_to_binary(<<16#80>>),
    <<"ok">> = beam_unicode:characters_to_binary(<<"ok">>),
    <<"ab">> = beam_unicode:characters_to_binary([<<"a">>, <<"b">>]),
    <<"42">> = beam_format:integer(42),
    <<"...">> = beam_format:term([1, 2], #{depth => 0, bytes => 8}),
    <<"..">> = beam_format:term(123, #{depth => 2, bytes => 2}),
    ok = public_library_exports(),
    ok = byte_library_cases(),
    ok = collection_library_cases(),
    ok = state_library_cases(),
    ok = ets_library_cases(),
    {ok, 2} = beam_maps:find(b, #{a => 1, b => 2}),
    error = beam_maps:find(missing, #{}),
    [2, 4] = beam_lists:map(fun(N) -> N * 2 end, [1, 2]),
    6 = beam_lists:foldl(fun(N, Acc) -> N + Acc end, 0, [1, 2, 3]),
    {'EXIT', {x, _}} = (catch beam_lists:map(fun(_) -> error(x) end, [1])),
    Deadline = beam_time:deadline_after(1000),
    false = beam_time:expired(Deadline),
    {Pid, Mon} = beam:spawn_monitor(fun() -> receive {From, N} -> From ! {self(), N + 1} end end),
    beam:send(Pid, {self(), 2}),
    3 = receive {Pid, Value} -> Value end,
    true = beam:demonitor(Mon),
    {HugeText, HugeReds, MapReds, FloatText, Nested, Opened} = format_bounds(),
    {HugeText, HugeReds, MapReds, FloatText, Nested, Opened, <<"LIBRARY_OK">>}.

public_library_exports() ->
    ok = public_library_exports(beam_ets,[{all,0},{delete,1},{delete,2},{delete_all_objects,1},{delete_object,2},{first,1},{first_lookup,1},{foldl,3},{foldr,3},{fun2ms,1},{give_away,3},{info,1},{info,2},{init_table,2},{insert,2},{insert_new,2},{internal_delete_all,2},{internal_request_all,0},{internal_select_delete,2},{is_compiled_ms,1},{last,1},{last_lookup,1},{lookup,2},{lookup_element,3},{lookup_element,4},{match,1},{match,2},{match,3},{match_delete,2},{match_object,1},{match_object,2},{match_object,3},{match_spec_compile,1},{match_spec_run,2},{match_spec_run_r,3},{member,2},{new,2},{next,2},{next_lookup,2},{prev,2},{prev_lookup,2},{rename,2},{repair_continuation,2},{safe_fixtable,2},{select,1},{select,2},{select,3},{select_count,2},{select_delete,2},{select_replace,2},{select_reverse,1},{select_reverse,2},{select_reverse,3},{setopts,2},{slot,2},{tab2list,1},{take,2},{test_ms,2},{update_counter,3},{update_counter,4},{update_element,3},{update_element,4},{whereis,1}]),
    ok = public_library_exports(beam_binary,[{at,2},{bin_to_list,1},{bin_to_list,2},{bin_to_list,3},{compile_pattern,1},{copy,1},{copy,2},{decode_hex,1},{decode_unsigned,1},{decode_unsigned,2},{encode_hex,1},{encode_hex,2},{encode_unsigned,1},{encode_unsigned,2},{first,1},{join,2},{last,1},{list_to_bin,1},{longest_common_prefix,1},{longest_common_suffix,1},{match,2},{match,3},{matches,2},{matches,3},{part,2},{part,3},{referenced_byte_size,1},{replace,3},{replace,4},{split,2},{split,3}]),
    ok = public_library_exports(beam_lists,[{all,2},{any,2},{append,1},{append,2},{concat,1},{delete,2},{droplast,1},{dropwhile,2},{duplicate,2},{enumerate,1},{enumerate,2},{enumerate,3},{filter,2},{filtermap,2},{flatlength,1},{flatmap,2},{flatten,1},{flatten,2},{foldl,3},{foldr,3},{foreach,2},{join,2},{keydelete,3},{keyfind,3},{keymap,3},{keymember,3},{keymerge,3},{keyreplace,4},{keysearch,3},{keysort,2},{keystore,4},{keytake,3},{last,1},{map,2},{mapfoldl,3},{mapfoldr,3},{max,1},{member,2},{merge,1},{merge,2},{merge,3},{merge3,3},{min,1},{nth,2},{nthtail,2},{partition,2},{prefix,2},{reverse,1},{reverse,2},{rkeymerge,3},{rmerge,2},{rmerge,3},{rmerge3,3},{rukeymerge,3},{rumerge,2},{rumerge,3},{rumerge3,3},{search,2},{seq,2},{seq,3},{sort,1},{sort,2},{split,2},{splitwith,2},{sublist,2},{sublist,3},{subtract,2},{suffix,2},{sum,1},{takewhile,2},{ukeymerge,3},{ukeysort,2},{umerge,1},{umerge,2},{umerge,3},{umerge3,3},{uniq,1},{uniq,2},{unzip,1},{unzip3,1},{usort,1},{usort,2},{zip,2},{zip,3},{zip3,3},{zip3,4},{zipwith,3},{zipwith,4},{zipwith3,4},{zipwith3,5}]),
    ok = public_library_exports(beam_maps,[{filter,2},{filtermap,2},{find,2},{fold,3},{foreach,2},{from_keys,2},{from_list,1},{get,2},{get,3},{groups_from_list,2},{groups_from_list,3},{intersect,2},{intersect_with,3},{is_iterator_valid,1},{is_key,2},{iterator,1},{iterator,2},{keys,1},{map,2},{merge,2},{merge_with,3},{new,0},{next,1},{put,3},{remove,2},{size,1},{take,2},{to_list,1},{update,3},{update_with,3},{update_with,4},{values,1},{with,2},{without,2}]),
    ok = public_library_exports(beam_records,[{create,4},{get,2},{get_definition,2},{get_field_names,1},{get_module,1},{get_name,1},{is_exported,1},{update,4}]),
    ok = public_library_exports(beam_string,[{casefold,1},{chomp,1},{equal,2},{equal,3},{equal,4},{find,2},{find,3},{is_empty,1},{jaro_similarity,2},{length,1},{lexemes,2},{list_to_float,1},{list_to_integer,1},{lowercase,1},{next_codepoint,1},{next_grapheme,1},{nth_lexeme,3},{pad,2},{pad,3},{pad,4},{prefix,2},{replace,3},{replace,4},{reverse,1},{slice,2},{slice,3},{split,2},{split,3},{take,2},{take,3},{take,4},{titlecase,1},{to_float,1},{to_graphemes,1},{to_integer,1},{trim,1},{trim,2},{trim,3},{uppercase,1}]),
    ok = public_library_exports(beam_unicode,[{bin_is_7bit,1},{bom_to_encoding,1},{category,1},{characters_to_binary,1},{characters_to_binary,2},{characters_to_binary,3},{characters_to_binary_int,2},{characters_to_list,1},{characters_to_list,2},{characters_to_list_int,2},{characters_to_nfc_binary,1},{characters_to_nfc_list,1},{characters_to_nfd_binary,1},{characters_to_nfd_list,1},{characters_to_nfkc_binary,1},{characters_to_nfkc_list,1},{characters_to_nfkd_binary,1},{characters_to_nfkd_list,1},{encoding_to_bom,1},{is_id_continue,1},{is_id_start,1},{is_whitespace,1}]),
    ok.
public_library_exports(Module,Expected) ->
    Exports=Module:module_info(exports),
    Actual=beam_lists:sort([FA || FA <- Exports,FA=/={module_info,0},FA=/={module_info,1}]),
    Actual=beam_lists:sort(Expected),
    ok.

ets_library_cases() ->
    true = beam:module_loaded(beam_ets),
    false = beam:module_loaded(erl_eval),
    false = beam:module_loaded(ms_transform),
    false = beam:module_loaded(qlc),
    false = beam:module_loaded(dets),
    false = beam:is_builtin(ets, new, 2),
    true = beam:is_builtin(beam_ets, new, 2),
    Set = beam_ets:new(returning_set, [set,public]),
    Ordered = beam_ets:new(returning_ordered, [ordered_set,private]),
    Bag = beam_ets:new(returning_bag, [bag]),
    Dup = beam_ets:new(returning_duplicate_bag, [duplicate_bag]),
    true = beam_ets:insert(Set, [{1,integer},{1.0,float}]),
    2 = beam_ets:info(Set,size),
    true = beam_ets:insert(Ordered, [{1,integer},{1.0,float}]),
    1 = beam_ets:info(Ordered,size),
    true = beam_ets:insert(Bag, [{key,a},{key,a},{key,b}]),
    2 = length(beam_ets:lookup(Bag,key)),
    true = beam_ets:insert(Dup, [{key,a},{key,a}]),
    [{key,a},{key,a}] = beam_ets:lookup(Dup,key),
    false = beam_ets:insert_new(Set,[{fresh,1},{1,conflict}]),
    [] = beam_ets:lookup(Set,fresh),
    3 = beam_ets:update_counter(Set,counter,{2,3},{counter,0}),
    true = beam_ets:update_element(Set,counter,{2,9}),
    [{counter,9}] = beam_ets:take(Set,counter),
    [] = beam_ets:take(Set,counter),
    true = beam_ets:safe_fixtable(Set,true),
    true = beam_ets:safe_fixtable(Set,true),
    true = beam_ets:safe_fixtable(Set,false),
    true = beam_ets:info(Set,fixed),
    true = beam_ets:safe_fixtable(Set,false),
    false = beam_ets:info(Set,fixed),
    Parent = self(),
    {Reader,RM} = erlang:spawn_monitor(fun() ->
        ok = collection_badarg(beam_ets,lookup,[Ordered,1]),
        [{1,integer}] = beam_ets:lookup(Set,1),
        Parent ! ets_access_ok
    end),
    receive ets_access_ok -> ok;
            {'DOWN',RM,process,Reader,Reason} -> error({ets_access_failed,Reason})
    after 5000 -> error(ets_access_timeout) end,
    receive {'DOWN',RM,process,Reader,normal} -> ok after 5000 -> error(ets_reader_exit) end,
    {Owner,OM} = erlang:spawn_monitor(fun() ->
        T = beam_ets:new(returning_heir,[{heir,Parent,heir_data}]),
        true = beam_ets:insert(T,{held,42}),
        Parent ! {ets_heir_ready,self(),T}
    end),
    Heir = receive {ets_heir_ready,Owner,T} -> T after 5000 -> error(ets_heir_timeout) end,
    receive {'ETS-TRANSFER',Heir,Owner,heir_data} -> ok after 5000 -> error(ets_transfer_timeout) end,
    receive {'DOWN',OM,process,Owner,normal} -> ok after 5000 -> error(ets_owner_exit) end,
    [{held,42}] = beam_ets:lookup(Heir,held),
    true = beam_ets:delete(Heir),
    true = beam_ets:delete(Set),
    ok = collection_badarg(beam_ets,lookup,[Set,1]),
    true = beam_ets:delete(Ordered),
    true = beam_ets:delete(Bag),
    true = beam_ets:delete(Dup),
    Named = beam_ets:new(returning_named,[named_table]),
    returning_named = Named,
    true = beam_ets:delete(Named),
    undefined = beam_ets:whereis(Named),
    ok = ets_ordinary_cases(),
    ok = ets_fun_rejection(),
    ok.

ets_ordinary_cases() ->
    Table = beam_ets:new(returning_wrappers,[ordered_set]),
    try
        true = beam_ets:insert(Table,[{a,1},{b,2},{c,3}]),
        self() ! ets_wrapper_message,
        true = beam_lists:member(Table,beam_ets:all()),
        receive ets_wrapper_message -> ok after 0 -> error(ets_mailbox_changed) end,
        6 = beam_ets:foldl(fun({_,N},A) -> N+A end,0,Table),
        6 = beam_ets:foldr(fun({_,N},A) -> N+A end,0,Table),
        [{a,1},{b,2},{c,3}] = beam_ets:tab2list(Table),
        callback_failure = try beam_ets:foldl(fun(_,_) -> throw(callback_failure) end,0,Table)
                           catch throw:callback_failure -> callback_failure end,
        MS = [{'_',[],['$_']}],
        Program = beam_ets:match_spec_compile(MS),
        [{a,1}] = beam_ets:match_spec_run([{a,1}],Program),
        {ok,{a,1}} = beam_ets:test_ms({a,1},MS),
        {error,[_|_]} = beam_ets:test_ms({a,1},invalid_match_spec),
        {[_],Continuation} = beam_ets:select(Table,MS,1),
        Continuation = beam_ets:repair_continuation(Continuation,MS),
        {[_],_} = beam_ets:select(Continuation),
        true = beam_ets:match_delete(Table,{b,'_'}),
        1 = beam_ets:select_delete(Table,[{{'_','$1'},[{'>','$1',1}],[true]}]),
        [{a,1}] = beam_ets:tab2list(Table),
        true = beam_ets:delete_all_objects(Table),
        [] = beam_ets:tab2list(Table),
        Next = fun(read) -> end_of_input; (close) -> ok end,
        Initial = fun(read) -> {[{loaded,7}],Next}; (close) -> ok end,
        true = beam_ets:init_table(Table,Initial),
        [{loaded,7}] = beam_ets:tab2list(Table)
    after
        true = beam_ets:delete(Table)
    end,
    ok.

ets_fun_rejection() ->
    Expected = {badarg,{beam_ets,fun2ms,
                       [function,called,with,real,'fun',should,be,transformed,with,
                        parse_transform,'or',called,with,a,'fun',generated,in,the,shell]}},
    beam_lists:foreach(fun(Fun) ->
        try beam_ets:fun2ms(Fun) of
            Value -> error({unexpected_fun2ms_value,Value})
        catch exit:Expected -> ok end
    end,[fun() -> ordinary end,fun beam_lists:reverse/1]),
    try beam_ets:fun2ms(not_a_fun) of
        Value -> error({unexpected_fun2ms_value,Value})
    catch error:function_clause:Stack ->
        [{beam_ets,fun2ms,[not_a_fun],_}|_] = Stack,
        ok
    end,
    false = beam:module_loaded(erl_eval),
    false = beam:module_loaded(ms_transform),
    ok.

byte_library_cases() ->
    false = beam:module_loaded(binary),
    false = member(binary, beam:pre_loaded()),
    true = beam:function_exported(beam_binary, match, 2),
    true = beam:function_exported(beam_binary, match, 3),
    false = beam:is_builtin(erts_internal, bytes_match, 3),
    true = beam:is_builtin(beam, binary_part, 3),
    true = beam:is_builtin(beam_binary, match, 2),
    0 = beam:byte_size(<<>>),
    4 = beam:byte_size(<<"abcd">>),
    <<"bc">> = beam_binary:part(<<"abcd">>, 1, 2),
    <<>> = beam_binary:part(<<>>, 0, 0),
    <<>> = beam_binary:part(<<"abcd">>, 4, 0),
    $a = beam_binary:first(<<"abcd">>),
    $d = beam_binary:last(<<"abcd">>),
    $b = beam_binary:at(<<"abcd">>, 1),
    <<>> = beam_binary:list_to_bin([]),
    <<"abcd">> = beam_binary:list_to_bin([<<"a">>, [98, [<<"c">>]] | <<"d">>]),
    ok = byte_badarg(list_to_bin,[<<"ab">>]),
    <<"ab">> = beam_binary:list_to_bin([<<"ab">>]),
    <<"ab">> = beam_binary:copy(<<"ab">>),
    <<>> = beam_binary:copy(<<>>),
    <<"ababab">> = beam_binary:copy(<<"ab">>, 3),
    <<"ab">> = beam_binary:copy(<<"ab">>, 1),
    <<>> = beam_binary:copy(<<"ab">>, 0),
    HugeCount = 1 bsl 200,
    ok = byte_badarg(copy, [<<>>, HugeCount]),
    ok = byte_badarg(copy, [<<"x">>, HugeCount]),
    "bc" = beam_binary:bin_to_list(<<"abcd">>,1,2),
    <<"61FF">> = beam_binary:encode_hex(<<97,255>>),
    <<97,255>> = beam_binary:decode_hex(<<"61FF">>),
    <<"a,b">> = beam_binary:join([<<"a">>,<<"b">>],<<",">>),
    <<"xbx">> = beam_binary:replace(<<"aba">>,<<"a">>,<<"x">>,[global]),
    ok = byte_search_cases(),
    ok = byte_conversion_cases(),
    ok = byte_dispatch_cases(),
    ok = byte_continuation_cases(),
    %% Large logical binaries exercise backing offsets and retention without
    %% making a particular small-binary copying threshold part of the API.
    Large = beam_binary:copy(<<"abcd">>, 16384),
    Unaligned = byte_unaligned(Large),
    $a = beam_binary:first(Unaligned),
    $d = beam_binary:last(Unaligned),
    $b = beam_binary:at(Unaligned, 257),
    <<"bc">> = beam_binary:part(Unaligned, 257, 2),
    {257, 2} = beam_binary:match(Unaligned, <<"bc">>, [{scope, {257, 2}}]),
    Slice = beam_binary:part(Unaligned, 256, 1024),
    Copy = beam_binary:copy(Slice),
    Slice = Copy,
    RepeatedOnce = beam_binary:copy(Slice, 1),
    true = beam_binary:referenced_byte_size(Slice) >= beam:byte_size(Slice),
    true = beam_binary:referenced_byte_size(Copy) < beam_binary:referenced_byte_size(Large),
    true = beam_binary:referenced_byte_size(RepeatedOnce) < beam_binary:referenced_byte_size(Large),
    1024 = beam_binary:longest_common_prefix([Slice, Copy]),
    1024 = beam_binary:longest_common_suffix([Slice, Copy]),
    0 = beam_binary:referenced_byte_size(<<>>),
    ok = byte_badarg_cases(HugeCount),
    false = beam:module_loaded(lists),
    false = beam:module_loaded(maps),
    false = beam:module_loaded(binary),
    ok.

byte_dispatch_cases() ->
    Module = beam_binary,
    Function = match,
    {1, 2} = Module:Function(<<"abcd">>, <<"bc">>),
    {1, 2} = apply(Module, Function, [<<"abcd">>, <<"bc">>, []]),
    ok.

byte_continuation_cases() ->
    Size = 1024 * 1024,
    Bytes = beam_binary:copy(<<0>>, Size),
    Pattern = beam_binary:compile_pattern([<<0, 0>>, <<0, 0, 0, 0>>]),
    ok = byte_ranges(beam_binary:matches(Bytes, Pattern), 0, Size),
    Size = beam_binary:longest_common_prefix([Bytes, beam_binary:copy(Bytes)]),
    Size = beam_binary:longest_common_suffix([Bytes, beam_binary:copy(Bytes)]),
    ok.

byte_ranges([], Position, Position) -> ok;
byte_ranges([{Position, 4} | Rest], Position, Size) ->
    byte_ranges(Rest, Position + 4, Size).

byte_search_cases() ->
    Single = beam_binary:compile_pattern(<<"bc">>),
    {1, 2} = beam_binary:match(<<"abcde">>, Single),
    [{1, 2}, {4, 2}] = beam_binary:matches(<<"abcabc">>, Single),
    [<<"a">>, <<"de">>] = beam_binary:split(<<"abcde">>, Single),
    Pattern = beam_binary:compile_pattern([<<"bc">>, <<"bcde">>, <<"de">>]),
    {1, 4} = beam_binary:match(<<"abcde">>, Pattern),
    {1, 4} = beam_binary:match(<<"abcde">>, [<<"de">>, <<"bcde">>, <<"bc">>]),
    {3, 2} = beam_binary:match(<<"abcde">>, Pattern, [{scope, {2, 3}}]),
    {1, 2} = beam_binary:match(<<"abcde">>, Pattern, [{scope, {1, 2}}]),
    nomatch = beam_binary:match(<<"abcde">>, Pattern, [{scope, {1, 1}}]),
    nomatch = beam_binary:match(<<"abcde">>, Pattern, [{scope, {5, 0}}]),
    nomatch = beam_binary:match(<<>>, <<"x">>),
    [] = beam_binary:matches(<<>>, <<"x">>, []),
    [{1, 4}] = beam_binary:matches(<<"abcde">>, Pattern),
    [{1, 2}, {3, 2}] = beam_binary:matches(<<"abcde">>, [<<"bc">>, <<"de">>]),
    [{0, 2}, {2, 2}] = beam_binary:matches(<<"aaaa">>, <<"aa">>, []),
    [{3, 2}] = beam_binary:matches(<<"abcabc">>, <<"ab">>, [{scope, {1, 5}}]),
    [<<"b">>, <<"nana">>] = beam_binary:split(<<"banana">>, <<"a">>),
    [<<"ban">>, <<"na">>] = beam_binary:split(<<"banana">>, <<"a">>, [{scope, {2, 3}}]),
    [<<"b">>, <<"n">>, <<"n">>, <<>>] =
        beam_binary:split(<<"banana">>, <<"a">>, [global]),
    [<<>>, <<"a">>] = beam_binary:split(<<",a,,">>, <<",">>, [global, trim]),
    [<<"a">>] = beam_binary:split(<<",a,,">>, <<",">>, [global, trim_all]),
    [<<>>, <<"a,,">>] = beam_binary:split(<<",a,,">>, <<",">>, [trim]),
    [<<"a,,">>] = beam_binary:split(<<",a,,">>, <<",">>, [trim_all]),
    [<<>>, <<"a,,">>] = beam_binary:split(<<",a,,">>, <<",">>, [{scope, {0, 4}}]),
    [<<"abcde">>] = beam_binary:split(<<"abcde">>, Pattern, [{scope, {5, 0}}]),
    [<<"a">>, <<>>] = beam_binary:split(<<"abcde">>, Pattern),
    [<<>>] = beam_binary:split(<<>>, <<"x">>),
    [<<>>] = beam_binary:split(<<>>, <<"x">>, [global]),
    [] = beam_binary:split(<<>>, <<"x">>, [trim]),
    [] = beam_binary:split(<<>>, <<"x">>, [trim_all]),
    ok = byte_badarg(compile_pattern, [Pattern]),
    ok.

byte_conversion_cases() ->
    3 = beam_binary:longest_common_prefix([<<"abcd">>, <<"abce">>]),
    3 = beam_binary:longest_common_suffix([<<"abcd">>, <<"xbcd">>]),
    4 = beam_binary:longest_common_prefix([<<"abcd">>]),
    0 = beam_binary:longest_common_suffix([<<"abcd">>, <<>>]),
    <<0>> = beam_binary:encode_unsigned(0),
    <<1, 2>> = beam_binary:encode_unsigned(258),
    <<1, 2>> = beam_binary:encode_unsigned(258, big),
    <<2, 1>> = beam_binary:encode_unsigned(258, little),
    0 = beam_binary:decode_unsigned(<<>>),
    0 = beam_binary:decode_unsigned(<<>>, little),
    258 = beam_binary:decode_unsigned(<<0, 0, 1, 2>>),
    258 = beam_binary:decode_unsigned(<<0, 0, 1, 2>>, big),
    258 = beam_binary:decode_unsigned(<<2, 1, 0, 0>>, little),
    Integer = (1 bsl 1033) bor 16#010203,
    Big = beam_binary:encode_unsigned(Integer, big),
    Little = beam_binary:encode_unsigned(Integer, little),
    Integer = beam_binary:decode_unsigned(byte_unaligned(Big)),
    Integer = beam_binary:decode_unsigned(byte_unaligned(Little), little),
    Integer = beam_binary:decode_unsigned(<<0, Big/binary>>, big),
    Integer = beam_binary:decode_unsigned(<<Little/binary, 0>>, little),
    ok.

byte_badarg_cases(Huge) ->
    Partial = <<1:9>>,
    ok = byte_badarg(part, [<<"abcd">>, 5, 0]),
    <<"a">> = beam_binary:part(<<"abcd">>, 1, -1),
    ok = byte_badarg(part, [Partial, 0, 0]),
    ok = byte_badarg(part, [<<>>, Huge, Huge]),
    ok = byte_badarg(first, [<<>>]),
    ok = byte_badarg(first, [Partial]),
    ok = byte_badarg(last, [<<>>]),
    ok = byte_badarg(last, [Partial]),
    ok = byte_badarg(at, [<<"ab">>, -1]),
    ok = byte_badarg(at, [<<"ab">>, 2]),
    ok = byte_badarg(at, [<<"ab">>, 1.0]),
    ok = byte_badarg(at, [<<"ab">>, Huge]),
    ok = byte_badarg(list_to_bin, [256]),
    ok = byte_badarg(list_to_bin, [[256]]),
    ok = byte_badarg(list_to_bin, [[1 | invalid]]),
    ok = byte_badarg(list_to_bin, [[Partial]]),
    ok = byte_badarg(copy, [Partial]),
    ok = byte_badarg(copy, [Partial, 0]),
    ok = byte_badarg(copy, [<<>>, -1]),
    ok = byte_badarg(copy, [<<>>, -Huge]),
    ok = byte_badarg(copy, [<<>>, 1.0]),
    1 = beam_binary:referenced_byte_size(Partial),
    ok = byte_badarg(compile_pattern, [<<>>]),
    ok = byte_badarg(compile_pattern, [[]]),
    ok = byte_badarg(compile_pattern, [[<<"x">>, <<>>]]),
    ok = byte_badarg(compile_pattern, [[Partial]]),
    ok = byte_badarg(match, [Partial, <<"x">>]),
    ok = byte_badarg(match, [<<>>, <<>>]),
    ok = byte_badarg(match, [<<>>, {bm, make_ref()}]),
    ok = byte_badarg(match, [<<"a">>, <<"a">>, [global]]),
    ok = byte_badarg(match, [<<>>, <<>>, [{scope, {0, 0}}]]),
    ok = byte_badarg(match, [<<"a">>, <<"a">>, [global]]),
    ok = byte_badarg(match, [<<"a">>, <<"a">>, [{scope, {0, -1}}]]),
    ok = byte_badarg(match, [<<"a">>, <<"a">>, [{scope, {Huge, 0}}]]),
    ok = byte_badarg(matches, [<<>>, []]),
    ok = byte_badarg(matches, [<<"a">>, <<"a">>, [{scope, {1, 1}}]]),
    ok = byte_badarg(matches, [<<"a">>, <<"a">>, [{scope, {0, 1}}, {extra, true}]]),
    ok = byte_badarg(split, [<<>>, <<>>]),
    ok = byte_badarg(split, [Partial, <<"x">>]),
    ok = byte_badarg(split, [<<"a">>, <<"a">>, [{global, 1}]]),
    ok = byte_badarg(split, [<<"a">>, <<"a">>, [{trim, true}]]),
    ok = byte_badarg(split, [<<"a">>, <<"a">>, [{scope, {0, 1.0}}]]),
    ok = byte_badarg(split, [<<"a">>, <<"a">>, [{extra, none}]]),
    ok = byte_badarg(longest_common_prefix, [[]]),
    ok = byte_badarg(longest_common_prefix, [[<<>>, invalid]]),
    ok = byte_badarg(longest_common_prefix, [[<<"a">> | <<"a">>]]),
    ok = byte_badarg(longest_common_suffix, [[<<>>, invalid]]),
    ok = byte_badarg(longest_common_suffix, [[Partial]]),
    ok = byte_badarg(encode_unsigned, [-1]),
    ok = byte_badarg(encode_unsigned, [1.0]),
    ok = byte_badarg(encode_unsigned, [0, native]),
    ok = byte_badarg(decode_unsigned, [Partial]),
    ok = byte_badarg(decode_unsigned, [<<>>, native]),
    ok.

byte_badarg(Function, Arguments) ->
    try apply(beam_binary, Function, Arguments) of
        Value -> beam:error({expected_badarg, Function, Arguments, Value})
    catch
        error:badarg:Stack ->
            true = byte_error_arguments(Stack, Function, Arguments),
            ok
    end.

byte_error_arguments([{beam_binary, Function, Arguments, _} | _], Function, Arguments) -> true;
byte_error_arguments([_ | Rest], Function, Arguments) ->
    byte_error_arguments(Rest, Function, Arguments);
byte_error_arguments([], _, _) -> false.

byte_unaligned(Bytes) ->
    Size = byte_size(Bytes),
    Wrapped = <<0:3, Bytes/binary, 0:5>>,
    <<_:3, Unaligned:Size/binary, _:5>> = Wrapped,
    Unaligned.

preloaded(_) ->
    Names = beam:pre_loaded(),
    5 = count(Names),
    true = member(erlang, Names),
    false = member(persistent_term, Names),
    false = member(atomics, Names),
    false = member(counters, Names),
    false = member(lists, Names),
    false = member(maps, Names),
    Names.

ret_context(Value) ->
    42 = erts_internal:vm_context(),
    ret(Value).

ret(Value) ->
    Value.

boom(_) ->
    beam:error(boom).

finish(_) ->
    beam_boot:finish(4).

halt(_) ->
    beam:halt(9).

kill_output(_) ->
    case whereis(beam_output_server) of
        Pid when is_pid(Pid) -> exit(Pid, kill);
        _ -> ok
    end,
    receive after 1000 -> ok end,
    beam_output:write(standard_output, <<"late">>, infinity).

cleanup(_) ->
    beam_host:request({output, cleanup_test, <<>>}),
    ok.

greeting(Name) ->
    ok = beam_output:write(standard_output, <<"Hello, ", Name/binary, "!\n">>, infinity),
    Name.

counter(_) ->
    {First, FirstMonitor, Eight} = run_worker(5, 3),
    exit(First, kill),
    await_down(First, FirstMonitor, killed),
    {_Second, _SecondMonitor, Seven} = run_worker(5, 2),
    %% Leave the replacement live to exercise entry-scoped teardown.
    {Eight, Seven, 1}.

counter_fault(Mode) ->
    {Pid, Mon} = beam:spawn_monitor(fun() -> faulty_worker(Mode) end),
    Ref = make_ref(),
    Pid ! {self(), Ref, 3},
    await_reply(Pid, Mon, Ref).

code_client(_) ->
    {ok, Bytes} = beam_code:fetch(memory, uppercase_transform, infinity),
    ok = beam_code:load(uppercase_transform, Bytes, memory),
    uppercase_transform:apply(<<"beam">>).

code_missing(_) ->
    {error, #{kind := not_found}} = beam_code:fetch(memory, missing_transform, infinity),
    <<"still">> = kept:value().

code_bad(_) ->
    {ok, Bytes} = beam_code:fetch(memory, not_a_module, infinity),
    {error, #{kind := invalid_data}} = beam_code:load(not_a_module, Bytes, memory),
    <<"still">> = kept:value().

file_report(Input, Output) ->
    false = beam:module_loaded(beam_socket),
    false = beam:module_loaded(prim_socket),
    false = beam:module_loaded(zlib),
    {ok, Bytes} = beam_file:read_file(Input, infinity),
    Lines = count_lines(Bytes),
    Size = byte_size(Bytes),
    Report = <<"lines=", (integer_to_binary(Lines))/binary,
               " bytes=", (integer_to_binary(Size))/binary, "\n">>,
    {ok, Handle} = beam_file:open(Output, infinity, #{mode => write}),
    ok = beam_file:write(Handle, Report, infinity),
    ok = beam_file:close(Handle, infinity),
    {Lines, Size}.

file_edges(Missing, Full) ->
    {error, #{kind := MissingKind}} = beam_file:read_file(Missing, infinity),
    {error, #{kind := InvalidKind}} = beam_file:read_file(<<"relative">>, infinity),
    {error, #{kind := WriteKind}} = beam_file:write_file(Full, <<"x">>, infinity),
    {ok, Handle} = beam_file:open(<<"/etc/hosts">>, infinity, #{mode => read}),
    Parent = self(),
    erlang:spawn(fun() -> Parent ! {edge, beam_file:read(Handle, 4, infinity)} end),
    {error, #{kind := OwnerKind}} = receive {edge, Result} -> Result end,
    ok = beam_file:close(Handle, infinity),
    {atom_to_binary(MissingKind), atom_to_binary(InvalidKind),
     atom_to_binary(WriteKind), atom_to_binary(OwnerKind)}.

file_omitted(_) ->
    false = beam:module_loaded(beam_file),
    false = beam:module_loaded(beam_socket),
    {'EXIT', {undef, _}} = (catch beam_file:read_file(<<"/etc/hosts">>, infinity)),
    {'EXIT', {undef, _}} = (catch prim_file:read_file("/etc/hosts")),
    {'EXIT', {undef, _}} = (catch beam_socket:sockname(undefined)),
    <<"undef">>.

file_owner_death(_) ->
    exit(whereis(beam_file_owner), kill),
    receive after 200 -> ok end,
    beam_file:read_file(<<"/etc/hosts">>, infinity).

echo(_) ->
    false = beam:module_loaded(beam_file),
    false = beam:module_loaded(prim_file),
    false = beam:module_loaded(zlib),
    echo_session(normal).

echo_partial(_) ->
    {1, 4, [<<"part">>]} = echo_session(partial),
    <<"part">>.

echo_peer(_) ->
    try echo_session(<<"header">>) of
        _ -> error(truncated_header_was_accepted)
    catch
        error:truncated_header -> <<"PEER_EOF">>
    end.

echo_abandon(_) ->
    {ok, Listen} = beam_socket:listen(#{addr => {127, 0, 0, 1}, port => 0},
                                    beam_time:deadline_after(5000)),
    {ok, #{port := Port}} = beam_socket:sockname(Listen),
    %% A persistent root retains a real open listener until VM teardown.
    beam_persistent_term:put(returning_abandoned_listener, Listen),
    Port.

echo_fault(Mode) ->
    echo_session(Mode).

echo_owner(_) ->
    exit(whereis(socket_registry), kill),
    receive after 200 -> ok end,
    beam_socket:listen(#{addr => {127, 0, 0, 1}, port => 0}, infinity).

echo_session(Mode) ->
    Deadline = beam_time:deadline_after(5000),
    {ok, Listen} = beam_socket:listen(#{addr => {127, 0, 0, 1}, port => 0}, Deadline),
    {ok, #{port := Port}} = beam_socket:sockname(Listen),
    Parent = self(),
    {Client, Mon} = beam:spawn_monitor(fun() -> echo_client(Port, Parent, Mode, Deadline) end),
    try
        receive
            {connected, Client} -> ok;
            {'DOWN', Mon, process, Client, Reason} -> error({client_failed, Reason})
        after beam_time:remaining(Deadline) -> error(client_connect_timeout)
        end,
        {ok, Accepted} = beam_socket:accept(Listen, Deadline),
        Result = try collect(Accepted, 0, 0, [], Deadline)
                 after close_socket(Accepted)
                 end,
        receive
            {client_done, Client} -> ok;
            {'DOWN', Mon, process, Client, Reason2} -> error({client_failed, Reason2})
        after beam_time:remaining(Deadline) -> error(client_completion_timeout)
        end,
        await_down(Client, Mon, normal),
        Result
    after
        close_socket(Listen),
        retire_worker(Client, Mon)
    end.

close_socket(Socket) ->
    %% An expired operation deadline must not suppress explicit cleanup.
    ok = beam_socket:close(Socket, beam_time:deadline_after(5000)).

record_service(Path) ->
    {ok, Alpha} = store(Path, 1, <<"alpha">>),
    Rebuilt = reconstruct(Path, 1),
    {ok, Beta} = store(Path, 2, <<"beta">>),
    Interrupted = interrupt(Path),
    {Alpha, Rebuilt, Beta, 2, 1, Interrupted}.

record_unreadable(Path) ->
    try record_service(Path) of
        _ -> error(unreadable_record_succeeded)
    catch
        error:{worker_failed, {{record_read_failed,
                               #{kind := permission_denied, service := file}}, _}} ->
            error(record_permission_denied)
    end.

record_fault(Mode) ->
    {Pid, Mon} = beam:spawn_monitor(fun() -> faulty_worker(Mode) end),
    Ref = make_ref(),
    Pid ! {self(), Ref, {store, 1, <<"unused">>}},
    await_reply(Pid, Mon, Ref).

zlib_explicit(_) ->
    Stream = zlib:open(),
    ok = zlib:deflateInit(Stream),
    _ = zlib:deflate(Stream, <<"abc">>, finish),
    ok = zlib:deflateEnd(Stream),
    ok = zlib:close(Stream),
    closed.

zlib_abandon(_) ->
    Stream = zlib:open(),
    ok = zlib:deflateInit(Stream),
    beam:spawn(fun() ->
                       Abandoned = zlib:open(),
                       ok = zlib:deflateInit(Abandoned)
               end),
    receive after 50 -> ok end,
    ok = zlib:deflateEnd(Stream),
    ok = zlib:close(Stream),
    abandoned.

zlib_persist(_) ->
    Stream = zlib:open(),
    ok = zlib:inflateInit(Stream),
    beam_persistent_term:put(beam_zlib_hold, Stream),
    held.

zlib_owner(_) ->
    erts_internal:request_failure(zlib, injected_failure),
    receive after 200 -> ok end,
    zlib:open().

format_bounds() ->
    Huge = 1 bsl 2000,
    Full = beam:integer_to_binary(Huge),
    Shown = beam_format:term(Huge, #{depth => 4, bytes => 8}),
    <<Prefix:5/binary, "...">> = Shown,
    <<Prefix:5/binary, _/binary>> = Full,
    <<Sliced:8/binary, _/binary>> = Full,
    true = Shown =/= Sliced,
    %% 2^1000000 has 301030 decimal digits. Rendering that text, or a power of
    %% ten of the same size, is millions of reductions. Stopping at the first
    %% five digits stays far below that.
    Million = 1 bsl 1000000,
    {reductions, BeforeHuge} = beam:process_info(self(), reductions),
    <<"99006...">> = beam_format:term(Million, #{depth => 4, bytes => 8}),
    {reductions, AfterHuge} = beam:process_info(self(), reductions),
    HugeReds = AfterHuge - BeforeHuge,
    %% integer_to_binary(1 bsl 1000000) is about four million reductions.
    true = HugeReds < 500000,
    AtomText = beam_format:term('aaaaaaaaaaaaaaaaaaaa', #{depth => 2, bytes => 8}),
    <<"aaaaa...">> = AtomText,
    <<AtomSlice:8/binary, _/binary>> = erlang:atom_to_binary('aaaaaaaaaaaaaaaaaaaa'),
    true = AtomText =/= AtomSlice,
    %% Budget 4 cannot hold a short float. The result is the marker.
    %% Slicing float_to_binary([short]) would keep a digit.
    <<"...">> = beam_format:term(1.0e300, #{depth => 2, bytes => 4}),
    %% Fixed-point decimals badarg for this magnitude. The short form is used
    %% only when it fits; otherwise the marker is the whole result.
    Short = beam:float_to_binary(1.0e300, [short]),
    FloatText = beam_format:term(1.0e300, #{depth => 2, bytes => 8}),
    true = is_binary(FloatText) andalso byte_size(FloatText) =< 8,
    case byte_size(Short) =< 8 of
        true -> Short = FloatText;
        false -> <<"...">> = FloatText
    end,
    Nested = beam_format:term([1.0e300], #{depth => 3, bytes => 8}),
    true = is_binary(Nested) andalso byte_size(Nested) =< 8,
    <<"[", _/binary>> = Nested,
    <<"#{1 => 2}">> = beam_format:term(#{1 => 2}, #{depth => 3, bytes => 40}),
    <<"[1, 2]">> = beam_format:term([1, 2], #{depth => 3, bytes => 40}),
    <<"{1, 2}">> = beam_format:term({1, 2}, #{depth => 3, bytes => 40}),
    <<"#{}">> = beam_format:term(#{}, #{depth => 3, bytes => 40}),
    Opened = <<"#{1 => 2}|[1, 2]|{1, 2}|#{}">>,
    Map = wide_map(4000, 0, #{}),
    {reductions, Before} = beam:process_info(self(), reductions),
    Text = beam_format:term(Map, #{depth => 3, bytes => 8}),
    {reductions, After} = beam:process_info(self(), reductions),
    MapReds = After - Before,
    true = byte_size(Text) =< 8,
    %% Walking all 4000 pairs is about 950000 reductions.
    true = MapReds < 8000,
    {<<"99006...">>, HugeReds, MapReds, FloatText, Nested, Opened}.

wide_map(0, _Key, Map) ->
    Map;
wide_map(Count, Key, Map) ->
    wide_map(Count - 1, Key + 1, Map#{Key => Key}).

output_cases(_) ->
    ok = beam_output:write(standard_output, <<"ready">>, infinity),
    {error, #{kind := not_found}} = beam_output:write(missing_destination, <<"x">>, infinity),
    {error, #{kind := unavailable}} = beam_output:write(standard_output, <<"wait">>, infinity),
    {error, #{kind := busy}} = beam_output:write(standard_output, <<"full">>, infinity),
    {error, #{kind := io}} = beam_output:write(standard_output, <<"fail">>, infinity),
    cases.

omitted(_) ->
    try io:format("no output~n") of
        Result -> {ok, Result}
    catch
        Class:Reason -> {Class, Reason}
    end.

services(_) ->
    beam_boot:services().

run_worker(Start, Add) ->
    {Pid, Mon} = beam:spawn_monitor(fun() -> worker(Start) end),
    Ref = make_ref(),
    Pid ! {self(), Ref, Add},
    {Pid, Mon, await_reply(Pid, Mon, Ref)}.

await_reply(Pid, Mon, Ref) ->
    receive
        {Ref, Reply} -> Reply;
        {'DOWN', Mon, process, Pid, Reason} -> error({worker_failed, Reason})
    after 5000 -> error(worker_timeout)
    end.

await_down(Pid, Mon, Expected) ->
    receive
        {'DOWN', Mon, process, Pid, Expected} -> ok;
        {'DOWN', Mon, process, Pid, Reason} -> error({worker_stop_failed, Reason})
    after 5000 -> error(worker_stop_timeout)
    end.

retire_worker(Pid, Mon) ->
    CleanupMonitor = beam:monitor(process, Pid),
    exit(Pid, kill),
    receive
        {'DOWN', CleanupMonitor, process, Pid, _} -> ok
    after 5000 -> error(worker_cleanup_timeout)
    end,
    beam:demonitor(Mon, [flush]).

faulty_worker(<<"crash">>) ->
    receive {_From, _Ref, _Request} -> exit(deliberate_worker_failure) end;
faulty_worker(<<"stall">>) ->
    receive {_From, _Ref, _Request} -> receive never -> ok end end.

worker(Value) ->
    receive
        {From, Ref, Add} ->
            From ! {Ref, Value + Add},
            worker(Value + Add)
    end.

count_lines(Bytes) ->
    count_lines(Bytes, 0).

count_lines(<<>>, Count) ->
    Count;
count_lines(<<$\n, Rest/binary>>, Count) ->
    count_lines(Rest, Count + 1);
count_lines(<<_, Rest/binary>>, Count) ->
    count_lines(Rest, Count).

echo_client(_Port, _Parent, <<"crash">>, _Deadline) ->
    exit(deliberate_client_failure);
echo_client(Port, Parent, Mode, Deadline) ->
    {ok, Socket} = beam_socket:connect(#{addr => {127, 0, 0, 1}, port => Port}, Deadline),
    Parent ! {connected, self()},
    try send_workload(Socket, Mode, Deadline)
    after close_socket(Socket)
    end,
    Parent ! {client_done, self()}.

send_workload(Socket, normal, Deadline) ->
    send_frame(Socket, <<"one">>, Deadline),
    send_frame(Socket, <<"two">>, Deadline),
    send_frame(Socket, <<"three">>, Deadline);
send_workload(Socket, partial, Deadline) ->
    send_parts(Socket, [<<0>>, <<0, 0>>, <<4>>, <<"p">>, <<"ar">>, <<"t">>], Deadline);
send_workload(Socket, <<"header">>, Deadline) ->
    send_parts(Socket, [<<0>>, <<0>>], Deadline);
send_workload(Socket, <<"body">>, Deadline) ->
    send_parts(Socket, [<<4:32>>, <<"pa">>], Deadline);
send_workload(Socket, <<"empty-body">>, Deadline) ->
    send_parts(Socket, [<<4:32>>], Deadline);
send_workload(Socket, <<"oversized">>, Deadline) ->
    send_parts(Socket, [<<4097:32>>], Deadline);
send_workload(_Socket, <<"stall">>, _Deadline) ->
    receive never -> ok end.

send_parts(_Socket, [], _Deadline) -> ok;
send_parts(Socket, [Part | Rest], Deadline) ->
    ok = beam_socket:send(Socket, Part, Deadline),
    send_parts(Socket, Rest, Deadline).

send_frame(Socket, Bytes, Deadline) ->
    Size = byte_size(Bytes),
    ok = beam_socket:send(Socket, <<Size:32, Bytes/binary>>, Deadline).

collect(Socket, Frames, Bytes, Acc, Deadline) ->
    case read_frame(Socket, Deadline) of
        eof -> {Frames, Bytes, Acc};
        {ok, Frame} ->
            collect(Socket, Frames + 1, Bytes + byte_size(Frame), Acc ++ [Frame], Deadline)
    end.

read_frame(Socket, Deadline) ->
    case read_exact(Socket, 4, <<>>, Deadline) of
        eof -> eof;
        truncated -> error(truncated_header);
        {ok, <<Size:32>>} when Size =< 4096 ->
            case read_exact(Socket, Size, <<>>, Deadline) of
                {ok, _} = Result -> Result;
                _ -> error(truncated_body)
            end;
        {ok, _} -> error(oversized_frame)
    end.

read_exact(_Socket, 0, Acc, _Deadline) ->
    {ok, Acc};
read_exact(Socket, Need, Acc, Deadline) ->
    %% Single-byte reads preserve the distinction between clean EOF and a
    %% truncated header even when the underlying exact-length read closes.
    case beam_socket:recv(Socket, 1, Deadline) of
        eof when Acc =:= <<>> -> eof;
        eof -> truncated;
        {ok, Data} ->
            read_exact(Socket, Need - byte_size(Data), <<Acc/binary, Data/binary>>, Deadline);
        {error, #{kind := timeout}} -> error(socket_timeout);
        {error, Reason} -> error({socket_read_failed, Reason})
    end.

store(Path, Id, Data) ->
    {Pid, Mon} = beam:spawn_monitor(fun() -> record_worker(Path) end),
    Ref = make_ref(),
    Pid ! {self(), Ref, {store, Id, Data}},
    Result = await_reply(Pid, Mon, Ref),
    exit(Pid, shutdown),
    await_down(Pid, Mon, shutdown),
    Result.

interrupt(Path) ->
    {Pid, Mon} = beam:spawn_monitor(fun() -> hold_request(Path) end),
    Ref = make_ref(),
    Pid ! {self(), Ref, {store, 9, <<"never">>}},
    accepted = await_reply(Pid, Mon, Ref),
    exit(Pid, kill),
    await_down(Pid, Mon, killed),
    interrupted.

hold_request(Path) ->
    receive
        {From, Ref, Request} ->
            From ! {Ref, accepted},
            receive
                continue -> apply_store(Path, From, Ref, Request)
            end
    end.

record_worker(Path) ->
    receive
        {From, Ref, Request} ->
            apply_store(Path, From, Ref, Request),
            record_worker(Path)
    end.

apply_store(Path, From, Ref, {store, Id, Data}) ->
    Line = <<(integer_to_binary(Id))/binary, " ", Data/binary, "\n">>,
    Deadline = beam_time:deadline_after(5000),
    Existing = case beam_file:read_file(Path, Deadline) of
                   {ok, Bytes} -> Bytes;
                   {error, #{kind := not_found, service := file}} -> <<>>;
                   {error, Error} -> error({record_read_failed, Error})
               end,
    {ok, Handle} = beam_file:open(Path, Deadline, #{mode => write}),
    try
        ok = beam_file:write(Handle, <<Existing/binary, Line/binary>>, Deadline)
    after
        ok = beam_file:close(Handle, beam_time:deadline_after(5000))
    end,
    From ! {Ref, {ok, Data}}.

reconstruct(Path, Id) ->
    {ok, Bytes} = beam_file:read_file(Path, beam_time:deadline_after(5000)),
    scan(Bytes, integer_to_binary(Id)).

scan(<<Id:1/binary, " ", Rest/binary>>, Id) ->
    line_data(Rest, <<>>);
scan(<<_, Rest/binary>>, Id) ->
    scan(Rest, Id);
scan(<<>>, _Id) ->
    missing.

line_data(<<$\n, _/binary>>, Acc) ->
    Acc;
line_data(<<Byte, Rest/binary>>, Acc) ->
    line_data(Rest, <<Acc/binary, Byte>>);
line_data(<<>>, Acc) ->
    Acc.

preloaded_ok() ->
    Names = beam:pre_loaded(),
    count(Names) =:= 5 andalso member(erlang, Names) andalso
        not member(persistent_term, Names) andalso not member(atomics, Names) andalso
        not member(counters, Names) andalso not member(lists, Names).

count([]) -> 0;
count([_ | Rest]) -> 1 + count(Rest).

member(_Name, []) -> false;
member(Name, [Name | _]) -> true;
member(Name, [_ | Rest]) -> member(Name, Rest).

%% DOWN is emitted only after the entry has completed. Its recorded outcome
%% must survive a halt from retiring application children.
return_then_halt(_) -> arm_late_halt(16), <<"won">>.
exception_then_halt(_) -> arm_late_halt(16), error(won).
arm_late_halt(0) -> ok;
arm_late_halt(N) ->
    Parent = self(),
    spawn_opt(fun() ->
        Ref = monitor(process, Parent),
        Parent ! {armed, self()},
        receive {'DOWN', Ref, process, Parent, _} -> beam:halt(17) end
    end, [{priority, max}]),
    receive {armed, _} -> ok end,
    arm_late_halt(N - 1).

finish_then_halt(_) -> arm_late_halt(16), beam_boot:finish(6).
service_then_halt(_) -> arm_late_halt(16), exit(whereis(beam_output_server), kill), receive after 5000 -> error(component_did_not_stop) end.


collection_library_cases() ->
    true = beam_lists:member(1, [1,1.0]),
    false = beam_lists:member(1.0, [1]),
    true = beam_lists:member(#{a=>1}, [#{a=>1}]),
    true = beam_lists:member(a, [a|bad]),
    false = beam_lists:member(anything, []),
    tail = beam_lists:reverse([], tail),
    [b,a|tail] = beam_lists:reverse([a,b], tail),
    [1,2,3] = beam_lists:sort([3,1,2]),
    [{1,value}] = beam_lists:keysort(1,[{1,value}]),
    {1,value} = beam_lists:keyfind(1.0,1,[{1,value}]),
    {ok,1} = beam_maps:find(key, #{key=>1}),
    error = beam_maps:find(key, #{}),
    #{key:=2} = beam_maps:put(key, 2, #{key=>1}),
    [{a,1},{b,2}] = beam_lists:sort(beam_maps:to_list(#{b=>2,a=>1})),
    {a,1,_} = beam_maps:next(beam_maps:iterator(#{b=>2,a=>1},ordered)),
    ok = collection_badarg(beam_lists, member, [a,bad]),
    ok = collection_badarg(beam_lists, member, [b,[a|bad]]),
    ok = collection_badarg(beam_lists, reverse, [[a|bad],[]]),
    {'EXIT',{{badmap,bad},_}} = catch beam_maps:find(key,bad),
    {'EXIT',{{badmap,bad},_}} = catch beam_maps:put(key,value,bad),
    {'EXIT',{{badkey,key},_}} = catch beam_maps:get(key,#{}),
    Proper = collection_list(1000000, []),
    Improper = collection_list(1000000, bad),
    ok = collection_yield(fun() -> beam_lists:member(missing,Proper) end,false),
    ok = collection_yield(fun() -> collection_member_error(Improper) end,ok),
    ok = collection_yield(fun() -> length(beam_lists:reverse(Proper,[])) end,1000000),
    ok = collection_yield(fun() -> collection_reverse_error(Improper) end,ok),
    ok = text_library_cases(),
    ok = record_library_cases(),
    ok.

collection_member_error(List) ->
    try beam_lists:member(missing,List) of _ -> error(expected_member_error)
    catch error:badarg:Stack ->
        [{beam_lists,member,[missing,Remaining],_}|_] = Stack,
        true = Remaining =/= List, ok end.

collection_reverse_error(List) ->
    try beam_lists:reverse(List,tail) of _ -> error(expected_reverse_error)
    catch error:badarg:Stack ->
        [{beam_lists,reverse,Args,_}|_] = Stack,
        true = Args =/= [List,tail], ok end.

text_library_cases() ->
    ok = collection_badarg(beam_unicode,characters_to_binary,[<<1:1>>]),
    ok = unicode_valid_cases([<<>>,<<"ascii">>,<<16#A2/utf8,16#20AC/utf8,16#10FFFF/utf8>>]),
    1 = beam_string:length([16#65,16#301]),
    [[16#65,16#301]] = beam_string:to_graphemes([16#65,16#301]),
    <<"STRASSE">> = beam_string:uppercase(<<"stra",16#DF/utf8,"e">>),
    <<"name">> = beam_string:trim(<<"  name  ">>),
    {42,"tail"} = beam_string:to_integer("42tail"),
    {1.5,"tail"} = beam_string:to_float("1.5tail"),
    <<16#E9/utf8>> = beam_unicode:characters_to_nfc_binary([16#65,16#301]),
    [16#65,16#301] = beam_unicode:characters_to_nfd_list(<<16#E9/utf8>>),
    <<16#E9/utf8>> = beam_unicode:characters_to_binary([<<16#E9>>],latin1),
    [16#1F600] = beam_unicode:characters_to_list(<<16#1F600/utf16-little>>,{utf16,little}),
    <<16#1F600:32/little>> = beam_unicode:characters_to_binary([16#1F600],unicode,{utf32,little}),
    {incomplete,<<>>,<<0>>} = beam_unicode:characters_to_binary(<<0>>,utf16),
    {error,<<>>,[[16#20AC]]} = beam_unicode:characters_to_binary([16#20AC],unicode,latin1),
    {utf8,3} = beam_unicode:bom_to_encoding(<<239,187,191,120>>),
    <<239,187,191>> = beam_unicode:encoding_to_bom(utf8),
    {error,<<>>,<<128>>} = beam_unicode:characters_to_binary(<<128>>),
    {incomplete,<<>>,<<226,130>>} = beam_unicode:characters_to_binary(<<226,130>>),
    Text = beam_binary:copy(<<16#E9/utf8>>,8*1024*1024),
    Text = beam_unicode:characters_to_binary(Text),
    %% Nested input forces conversion; the canonical valid-binary fast path may return directly.
    ok = collection_yield(fun()->beam_unicode:characters_to_binary([Text,[]]) end,Text),
    ok = collection_yield(fun()->beam_unicode:characters_to_binary(<<Text/binary,128>>) end,
                          {error,Text,<<128>>}),
    ok = unicode_validation_cases([<<128>>,<<192,128>>,<<224,128,128>>,<<237,160,128>>,
                                   <<244,144,128,128>>,<<245,128,128,128>>],error),
    ok = unicode_validation_cases([<<226,130>>],incomplete),
    false = beam:function_exported(beam_string,concat,2),
    false = beam:function_exported(beam_lists,zf,2),
    true = beam:function_exported(beam_string,list_to_float,1),
    true = beam:function_exported(beam_maps,is_iterator_valid,1),
    ok.

unicode_valid_cases([]) -> ok;
unicode_valid_cases([Bytes|Rest]) ->
    Bytes = beam_unicode:characters_to_binary(Bytes),
    Bytes = beam_unicode:characters_to_binary(byte_unaligned(Bytes)),
    unicode_valid_cases(Rest).

unicode_validation_cases([],_) -> ok;
unicode_validation_cases([Bytes|Rest],Kind) ->
    {Kind,<<>>,Bytes}=beam_unicode:characters_to_binary(Bytes),
    {Kind,<<>>,Bytes}=beam_unicode:characters_to_binary(byte_unaligned(Bytes)),
    unicode_validation_cases(Rest,Kind).

record_library_cases() ->
    {#{is_exported:=true},[{field,default}]} =
        beam_records:get_definition(?MODULE,contract_definition),
    LanguageRecord=#contract_definition{},
    default=beam_records:get(field,LanguageRecord),
    Record = beam_records:create(contract,value,[{b,2},{a,1}],#{is_exported=>false}),
    contract = beam_records:get_module(Record),
    value = beam_records:get_name(Record),
    [b,a] = beam_records:get_field_names(Record),
    false = beam_records:is_exported(Record),
    1 = beam_records:get(a,Record),
    Changed = beam_records:update(Record,contract,value,#{a=>3}),
    3 = beam_records:get(a,Changed),
    1 = beam_records:get(a,Record),
    {'EXIT',{{badmap,bad},_}} = catch beam_records:update(Record,contract,value,bad),
    {'EXIT',{{badrecord,not_record},_}} = catch beam_records:get_field_names(not_record),
    ok.

collection_badarg(Module, Function, Arguments) ->
    try apply(Module, Function, Arguments) of
        _ -> error({expected_collection_badarg,Module,Function})
    catch error:badarg:Stack ->
        case Stack of
            [{Module,Function,Actual,_}|_] ->
                true = Actual =:= Arguments,
                ok;
            _ -> error({unexpected_collection_error_identity,Module,Function})
        end
    end.

collection_list(0, Tail) -> Tail;
collection_list(N, Tail) -> collection_list(N-1, [N|Tail]).

collection_yield(Fun, Expected) ->
    Parent = self(),
    {Pid,Mon} = erlang:spawn_monitor(fun() ->
        receive go -> ok end,
        Parent ! {collection_started,self()},
        Value = Fun(),
        Parent ! {collection_done,self(),Value}
    end),
    Pid ! go,
    receive {collection_started,Pid} -> ok
    after 5000 -> error(collection_start_timeout) end,
    ok = collection_active(Pid, 1000),
    receive {collection_done,Pid,Expected} -> ok;
            {collection_done,Pid,Other} -> error({unexpected_collection_result,Other});
            {'DOWN',Mon,process,Pid,Reason} -> error({collection_crashed,Reason})
    after 10000 -> error(collection_result_timeout) end,
    receive {'DOWN',Mon,process,Pid,normal} -> ok
    after 5000 -> error(collection_owner_timeout) end.

collection_active(_, 0) -> error(collection_never_yielded);
collection_active(Pid, Attempts) ->
    case process_info(Pid, [current_function,reductions]) of
        [{current_function,{erlang,characters_to_utf8_trap,4}},{reductions,R}]
          when R >= 1000 -> ok;
        [{current_function,{Module,_,_}},{reductions,R}]
          when R >= 1000, Module =:= beam_lists; R >= 1000, Module =:= beam_unicode;
               R >= 1000, Module =:= beam_atomics -> ok;
        undefined -> error(collection_finished_before_observation);
        _ -> beam:yield(), collection_active(Pid, Attempts-1)
    end.

%% These contracts run in the modern-only library lifetime: no canonical public
%% atomics/counters facades or persistent-term bindings may be needed here.
state_library_cases() ->
    false = beam:module_loaded(atomics),
    false = beam:module_loaded(counters),
    false = beam:is_builtin(persistent_term, put, 2),
    A = beam_atomics:new(3, []),
    #{size := 3, min := Min, max := Max, memory := Memory} = beam_atomics:info(A),
    Min = -(1 bsl 63), Max = (1 bsl 63)-1, true = Memory > 0,
    0 = beam_atomics:get(A, 1),
    ok = beam_atomics:put(A, 1, Max),
    Min = beam_atomics:add_get(A, 1, 1),
    ok = beam_atomics:sub(A, 1, 1),
    Max = beam_atomics:get(A, 1),
    Max = beam_atomics:exchange(A, 1, 2),
    2 = beam_atomics:compare_exchange(A, 1, 3, 4),
    ok = beam_atomics:compare_exchange(A, 1, 2, 4),
    3 = beam_atomics:sub_get(A, 1, 1),
    U = beam_atomics:new(1, [{signed,true},{signed,false}]),
    #{min := 0, max := UMax} = beam_atomics:info(U),
    UMax = (1 bsl 64)-1,
    ok = beam_atomics:put(U, 1, UMax),
    0 = beam_atomics:add_get(U, 1, 1),
    #{min := Min} = beam_atomics:info(beam_atomics:new(1, [{signed,false},{signed,true}])),
    ok = state_badarg(beam_atomics, new, [bad,[unknown]], badopt),
    ok = state_badarg(beam_atomics, new, [bad,[]], none),
    ok = state_system_limit(beam_atomics, new, [1 bsl 200,[]]),
    ok = collection_badarg(beam_atomics, get, [bad,1]),
    ok = collection_badarg(beam_atomics, get, [A,0]),
    ok = collection_badarg(beam_atomics, put, [A,4,0]),
    ok = collection_badarg(beam_atomics, put, [A,1,Max+1]),
    ok = state_subtraction(beam_atomics, sub),
    ok = state_subtraction(beam_atomics, sub_get),
    1 = beam:system_flag(schedulers_online, 2),
    Parent = self(),
    AtomicWorkers = [erlang:spawn_monitor(fun() ->
        state_atomic_add(A, 10000), Parent ! {atomic_done,self()}
    end) || _ <- [1,2,3,4]],
    state_workers(AtomicWorkers, atomic_done),
    40000 = beam_atomics:get(A, 2),
    ok = state_counter_cases([]),
    ok = state_counter_cases([atomics]),
    ok = state_counter_cases([write_concurrency]),
    ok = state_badarg(beam_counters, new, [bad,[unknown]], badopt),
    ok = collection_badarg(beam_counters, new, [1,[atomics,atomics]]),
    ok = collection_badarg(beam_counters, new, [1,[write_concurrency,atomics]]),
    ok = collection_badarg(beam_counters, new, [bad,[]]),
    ok = state_system_limit(beam_counters, new, [1 bsl 200,[]]),
    ok = collection_badarg(beam_counters, get, [bad,1]),
    ok = state_subtraction(beam_counters, sub),
    2 = beam:system_flag(schedulers_online, 1),
    %% Bounded option traversal must preserve late error arguments and cause.
    Overflow = ((1 bsl (beam:system_info(wordsize)*8))-1) div 8,
    ok = state_system_limit(beam_atomics, new, [Overflow,[]]),
    Options = state_options(1000000, []),
    ok = collection_yield(fun() ->
        #{min := Min} = beam_atomics:info(beam_atomics:new(1, Options)), ok
    end, ok),
    ok = collection_yield(fun() -> state_system_limit(beam_atomics,new,[Overflow,Options]) end, ok),
    Invalid = state_options(1000000, [bad]),
    ok = collection_yield(fun() -> state_badarg(beam_atomics,new,[bad,Invalid],badopt) end, ok),
    1 = beam:system_flag(schedulers_online, 2),
    ok = state_persistent_cases(A),
    2 = beam:system_flag(schedulers_online, 1),
    ok.

state_options(0, Tail) -> Tail;
state_options(N, Tail) -> state_options(N-1, [{signed,true}|Tail]).

state_counter_cases(Options) ->
    C = beam_counters:new(2, Options),
    Info = beam_counters:info(C),
    #{size := 2, memory := Memory} = Info, true = Memory > 0,
    case Options of
        [write_concurrency] -> 2 = map_size(Info);
        _ -> #{min := -9223372036854775808, max := 9223372036854775807} = Info
    end,
    0 = beam_counters:get(C, 1),
    ok = beam_counters:put(C, 1, 9223372036854775807),
    ok = beam_counters:add(C, 1, 1),
    -9223372036854775808 = beam_counters:get(C, 1),
    ok = beam_counters:sub(C, 1, 1),
    9223372036854775807 = beam_counters:get(C, 1),
    ok = collection_badarg(beam_counters, get, [C,3]),
    ok = collection_badarg(beam_counters, put, [C,1,1 bsl 64]),
    Parent = self(),
    Workers = [erlang:spawn_monitor(fun() -> state_counter_add(C, 10000), Parent ! {counter_done,self()} end)
               || _ <- [1,2,3,4]],
    state_workers(Workers, counter_done),
    40000 = beam_counters:get(C, 2),
    ok.

state_counter_add(_, 0) -> ok;
state_counter_add(C, N) -> ok = beam_counters:add(C, 2, 1), state_counter_add(C, N-1).

state_workers([], _) -> ok;
state_workers([{Pid,Mon}|Rest], Tag) ->
    receive {Tag,Pid} -> ok after 10000 -> error(state_worker_timeout) end,
    receive {'DOWN',Mon,process,Pid,normal} -> ok after 5000 -> error(state_worker_exit) end,
    state_workers(Rest, Tag).

state_subtraction(Module, Function) ->
    try apply(Module, Function, [bad,bad,nonnumeric]) of
        _ -> error(expected_badarith)
    catch error:badarith:Stack ->
        [{Module,Function,[bad,bad,nonnumeric],_}|_] = Stack, ok
    end,
    collection_badarg(Module, Function, [bad,bad,1]).

state_badarg(Module, Function, Args, Cause) ->
    try apply(Module, Function, Args) of
        _ -> error({expected_state_badarg,Module,Function})
    catch error:badarg:Stack ->
        [{Module,Function,Actual,Location}|_] = Stack,
        true = Actual =:= Args,
        case Cause of
            none -> ok;
            _ -> {error_info,#{cause := Cause}} = state_error_info(Location)
        end, ok
    end.

state_error_info([Entry={error_info,_}|_]) -> Entry;
state_error_info([_|Rest]) -> state_error_info(Rest).

state_persistent_cases(Atomic) ->
    Key = {?MODULE, state},
    missing = beam_persistent_term:get(Key, missing),
    ok = collection_badarg(beam_persistent_term, get, [Key]),
    Value = {Atomic,beam_binary:copy(<<"retained">>, 8192),[a,b,c]},
    ok = beam_persistent_term:put(Key, Value),
    Value = beam_persistent_term:get(Key),
    ok = beam_persistent_term:put_new(Key, Value),
    ok = collection_badarg(beam_persistent_term, put_new, [Key,different]),
    true = member({Key,Value}, beam_persistent_term:get()),
    #{count := Count, memory := Bytes} = beam_persistent_term:info(),
    true = Count > 0, true = Bytes > 0,
    true = beam_persistent_term:erase(Key),
    false = beam_persistent_term:erase(Key),
    %% The retained literal and resource survive removal from the store.
    {Atomic,_,_} = Value,
    3 = beam_atomics:get(Atomic, 1),
    Parent = self(),
    Workers = [erlang:spawn_monitor(fun() ->
        state_persistent_updates({Key,N},100), Parent ! {persistent_done,self()}
    end) || N <- [1,2,3,4]],
    state_workers(Workers, persistent_done),
    false = erts_internal:dynamic_node_name(),
    ok = erts_internal:dynamic_node_name(false),
    missing = beam_persistent_term:get({erts_internal,dynamic_node_name}, missing),
    ok = beam_persistent_term:put({erts_internal,dynamic_node_name}, false),
    false = erts_internal:dynamic_node_name(),
    ok = erts_internal:dynamic_node_name(false),
    false = beam_persistent_term:get({erts_internal,dynamic_node_name}),
    ok = beam_persistent_term:put({erts_internal,dynamic_node_name}, other),
    true = erts_internal:dynamic_node_name(),
    ok = erts_internal:dynamic_node_name(false),
    missing = beam_persistent_term:get({erts_internal,dynamic_node_name}, missing),
    ok = erts_internal:dynamic_node_name(true),
    true = erts_internal:dynamic_node_name(),
    ok = erts_internal:dynamic_node_name(false),
    function_clause = try erts_internal:dynamic_node_name(other)
                      catch error:function_clause -> function_clause end,
    ok.

state_persistent_updates(Key, 0) -> true = beam_persistent_term:erase(Key), ok;
state_persistent_updates(Key, N) ->
    ok = beam_persistent_term:put(Key, {N,beam_binary:copy(<<0>>, 1024)}),
    {N,_} = beam_persistent_term:get(Key),
    _ = beam_persistent_term:info(),
    _ = beam_persistent_term:get(),
    state_persistent_updates(Key, N-1).

state_system_limit(Module, Function, Args) ->
    try apply(Module, Function, Args) of
        _ -> error(expected_state_system_limit)
    catch error:system_limit:Stack ->
        [{Module,Function,Actual,_}|_] = Stack, true = Actual =:= Args, ok
    end.

state_atomic_add(_, 0) -> ok;
state_atomic_add(A, N) ->
    _ = beam_atomics:add_get(A, 2, 1), state_atomic_add(A, N-1).
