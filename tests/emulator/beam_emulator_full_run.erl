%% Observe the unchanged upstream specification without replacing its lifecycle.
-module(beam_emulator_full_run).
-export([run/1]).

run([Image, FlavorText, BuildTypeText, LogDir, Selection, CasesText]) ->
    run([Image, FlavorText, BuildTypeText, LogDir, Selection, CasesText, ""]);
run([Image, FlavorText, BuildTypeText, LogDir, Selection, CasesText, GroupText]) ->
    Flavor = erlang:system_info(emu_flavor),
    FlavorText = atom_to_list(Flavor),
    Expected = case BuildTypeText of
                   "opt" -> {opt, false, false};
                   "debug" -> {debug, true, true}
               end,
    Expected = {erlang:system_info(build_type), erlang:system_info(debug_compiled),
                erlang:system_info(lock_checking)},
    Image = code:root_dir(),
    Root = filename:join(Image, "emulator_test"),
    CompilerConfig = filename:join(Image, "compiler_config"),
    case file:read_file_info(filename:join(CompilerConfig, "variables")) of
        {ok, _} -> ok;
        _ -> erlang:error({missing_compiler_configuration, CompilerConfig})
    end,
    true = code:add_patha(Root),
    true = code:add_patha(filename:join(Image, "support")),
    %% Front of the path, and not test_server/variables.
    true = code:add_patha(CompilerConfig),
    {ok, _} = application:ensure_all_started(os_mon),
    Memory = memsup:get_system_memory_data(),
    true = is_list(Memory) andalso Memory =/= [],
    {OSFamily, OSName} = os:type(),
    Facts = #{flavor => Flavor, dynamic_trace => erlang:system_info(dynamic_trace),
              os_family => OSFamily, os_name => OSName,
              lock_counting => erlang:system_info(lock_counting),
              architecture => erlang:system_info(system_architecture),
              wordsize => erlang:system_info(wordsize),
              build_type => erlang:system_info(build_type),
              debug_compiled => erlang:system_info(debug_compiled),
              lock_checking => erlang:system_info(lock_checking),
              schedulers => erlang:system_info(schedulers_online),
              memory => Memory, node => node(), root => Image,
              suite_all_empty => empty_guard_suites(),
              perf => os:find_executable("perf"),
              property_tools => [T || T <- [proper,eqc,triq], code:which(T) =/= non_existing]},
    TimezoneFacts = maps:from_list([{binary_to_atom(K, utf8), V} || {K, V} <-
                                      maps:to_list(beam_emulator_events:timezone_facts())]),
    ok = write_json(filename:join(LogDir, "runtime-facts.json"),
                    maps:merge(Facts, TimezoneFacts)),
    Base = [{auto_compile,false}, {abort_if_missing_suites,true}, {logdir,LogDir},
            {event_handler,[{beam_emulator_events,[LogDir]}]}],
    Options = case Selection of
        "full" ->
            BaseSpec = filename:join([Image,"specs","emulator.spec"]),
            ScopeSpec = filename:join([Image,"specs","functional-scope.spec"]),
            case {file:read_file_info(BaseSpec), file:read_file_info(ScopeSpec)} of
                {{ok, _}, {ok, _}} -> ok;
                _ -> erlang:error({missing_full_specification, BaseSpec, ScopeSpec})
            end,
            %% join_specs merges the files into one campaign. Separate runs
            %% reopen the event journal and drop the single a_SUITE/z_SUITE lifecycle.
            [{spec,[BaseSpec, ScopeSpec]}, {join_specs, true} | Base];
        _ ->
            Focus = beam_emulator_focus:prepare(Image, LogDir, Selection, GroupText, CasesText),
            [{spec,binary_to_list(maps:get(spec, Focus))}|Base]
    end,
    Result = ct:run_test(Options),
    ok = file:write_file(filename:join(LogDir,"ct-result.term"),io_lib:format("~tp.~n",[Result])),
    case Result of
        {Passed,Failed,{User,Auto}} ->
            ok = write_json(filename:join(LogDir,"ct-result.json"),
                            #{passed=>Passed,failed=>Failed,user_skipped=>User,auto_skipped=>Auto}),
            io:format("emulator_corpus_result ~p~n",[Result]),
            Completed = case Selection of
                            "full" -> Passed > 0;
                            _ -> Passed + User > 0 orelse empty_guard_selection(Selection, GroupText, CasesText)
                        end,
            case Failed =:= 0 andalso Auto =:= 0 andalso Completed of
                true -> ok;
                false -> erlang:error({emulator_corpus_nonpassing,Result})
            end;
        _ -> erlang:error({emulator_corpus_invalid_result,Result})
    end.

empty_guard_suites() ->
    %% Only the pinned, source-audited all/0 platform guard is probed here.
    %% Do not invoke every suite's inventory a second time during startup.
    case code:ensure_loaded(os_signal_SUITE) of
        {module, os_signal_SUITE} ->
            case os_signal_SUITE:all() of
                [] -> [os_signal_SUITE];
                _ -> []
            end;
        _ -> []
    end.

empty_guard_selection("os_signal_SUITE", "", "") ->
    case os:type() of
        {win32, _} -> empty_guard_suites() =:= [os_signal_SUITE];
        _ -> false
    end;
empty_guard_selection(_, _, _) -> false.

write_json(Path, Term) ->
    file:write_file(Path,[json:encode(json_term(Term)),"\n"]).

json_term(M) when is_map(M) -> maps:from_list([{atom_to_binary(K,utf8),json_term(V)} || {K,V} <- maps:to_list(M)]);
json_term(A) when is_atom(A) -> atom_to_binary(A,utf8);
json_term(T) when is_tuple(T) -> [json_term(V) || V <- tuple_to_list(T)];
json_term([]) -> [];
json_term(L) when is_list(L) ->
    case io_lib:printable_unicode_list(L) of
        true -> unicode:characters_to_binary(L);
        false -> [json_term(V) || V <- L]
    end;
json_term(V) -> V.
