%% SPDX-License-Identifier: Apache-2.0
-module(bare_managed).
-export([generation/0, literal/0, closure/0, start_busy/0, start_stack/0]).

generation() -> ?GENERATION.
literal() -> {managed_literal, ?GENERATION, <<"retained module literal">>, [11,22,33]}.
closure() -> fun() -> ?GENERATION end.

start_busy() ->
    Parent = self(),
    Pid = spawn(fun() ->
        true = register(managed_worker, self()),
        Parent ! ready,
        busy()
    end),
    receive ready -> Pid end,
    ok.

busy() -> receive stop -> ok; _ -> busy() end.

start_stack() ->
    Parent = self(),
    spawn(fun() -> stack(Parent) end),
    receive ready -> ok end.

stack(Parent) ->
    %% The call into another module retains a continuation in this module.
    {bare_management:hold(Parent), generation()}.
