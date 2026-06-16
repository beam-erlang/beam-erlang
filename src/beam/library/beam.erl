%% SPDX-License-Identifier: Apache-2.0
-module(beam).
-export([self/0, spawn/1, spawn_monitor/1, send/2, monitor/1, demonitor/1,
         link/1, unlink/1, exit/1, exit/2, error/1, raise/3]).

-spec self() -> pid().
self() ->
    erlang:self().

-spec spawn(fun(() -> term())) -> pid().
spawn(Fun) when is_function(Fun, 0) ->
    erlang:spawn(Fun);
spawn(Fun) ->
    erlang:error(badarg, [Fun]).

-spec spawn_monitor(fun(() -> term())) -> {pid(), reference()}.
spawn_monitor(Fun) when is_function(Fun, 0) ->
    erlang:spawn_monitor(Fun);
spawn_monitor(Fun) ->
    erlang:error(badarg, [Fun]).

-spec send(pid(), term()) -> ok.
send(Pid, Message) when is_pid(Pid) ->
    erlang:send(Pid, Message),
    ok;
send(Pid, Message) ->
    erlang:error(badarg, [Pid, Message]).

-spec monitor(pid()) -> reference().
monitor(Pid) when is_pid(Pid) ->
    erlang:monitor(process, Pid);
monitor(Pid) ->
    erlang:error(badarg, [Pid]).

-spec demonitor(reference()) -> ok.
demonitor(Ref) when is_reference(Ref) ->
    true = erlang:demonitor(Ref, [flush]),
    ok;
demonitor(Ref) ->
    erlang:error(badarg, [Ref]).

-spec link(pid()) -> ok.
link(Pid) when is_pid(Pid) ->
    erlang:link(Pid),
    ok;
link(Pid) ->
    erlang:error(badarg, [Pid]).

-spec unlink(pid()) -> ok.
unlink(Pid) when is_pid(Pid) ->
    erlang:unlink(Pid),
    ok;
unlink(Pid) ->
    erlang:error(badarg, [Pid]).

-spec exit(term()) -> no_return().
exit(Reason) ->
    erlang:exit(Reason).

-spec exit(pid(), term()) -> ok.
exit(Pid, Reason) when is_pid(Pid) ->
    erlang:exit(Pid, Reason),
    ok;
exit(Pid, Reason) ->
    erlang:error(badarg, [Pid, Reason]).

-spec error(term()) -> no_return().
error(Reason) ->
    erlang:error(Reason).

-spec raise(error | exit | throw, term(), [term()]) -> no_return().
raise(Class, Reason, Stack)
  when Class =:= error; Class =:= exit; Class =:= throw, is_list(Stack) ->
    erlang:raise(Class, Reason, Stack);
raise(Class, Reason, Stack) ->
    erlang:error(badarg, [Class, Reason, Stack]).
