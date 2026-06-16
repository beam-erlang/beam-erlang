%% SPDX-License-Identifier: Apache-2.0
-module(bare_prim_eval).
-export([exercise/0, absent/0]).

absent() ->
    false = erlang:module_loaded(prim_eval),
    false = member(prim_eval, erlang:pre_loaded()),
    42.

exercise() ->
    true = erlang:module_loaded(prim_eval),
    false = member(prim_eval, erlang:pre_loaded()),
    self() ! preserved_message,
    self() ! selected_message,
    %% The matcher must not allocate or retain the inspected message: prim_eval
    %% invokes it while scanning the mailbox with garbage collection disabled.
    matched = prim_eval:'receive'(fun(selected_message) -> matched;
                                    (_) -> nomatch
                                 end, 1000),
    preserved = receive preserved_message -> preserved
                after 0 -> missing_preserved_message end,
    timeout = prim_eval:'receive'(fun(_) -> nomatch end, 1),
    42.

member(_, []) -> false;
member(Value, [Value | _]) -> true;
member(Value, [_ | Rest]) -> member(Value, Rest).
