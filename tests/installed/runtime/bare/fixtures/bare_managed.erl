%% SPDX-License-Identifier: Apache-2.0
-module(bare_managed).
-inspection_generation(?GENERATION).
-export([generation/0, literal/0, closure/0, start_busy/0, start_stack/0,
         start_counter/0, counter/1, counter_switch/2]).
-if(?GENERATION == 2).
-export([version_two_only/0]).
-endif.

generation() -> ?GENERATION.
literal() -> {managed_literal, ?GENERATION, <<"retained module literal">>, [11,22,33]}.
closure() -> fun() -> ?GENERATION end.

start_busy() ->
    Parent = self(),
    Pid = erlang:spawn(fun() ->
        true = register(managed_worker, self()),
        Parent ! ready,
        busy()
    end),
    receive ready -> Pid end,
    ok.

busy() -> receive stop -> ok; _ -> busy() end.

start_counter() ->
    Parent = self(),
    {Pid, Ref} = erlang:spawn_monitor(fun() ->
        true = register(managed_worker, self()),
        Parent ! {counter_ready, self()},
        counter(0)
    end),
    receive {counter_ready, Pid} -> true = demonitor(Ref, [flush]), ok end.

counter(State) ->
    receive
        {increment, From} ->
            Next = State + 1,
            From ! {counter_state, ?GENERATION, Next},
            counter(Next);
        {switch, From} ->
            %% The external tail call leaves no old-code return continuation.
            ?MODULE:counter_switch(State, From);
        stop -> ok
    end.

counter_switch(State, From) ->
    From ! {counter_state, ?GENERATION, State},
    counter(State).

start_stack() ->
    Parent = self(),
    erlang:spawn(fun() -> stack(Parent) end),
    receive ready -> ok end.

stack(Parent) ->
    %% The call into another module retains a continuation in this module.
    {bare_management:hold(Parent), generation()}.

-if(?GENERATION == 2).
version_two_only() -> version_two.
-endif.
