%% SPDX-License-Identifier: Apache-2.0
-module(bare_fixture).
-export([answer/0, add/2, echo/1, caught/0, raise_error/0, raise_throw/0,
         raise_exit/0, allocate/1, spawn_message/0, external_exit/0,
         leave_children/0, child/0, halt_vm/1, background_halt/0,
         bad_halts/0, native_services/0, composition/0, cross_module/0,
         child_error/0, max_heap_exit/0, leave_cleanup/0, shared_data/0]).

answer() -> 42.
add(A, B) -> A + B.
echo(Value) -> Value.
caught() -> try erlang:error(test_error) catch error:test_error -> caught end.
raise_error() -> erlang:error(test_error).
raise_throw() -> throw(test_throw).
raise_exit() -> exit(test_exit).

allocate(N) ->
    Values = build(N, []),
    erlang:garbage_collect(),
    length(Values).
build(0, Acc) -> Acc;
build(N, Acc) -> build(N - 1, [{N, <<N:64>>, N * N} | Acc]).

spawn_message() ->
    Parent = self(),
    spawn(fun() -> Parent ! {self(), 42} end),
    receive {_Child, Value} -> Value end.

external_exit() ->
    Parent = self(),
    spawn(fun() -> exit(Parent, kill) end),
    receive never -> never end.

max_heap_exit() ->
    %% Keep enough live data to run the final collection on a dirty scheduler.
    Values = build(100000, []),
    process_flag(max_heap_size, #{size => 1000, kill => true, error_logger => false}),
    erlang:garbage_collect(),
    length(Values).

leave_children() ->
    Pid = spawn(?MODULE, child, []),
    erlang:send_after(60000, Pid, tick),
    Pid.
child() -> receive stop -> ok; _ -> child() end.

leave_cleanup() ->
    Parent = self(),
    spawn(fun() ->
        Target = spawn_link(?MODULE, child, []),
        cleanup_monitors(50000, Target),
        cleanup_links(2048),
        Parent ! cleanup_ready,
        child()
    end),
    receive cleanup_ready -> ok end.

%% Link and monitor cleanup can yield after the process leaves the process table.
cleanup_monitors(0, _Target) -> ok;
cleanup_monitors(N, Target) ->
    erlang:monitor(process, Target),
    cleanup_monitors(N - 1, Target).
cleanup_links(0) -> ok;
cleanup_links(N) ->
    spawn_link(?MODULE, child, []),
    cleanup_links(N - 1).

halt_vm(Status) -> erlang:halt(Status).
background_halt() ->
    spawn(fun() -> receive after 25 -> erlang:halt(19) end end),
    ok.

bad_halts() ->
    error = rejected(fun() -> erlang:halt(abort) end),
    error = rejected(fun() -> erlang:halt("dump") end),
    error = rejected(fun() -> erlang:halt(0, [{flush, false}]) end),
    error = rejected(fun() -> erlang:halt(0, [{flush_timeout, 1}]) end),
    ok.
rejected(Fun) -> try Fun() of _ -> unexpected catch error:badarg -> error end.

native_services() ->
    notsup = unsupported(fun() -> erlang:display(bare_output) end),
    notsup = unsupported(fun() -> erlang:display_string("bare_output") end),
    notsup = unsupported(fun() -> erlang:process_display(self(), backtrace) end),
    notsup = unsupported(fun() -> open_port({spawn, "true"}, []) end),
    notsup = unsupported(fun() -> erlang:setnode('bare@localhost', 0) end),
    {error, _} = erlang:load_nif("missing", 0),
    ok.
unsupported(Fun) -> try Fun() of _ -> unexpected catch error:notsup -> notsup end.

composition() ->
    undefined = whereis(init),
    undefined = whereis(code_server),
    undefined = whereis(application_controller),
    false = erlang:module_loaded(erl_init),
    false = erlang:module_loaded(init),
    false = erlang:module_loaded(erl_prim_loader),
    false = erlang:module_loaded(io),
    true = erlang:module_loaded(atomics),
    true = erlang:module_loaded(counters),
    true = erlang:module_loaded(persistent_term),
    9 = length(erlang:pre_loaded()),
    ok.

shared_data() ->
    Atomics = atomics:new(2, [{signed, true}]),
    ok = atomics:put(Atomics, 1, 10),
    ok = atomics:sub(Atomics, 1, 3),
    5 = atomics:sub_get(Atomics, 1, 2),
    ok = atomics:compare_exchange(Atomics, 1, 5, -4),
    -4 = atomics:exchange(Atomics, 1, 7),
    #{size := 2} = atomics:info(Atomics),
    error = rejected(fun() -> atomics:new(1, [invalid]) end),
    badarith = try atomics:sub(Atomics, 1, invalid) of
        _ -> unexpected
    catch error:Reason -> Reason end,
    ok = counter_modes([[], [atomics], [write_concurrency]]),
    Key = {?MODULE, shared_data},
    absent = persistent_term:get(Key, absent),
    ok = persistent_term:put_new(Key, Atomics),
    ok = persistent_term:put_new(Key, Atomics),
    error = rejected(fun() -> persistent_term:put_new(Key, ignored) end),
    Parent = self(),
    {Pid, Monitor} = spawn_monitor(fun() ->
        Ref = persistent_term:get(Key),
        8 = atomics:add_get(Ref, 1, 1),
        Parent ! {self(), updated}
    end),
    receive {Pid, updated} -> ok end,
    receive {'DOWN', Monitor, process, Pid, normal} -> ok end,
    8 = atomics:get(Atomics, 1),
    true = persistent_term:erase(Key),
    absent = persistent_term:get(Key, absent),
    ok.

counter_modes([]) -> ok;
counter_modes([Options | Rest]) ->
    Counter = counters:new(2, Options),
    ok = counters:put(Counter, 1, 10),
    ok = counters:sub(Counter, 1, 4),
    6 = counters:get(Counter, 1),
    Parent = self(),
    {Pid, Monitor} = spawn_monitor(fun() ->
        ok = counters:add(Counter, 1, 3),
        Parent ! {self(), updated}
    end),
    receive {Pid, updated} -> ok end,
    receive {'DOWN', Monitor, process, Pid, normal} -> ok end,
    9 = counters:get(Counter, 1),
    #{size := 2} = counters:info(Counter),
    error = rejected(fun() -> counters:get(Counter, 0) end),
    error = rejected(fun() -> counters:new(1, [invalid]) end),
    counter_modes(Rest).

cross_module() -> bare_second:answer().

child_error() ->
    %% Exercise failure delivery after an application selects a missing logger.
    erlang:system_flag(system_logger, logger),
    {Pid, Ref} = spawn_monitor(fun() -> erlang:error(background_error) end),
    receive {'DOWN', Ref, process, Pid, _Reason} -> ok end.
