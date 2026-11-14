%% Project-owned regression fixture for the focused diagnostic interface.
-module(beam_emulator_focus_fixture_SUITE).
-export([all/0, groups/0, init_per_group/2, end_per_group/2,
         top_case/1, nested_case/1, descendant_case/1, sibling_case/1,
         ambiguous_case/1, guard_pass/1, skipped_case/1, excluded_case/1]).

all() ->
    [top_case, {group, parent}, {group, ambiguous_parent_a},
     {group, ambiguous_parent_b}, {group, duplicate_root},
     {group, guard_parent}, {group, excluded_group}].

groups() ->
    [{parent, [], [{group, target}, {group, sibling}]},
     {target, [], [nested_case, {group, child}]},
     {child, [], [descendant_case]},
     {sibling, [], [sibling_case]},
     {ambiguous_parent_a, [], [{group, ambiguous}]},
     {ambiguous_parent_b, [], [{group, ambiguous}]},
     {ambiguous, [], [ambiguous_case]},
     {duplicate_root, [], [{group, duplicate_branch_a}, {group, duplicate_branch_b}]},
     {duplicate_branch_a, [], [{group, duplicate_descendant}]},
     {duplicate_branch_b, [], [{group, duplicate_descendant}]},
     {duplicate_descendant, [], [ambiguous_case]},
     {guard_parent, [], [guard_pass, {group, skipped_group}]},
     {skipped_group, [], [{group, skipped_child}]},
     {skipped_child, [], [skipped_case]},
     {excluded_group, [], [excluded_case]}].

init_per_group(parent, Config) -> [{parent, true} | Config];
init_per_group(target, Config) ->
    true = proplists:get_value(parent, Config),
    [{target, true} | Config];
init_per_group(child, Config) ->
    true = proplists:get_value(target, Config),
    [{child, true} | Config];
init_per_group(skipped_group, _) -> {skip, "audited fixture condition"};
init_per_group(_, Config) -> Config.

end_per_group(_, Config) -> Config.

top_case(_) -> ok.
nested_case(Config) ->
    true = proplists:get_value(parent, Config),
    true = proplists:get_value(target, Config),
    ok.
descendant_case(Config) ->
    true = proplists:get_value(parent, Config),
    true = proplists:get_value(target, Config),
    true = proplists:get_value(child, Config),
    ok.
sibling_case(_) -> erlang:error(unselected_sibling_executed).
ambiguous_case(_) -> ok.
guard_pass(_) -> ok.
skipped_case(_) -> erlang:error(skipped_group_case_executed).
excluded_case(_) -> erlang:error(excluded_case_executed).
