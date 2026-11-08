%% SPDX-License-Identifier: Apache-2.0
-module(bare_scheduler_stress).
-export([start/0, stop/0, check/0]).

start() ->
    Parent = self(),
    erlang:spawn(fun() -> coordinator(Parent) end),
    receive ready -> ok end.

coordinator(Parent) ->
    true = register(scheduler_changers, self()),
    Owner = self(),
    Workers = [erlang:spawn_monitor(fun() -> normal_start(Owner) end),
               erlang:spawn_monitor(fun() -> dirty_start(Owner) end),
               erlang:spawn_monitor(fun() -> dirty_start(Owner) end)],
    ready(3),
    Parent ! ready,
    receive {stop, Caller} ->
        %% Termination also exercises withdrawal of queued online changes.
        [exit(Pid, kill) || {Pid, _} <- Workers],
        down(Workers),
        Caller ! stopped
    end.

ready(0) -> ok;
ready(N) -> receive ready -> ready(N - 1) end.

down([]) -> ok;
down([{Pid, Ref} | Rest]) ->
    receive {'DOWN', Ref, process, Pid, killed} -> down(Rest) end.

normal_start(Owner) ->
    beam:system_flag(schedulers_online, 4),
    Owner ! ready,
    normal_changes().

normal_changes() ->
    beam:system_flag(schedulers_online, 8),
    beam:system_flag(schedulers_online, 4),
    normal_changes().

dirty_start(Owner) ->
    beam:system_flag(dirty_cpu_schedulers_online, 1),
    Owner ! ready,
    dirty_changes().

dirty_changes() ->
    beam:system_flag(dirty_cpu_schedulers_online, 2),
    beam:system_flag(dirty_cpu_schedulers_online, 1),
    dirty_changes().

stop() ->
    Pid = whereis(scheduler_changers),
    Ref = monitor(process, Pid),
    Pid ! {stop, self()},
    receive stopped -> ok end,
    receive {'DOWN', Ref, process, Pid, normal} -> ok end.

check() ->
    4 = beam:system_info(schedulers_online),
    3 = beam:system_info(dirty_cpu_schedulers_online),
    ok.
