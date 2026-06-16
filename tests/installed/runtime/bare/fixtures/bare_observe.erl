%% SPDX-License-Identifier: Apache-2.0
-module(bare_observe).
-export([start/0, add/1, snapshot/0, stop/0]).

start() ->
    true = register(bare_counter, spawn(fun() -> counter(0) end)),
    ok.

add(Amount) -> request({add, Amount}).

request(Request) ->
    Pid = whereis(bare_counter),
    Ref = monitor(process, Pid),
    Pid ! {self(), Ref, Request},
    receive
        {Ref, Result} -> demonitor(Ref, [flush]), Result;
        {'DOWN', Ref, process, Pid, Reason} -> error({counter_exited, Reason})
    end.

counter(Value) ->
    receive
        {From, Ref, {add, Amount}} ->
            Next = Value + Amount,
            From ! {Ref, Next},
            counter(Next);
        stop -> ok
    end.

snapshot() ->
    Worker = case whereis(bare_counter) of
        undefined -> absent;
        Pid ->
            case process_info(Pid, [status, memory, message_queue_len, reductions]) of
                undefined -> absent;
                Info -> {present, Info}
            end
    end,
    {Reductions, _SinceLastCall} = erlang:statistics(reductions),
    {snapshot, erlang:system_info(process_count),
     erlang:system_info(schedulers_online), erlang:statistics(run_queue),
     Reductions, erlang:memory(), Worker}.

stop() ->
    case whereis(bare_counter) of
        undefined -> ok;
        Pid ->
            Ref = monitor(process, Pid),
            Pid ! stop,
            receive {'DOWN', Ref, process, Pid, normal} -> ok end
    end.
