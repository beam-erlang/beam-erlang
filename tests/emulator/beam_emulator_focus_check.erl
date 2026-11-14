%% Bounded real Common Test regression checks; never run the retained corpus.
-module(beam_emulator_focus_check).
-export([run/1]).

run([Image, Scratch]) ->
    Suite = "beam_emulator_focus_fixture_SUITE",
    SpecDir = filename:join(Scratch, "specs"),
    Corpus = filename:join(Scratch, "emulator_test"),
    ok = filelib:ensure_dir(filename:join(SpecDir, "placeholder")),
    ok = filelib:ensure_dir(filename:join(Corpus, "placeholder")),
    Fixture = filename:join([Image, "support", Suite ++ ".beam"]),
    {ok, _} = file:copy(Fixture, filename:join(Corpus, Suite ++ ".beam")),
    true = code:add_patha(Corpus),
    ok = file:write_file(filename:join(SpecDir, "emulator.spec"),
                        "{enable_builtin_hooks,false}.\n"
                        "{suites,\"../emulator_test\",all}.\n"),
    Scope = io_lib:format("{skip_groups,\"../emulator_test\",~s,[excluded_group],"
                          "\"fixture functional exclusion\"}.~n"
                          "{skip_cases,\"../emulator_test\",unrelated_SUITE,[other],"
                          "\"unrelated exclusion\"}.~n", [Suite]),
    ok = file:write_file(filename:join(SpecDir, "functional-scope.spec"), Scope),
    Whole = focus(Scratch, "whole", Suite, "", ""),
    [<<"beam_emulator_focus_fixture_SUITE">>] = maps:get(execution_suites, Whole),
    <<"ct_planned">> = maps:get(coverage_mode, Whole),
    [_] = maps:get(applied_exclusions, Whole),
    {ok, WholeTerms} = file:consult(binary_to_list(maps:get(spec, Whole))),
    [{suites, "../emulator_test", [beam_emulator_focus_fixture_SUITE]}] =
        [T || T <- WholeTerms, element(1, T) =:= suites],
    false = lists:any(fun(T) -> lists:member(unrelated_SUITE, tuple_to_list(T)) end,
                      WholeTerms),
    Top = focus(Scratch, "top", Suite, "", "top_case"),
    {1, 0, {0, 0}} = execute(Top, filename:join(Scratch, "ct-top")),
    Nested = focus(Scratch, "nested", Suite, "target", ""),
    [<<"child">>, <<"parent">>, <<"target">>] = maps:get(expected_groups, Nested),
    [[<<"parent">>], [<<"parent">>, <<"target">>],
     [<<"parent">>, <<"target">>, <<"child">>]] = maps:get(expected_group_paths, Nested),
    [<<"descendant_case">>, <<"nested_case">>] = case_texts(Nested),
    {2, 0, {0, 0}} = execute(Nested, filename:join(Scratch, "ct-nested")),
    One = focus(Scratch, "one", Suite, "target", "descendant_case"),
    [<<"descendant_case">>] = case_texts(One),
    {1, 0, {0, 0}} = execute(One, filename:join(Scratch, "ct-one")),
    Guard = focus(Scratch, "guard", Suite, "guard_parent", ""),
    true = lists:member([<<"guard_parent">>, <<"skipped_group">>, <<"skipped_child">>],
                         maps:get(expected_group_paths, Guard)),
    {1, 0, {1, 0}} = execute(Guard, filename:join(Scratch, "ct-guard")),
    OnlySkipped = focus(Scratch, "only-skipped", Suite, "skipped_group", ""),
    {0, 0, {1, 0}} = execute(OnlySkipped, filename:join(Scratch, "ct-only-skipped")),
    rejects(fun() -> focus(Scratch, "ambiguous", Suite, "ambiguous", "") end,
            ambiguous_focused_group),
    rejects(fun() -> focus(Scratch, "duplicate-descendant", Suite, "duplicate_root", "") end,
            ambiguous_focused_group_occurrences),
    rejects(fun() -> focus(Scratch, "missing-group", Suite, "missing", "") end,
            unknown_focused_group),
    rejects(fun() -> focus(Scratch, "missing-case", Suite, "", "top_case,missing") end,
            unknown_or_nontoplevel_focused_cases),
    rejects(fun() -> focus(Scratch, "grouped-without-group", Suite, "", "nested_case") end,
            unknown_or_nontoplevel_focused_cases),
    rejects(fun() -> focus(Scratch, "excluded", Suite, "excluded_group", "") end,
            focused_selection_outside_functional_scope),
    lifecycle_checks(filename:join(Scratch, "lifecycle")),
    io:format("Focused emulator regression checks passed~n"),
    ok.

lifecycle_checks(Image) ->
    SpecDir = filename:join(Image, "specs"),
    Corpus = filename:join(Image, "emulator_test"),
    FixtureSource = filename:join(Image, "fixture-source"),
    lists:foreach(fun(Dir) -> ok = filelib:ensure_dir(filename:join(Dir, "placeholder")) end,
                  [SpecDir, Corpus, FixtureSource]),
    ok = file:write_file(filename:join(SpecDir, "emulator.spec"),
                        "{enable_builtin_hooks,false}.\n"
                        "{suites,\"../emulator_test\",all}.\n"),
    ok = file:write_file(filename:join(SpecDir, "functional-scope.spec"), <<>>),
    %% These tiny sources are project-owned payloads in the disposable image,
    %% not edits to the retained upstream a_SUITE/z_SUITE sources.
    fixture_module(a_SUITE, FixtureSource, Corpus,
                   "-module(a_SUITE).\n"
                   "-export([all/0,start_state/1]).\n"
                   "all()->[start_state].\n"
                   "start_state(_)->"
                   "undefined=persistent_term:get({beam_focus_lifecycle,state},undefined),"
                   "persistent_term:put({beam_focus_lifecycle,state},[a_SUITE]),ok.\n"),
    fixture_module(z_SUITE, FixtureSource, Corpus,
                   "-module(z_SUITE).\n"
                   "-export([all/0,finish_state/1]).\n"
                   "all()->[finish_state].\n"
                   "finish_state(_)->"
                   "[a_SUITE]=persistent_term:get({beam_focus_lifecycle,state}),"
                   "true=persistent_term:erase({beam_focus_lifecycle,state}),ok.\n"),
    lists:foreach(fun(Primary) ->
                          Metadata = focus(Image, Primary, Primary, "", ""),
                          [<<"a_SUITE">>, <<"z_SUITE">>] = maps:get(execution_suites, Metadata),
                          PrimaryBinary = unicode:characters_to_binary(Primary),
                          PrimaryBinary = maps:get(suite, maps:get(selection, Metadata)),
                          {ok, Terms} = file:consult(binary_to_list(maps:get(spec, Metadata))),
                          [{suites, "../emulator_test", [a_SUITE, z_SUITE]}] =
                              [T || T <- Terms, element(1, T) =:= suites],
                          {2, 0, {0, 0}} = execute(Metadata, filename:join(Image, "ct-" ++ Primary)),
                          undefined = persistent_term:get({beam_focus_lifecycle,state},undefined),
                          rejects(fun() -> focus(Image, "partial-group", Primary, "any_group", "") end,
                                  lifecycle_pair_requires_whole_suites),
                          rejects(fun() -> focus(Image, "partial-case", Primary, "", "any_case") end,
                                  lifecycle_pair_requires_whole_suites)
                  end, ["a_SUITE", "z_SUITE"]).

fixture_module(Module, SourceDir, Corpus, Contents) ->
    Source = filename:join(SourceDir, atom_to_list(Module) ++ ".erl"),
    ok = file:write_file(Source, Contents),
    {ok, Module, Binary, []} = compile:file(Source, [binary, return_errors, return_warnings]),
    ok = file:write_file(filename:join(Corpus, atom_to_list(Module) ++ ".beam"), Binary),
    _ = code:purge(Module),
    _ = code:delete(Module),
    ok.

focus(Image, Name, Suite, Group, Cases) ->
    beam_emulator_focus:prepare(Image, filename:join(Image, "logs-" ++ Name),
                                Suite, Group, Cases).

case_texts(Metadata) ->
    lists:sort([maps:get('case', C) || C <- maps:get(expected_cases, Metadata)]).

execute(Metadata, Logs) ->
    ok = filelib:ensure_dir(filename:join(Logs, "placeholder")),
    ct:run_test([{spec, binary_to_list(maps:get(spec, Metadata))},
                 {auto_compile, false}, {abort_if_missing_suites, true},
                 {logdir, Logs}]).

rejects(Fun, Tag) ->
    try Fun() of
        Value -> erlang:error({expected_focused_rejection, Tag, Value})
    catch
        error:Reason when is_tuple(Reason), element(1, Reason) =:= Tag -> ok
    end.
