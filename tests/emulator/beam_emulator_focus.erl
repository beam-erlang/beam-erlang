%% Focused diagnostics use the pinned Common Test group expansion, without
%% changing the retained suites or the complete-corpus specification.
-module(beam_emulator_focus).
-export([prepare/5]).

prepare(Image, LogDir, SuiteText, GroupText, CasesText) ->
    Root = filename:join(Image, "emulator_test"),
    true = code:add_patha(Root),
    Suite = list_to_atom(SuiteText),
    Cases = case_names(CasesText),
    Group = case GroupText of "" -> none; _ -> list_to_atom(GroupText) end,
    ExecutionSuites = execution_suites(Suite, Group, Cases),
    lists:foreach(fun(S) ->
                          {module, S} = code:ensure_loaded(S),
                          Root = filename:dirname(code:which(S))
                  end, ExecutionSuites),
    SpecDir = filename:join(Image, "specs"),
    {Globals, Directory, Exclusions} = specifications(SpecDir, Root),
    Matching = [E || E <- Exclusions, lists:member(maps:get(suite, E), ExecutionSuites)],
    {SelectionTerm, CoverageMode, ExpectedCases, ExpectedGroups, GroupPaths, Applied} =
        case ExecutionSuites of
            [Suite] -> selection(Suite, Group, Cases, Directory, Matching);
            [a_SUITE, z_SUITE] ->
                {{suites, Directory, ExecutionSuites}, <<"ct_planned">>,
                 [], [], [], Matching}
        end,
    Spec = filename:join(SpecDir, "focused.spec"),
    Terms = Globals ++ [SelectionTerm] ++ [maps:get(term, E) || E <- Applied],
    ok = file:write_file(Spec, ["%% Generated focused diagnostic specification.\n",
                              [io_lib:format("~tp.~n", [T]) || T <- Terms]]),
    Metadata = #{spec => text(Spec), coverage_mode => CoverageMode,
                 execution_suites => [text(S) || S <- ExecutionSuites],
                 selection => #{mode => <<"focused">>, suite => text(SuiteText),
                                group => text(GroupText),
                                cases => [text(C) || C <- Cases]},
                 expected_cases => [#{module => text(M), 'case' => text(C)}
                                    || {M, C} <- ExpectedCases],
                 expected_groups => [text(G) || G <- ExpectedGroups],
                 expected_group_paths => [[text(G) || G <- P] || P <- GroupPaths],
                 empty_suites => empty_suites(ExecutionSuites, Group, Cases),
                 applied_exclusions => [public_exclusion(E) || E <- Applied]},
    Inventory = filename:join(LogDir, "focus-inventory.json"),
    ok = filelib:ensure_dir(Inventory),
    ok = file:write_file(Inventory, [json:encode(json_keys(Metadata)), "\n"]),
    Metadata.

%% This is an observation, not acceptance. The outer auditor must match an
%% exact source-hashed native guard before it may account for a zero-case suite.
empty_suites([os_signal_SUITE], none, []) ->
    case os_signal_SUITE:all() of
        [] -> [<<"os_signal_SUITE">>];
        _ -> []
    end;
empty_suites(_, _, _) -> [].

execution_suites(Suite, none, []) when Suite =:= a_SUITE; Suite =:= z_SUITE ->
    [a_SUITE, z_SUITE];
execution_suites(Suite, _, _) when Suite =:= a_SUITE; Suite =:= z_SUITE ->
    erlang:error({lifecycle_pair_requires_whole_suites, Suite,
                  select_a_SUITE_or_z_SUITE_without_group_or_cases});
execution_suites(Suite, _, _) -> [Suite].

case_names("") -> [];
case_names(Text) ->
    Parts = string:split(Text, ",", all),
    case lists:any(fun(P) -> P =:= "" orelse string:trim(P) =/= P end, Parts) of
        true -> erlang:error({invalid_focused_cases, Text});
        false -> ok
    end,
    Names = [list_to_atom(P) || P <- Parts],
    case length(Names) =:= length(lists:usort(Names)) of
        true -> Names;
        false -> erlang:error({duplicate_focused_cases, Text})
    end.

specifications(SpecDir, Root) ->
    {ok, Base} = file:consult(filename:join(SpecDir, "emulator.spec")),
    {ok, Scope} = file:consult(filename:join(SpecDir, "functional-scope.spec")),
    [{suites, Directory, all}] = [T || T <- Base, element(1, T) =:= suites],
    "../emulator_test" = Directory,
    Root = filename:join(filename:dirname(SpecDir), "emulator_test"),
    [{enable_builtin_hooks, false}] = [T || T <- Base,
                                           element(1, T) =:= enable_builtin_hooks],
    Globals = [T || T <- Base, not selection_term(T), not skip_term(T)],
    %% Fail closed if a new base selection form is introduced upstream.
    [] = [T || T <- Base, selection_term(T), T =/= {suites, Directory, all}],
    [] = [T || T <- Scope, not skip_term(T)],
    Exclusions = [exclusion(T, Directory) || T <- Base ++ Scope, skip_term(T)],
    {Globals, Directory, Exclusions}.

selection_term(T) when is_tuple(T), tuple_size(T) > 0 ->
    lists:member(element(1, T), [suites, groups, cases, specs]);
selection_term(_) -> true.

skip_term(T) when is_tuple(T), tuple_size(T) > 0 ->
    lists:member(element(1, T), [skip_suites, skip_groups, skip_cases]);
skip_term(_) -> false.

exclusion({skip_cases, Directory, Suite, Cases, Reason} = Term, Directory)
  when is_atom(Suite), is_list(Cases), is_list(Reason) ->
    true = lists:all(fun is_atom/1, Cases),
    #{kind => cases, suite => Suite, targets => Cases, reason => Reason, term => Term};
exclusion({skip_groups, Directory, Suite, Groups, Reason} = Term, Directory)
  when is_atom(Suite), is_list(Groups), is_list(Reason) ->
    true = lists:all(fun is_atom/1, Groups),
    #{kind => groups, suite => Suite, targets => Groups, reason => Reason, term => Term};
exclusion(Term, _) ->
    erlang:error({unsupported_focused_exclusion, Term}).

selection(Suite, none, [], Directory, Exclusions) ->
    {{suites, Directory, [Suite]}, <<"ct_planned">>, [], [], [], Exclusions};
selection(Suite, none, Cases, Directory, Exclusions) ->
    All = Suite:all(),
    true = is_list(All),
    Direct = direct_cases(All),
    require_cases(Cases, Direct, Suite, none),
    Leaves = [{Suite, C} || C <- Cases],
    reject_exclusions(Exclusions, Leaves, []),
    {{cases, Directory, Suite, Cases}, <<"exact_cases">>, Leaves, [], [], []};
selection(Suite, Group, Cases, Directory, Exclusions) ->
    Definitions = case erlang:function_exported(Suite, groups, 0) of
                      true -> Suite:groups();
                      false -> []
                  end,
    true = is_list(Definitions),
    Expanded0 = ct_groups:find_groups(Suite, Group, all, Definitions),
    Expanded = ct_groups:delete_subs(Expanded0, Expanded0),
    {Available, _AvailableGroups, Occurrences} = inventory(Expanded, Suite, Group),
    case Occurrences of
        0 -> erlang:error({unknown_focused_group, Suite, Group});
        1 -> ok;
        _ -> erlang:error({ambiguous_focused_group, Suite, Group,
                           use_suite_wide_diagnostic})
    end,
    case Cases of
        [] -> ok;
        _ -> require_cases(Cases, [C || {_, C} <- Available], Suite, Group)
    end,
    Selected0 = ct_groups:find_groups(Suite, Group, cases_or_all(Cases), Definitions),
    Selected = ct_groups:delete_subs(Selected0, Selected0),
    {Leaves, SelectedGroups, 1} = inventory(Selected, Suite, Group),
    GroupPaths = group_paths(Selected, []),
    GroupNames = [lists:last(Path) || Path <- GroupPaths],
    RepeatedGroups = lists:usort(GroupNames -- lists:usort(GroupNames)),
    case RepeatedGroups of
        [] -> ok;
        _ -> erlang:error({ambiguous_focused_group_occurrences, Suite, RepeatedGroups,
                           use_suite_wide_diagnostic})
    end,
    case Leaves of
        [] -> erlang:error({empty_focused_group, Suite, Group});
        _ -> ok
    end,
    %% The selected tree includes required ancestors and matching descendants,
    %% while pruning sibling branches when individual cases are requested.
    reject_exclusions(Exclusions, Leaves, SelectedGroups),
    Term = case Cases of
               [] -> {groups, Directory, Suite, Group};
               _ -> {groups, Directory, Suite, Group, {cases, Cases}}
           end,
    {Term, <<"exact_cases">>, Leaves, SelectedGroups, GroupPaths, []}.

cases_or_all([]) -> all;
cases_or_all(Cases) -> Cases.

direct_cases(Tests) ->
    lists:flatmap(fun(C) when is_atom(C) -> [C];
                     ({group, G}) when is_atom(G) -> [];
                     ({group, G, P}) when is_atom(G), is_list(P) -> [];
                     ({group, G, P, S}) when is_atom(G), is_list(P), is_list(S) -> [];
                     (Other) -> erlang:error({unsupported_focused_top_level,
                                              Other, use_suite_wide_diagnostic})
                  end, Tests).

require_cases(Cases, Available, Suite, Group) ->
    case Cases -- Available of
        [] -> ok;
        Missing -> erlang:error({unknown_or_nontoplevel_focused_cases,
                                 Suite, Group, Missing,
                                 use_group_for_grouped_cases})
    end.

inventory(Confs, Suite, Target) ->
    {Leaves, Groups, Count} = inventory_nodes(Confs, Suite, Target),
    {lists:usort(Leaves), lists:usort(Groups), Count}.

inventory_nodes([], _, _) -> {[], [], 0};
inventory_nodes([Node | Rest], Suite, Target) ->
    {L1, G1, N1} = inventory_node(Node, Suite, Target),
    {L2, G2, N2} = inventory_nodes(Rest, Suite, Target),
    {L1 ++ L2, G1 ++ G2, N1 + N2}.

inventory_node({conf, Props, _Init, Children, _End}, Suite, Target)
  when is_list(Props), is_list(Children) ->
    Group = proplists:get_value(name, Props),
    true = is_atom(Group),
    case proplists:get_value(suite, Props, Suite) of
        Suite -> ok;
        Other -> erlang:error({unsupported_focused_remote_group, Other})
    end,
    {Leaves, Groups, Count} = inventory_nodes(Children, Suite, Target),
    Increment = case Group of Target -> 1; _ -> 0 end,
    {Leaves, [Group | Groups], Count + Increment};
inventory_node({Suite, Case}, Suite, _) when is_atom(Case) ->
    {[{Suite, Case}], [], 0};
inventory_node({repeat, {Suite, Case}, _Property}, Suite, _) when is_atom(Case) ->
    {[{Suite, Case}], [], 0};
inventory_node(Other, _, _) ->
    erlang:error({unsupported_focused_inventory, Other, use_suite_wide_diagnostic}).

group_paths([], _) -> [];
group_paths([{conf, Props, _Init, Children, _End} | Rest], Parent) ->
    Path = Parent ++ [proplists:get_value(name, Props)],
    [Path | group_paths(Children, Path)] ++ group_paths(Rest, Parent);
group_paths([_Leaf | Rest], Parent) -> group_paths(Rest, Parent).

reject_exclusions(Exclusions, Leaves, Groups) ->
    Cases = [C || {_, C} <- Leaves],
    Intersected = [public_exclusion(E) || E <- Exclusions,
                     case maps:get(kind, E) of
                         cases -> intersects(Cases, maps:get(targets, E));
                         groups -> intersects(Groups, maps:get(targets, E))
                     end],
    case Intersected of
        [] -> ok;
        _ -> erlang:error({focused_selection_outside_functional_scope, Intersected})
    end.

intersects(A, B) -> lists:any(fun(E) -> lists:member(E, B) end, A).

public_exclusion(E) ->
    #{kind => text(maps:get(kind, E)), suite => text(maps:get(suite, E)),
      targets => [text(T) || T <- maps:get(targets, E)],
      reason => text(maps:get(reason, E))}.

text(A) when is_atom(A) -> atom_to_binary(A, utf8);
text(L) when is_list(L) -> unicode:characters_to_binary(L).

json_keys(M) when is_map(M) ->
    maps:from_list([{text(K), json_keys(V)} || {K, V} <- maps:to_list(M)]);
json_keys(L) when is_list(L) -> [json_keys(V) || V <- L];
json_keys(V) -> V.
