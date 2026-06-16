%% SPDX-License-Identifier: Apache-2.0
-module(bare_lifecycle).
-export([generation/0, configuration/11, allocator_tags/1, atom_absent/1, leave_state/1,
         callback/0, halt/0, identities/0, old_identities/1, local_arguments/0,
         background_halt/0, pending/0]).

generation() -> ?GENERATION.

allocator_tags(Mode) ->
    ok = allocator_tags([binary_alloc, std_alloc, ets_alloc, eheap_alloc,
                         ll_alloc, driver_alloc, fix_alloc, sl_alloc], Mode),
    ok = allocator_instances(erlang:system_info({allocator, temp_alloc}), false),
    ok = allocator_instances(erlang:system_info({allocator, literal_alloc}), false),
    false = erlang:system_info({allocator, test_alloc}),
    ok.

allocator_tags([], _) -> ok;
allocator_tags([Allocator | Rest], Mode) ->
    Expected = case Mode of
                   0 -> Allocator =:= binary_alloc orelse Allocator =:= driver_alloc;
                   1 -> true;
                   2 -> false
               end,
    ok = allocator_instances(erlang:system_info({allocator, Allocator}), Expected),
    allocator_tags(Rest, Mode).

allocator_instances([], _) -> ok;
allocator_instances([{instance, _, Info} | Rest], Expected) ->
    Expected = property(atags, property(options, Info)),
    allocator_instances(Rest, Expected).

property(Key, [{Key, Value} | _]) -> Value;
property(Key, [_ | Rest]) -> property(Key, Rest).

configuration(S, SO, D, DO, DI, A, P, Atoms, MinHeap, MinBinHeap, MaxHeap) ->
    S = erlang:system_info(schedulers),
    SO = erlang:system_info(schedulers_online),
    D = erlang:system_info(dirty_cpu_schedulers),
    DO = erlang:system_info(dirty_cpu_schedulers_online),
    DI = erlang:system_info(dirty_io_schedulers),
    A = erlang:system_info(thread_pool_size),
    P = erlang:system_info(process_limit),
    Atoms = erlang:system_info(atom_limit),
    {min_heap_size, MinHeap} = process_info(self(), min_heap_size),
    {min_bin_vheap_size, MinBinHeap} = process_info(self(), min_bin_vheap_size),
    {max_heap_size, #{size := MaxHeap}} = process_info(self(), max_heap_size),
    false = erlang:system_flag(microstate_accounting, true),
    Stats = erlang:statistics(microstate_accounting),
    S = length([M || #{type := scheduler} = M <- Stats]),
    D = length([M || #{type := dirty_cpu_scheduler} = M <- Stats]),
    DI = length([M || #{type := dirty_io_scheduler} = M <- Stats]),
    A = length([M || #{type := async} = M <- Stats]),
    ok = unique_microstates(Stats, #{}),
    true = erlang:system_flag(microstate_accounting, false),
    %% Leave accounting enabled to exercise termination of active records.
    false = erlang:system_flag(microstate_accounting, true),
    ok.

unique_microstates([], _) -> ok;
unique_microstates([#{type := Type, id := Id, counters := Counters} | Rest], Seen)
  when is_atom(Type), is_integer(Id), is_map(Counters) ->
    Key = {Type, Id},
    false = is_map_key(Key, Seen),
    unique_microstates(Rest, Seen#{Key => true}).

atom_absent(Name) ->
    try binary_to_existing_atom(Name, utf8) of _ -> false
    catch error:badarg -> true
    end.

leave_state(Name) ->
    undefined = whereis(lifecycle_counter),
    undefined = ets:info(lifecycle_table),
    absent = persistent_term:get(lifecycle_value, absent),
    absent = persistent_term:get(lifecycle_trace, absent),
    _ = binary_to_atom(Name, utf8),
    Atomics = atomics:new(3, []),
    Counter = counters:new(3, [write_concurrency]),
    ok = atomics:put(Atomics, 1, ?GENERATION),
    ok = counters:add(Counter, 1, ?GENERATION),
    %% Keep native arrays alive through VM-wide roots after this call exits.
    Value = {Name, <<(erlang:unique_integer([positive])):64, 0:524288>>,
             make_ref(), Atomics, Counter},
    ok = persistent_term:put(lifecycle_value, Value),
    ok = persistent_term:put(lifecycle_value, {replaced, Value}),
    Parent = self(),
    Pid = spawn(fun() ->
        lifecycle_table = ets:new(lifecycle_table, [named_table, public]),
        true = ets:insert(lifecycle_table, {value, Value}),
        Parent ! ready,
        counter()
    end),
    true = register(lifecycle_counter, Pid),
    receive ready -> ok end,
    %% Retain a native session and breakpoint without requiring a tracer NIF.
    Session = erts_internal:trace_session_create(lifecycle_trace, undefined, []),
    1 = erts_internal:trace_pattern(Session, {?MODULE, generation, 0}, true, [local]),
    ok = persistent_term:put(lifecycle_trace, Session),
    _ = erlang:send_after(60000, lifecycle_counter, {Value, fun counter/0, Session}),
    ok.

counter() -> receive _ -> counter() end.

callback() ->
    {ok, 42} = beam_host:request(ping),
    ok.

halt() -> erlang:halt(17).

identities() -> {self(), make_ref()}.

old_identities({Pid, Ref}) ->
    true = is_pid(Pid),
    true = is_reference(Ref),
    false = is_process_alive(Pid),
    %% This native operation accepts only identities from the current VM.
    true = rejects_local(fun() -> erts_internal:is_process_alive(Pid, make_ref()) end),
    true = rejects_local(fun() -> erts_internal:is_process_alive(self(), Ref) end),
    ok.

rejects_local(Fun) ->
    try Fun() of _ -> false catch error:badarg -> true end.

local_arguments() -> term_to_binary([self(), make_ref()], [local]).

background_halt() ->
    spawn(fun() -> receive after 25 -> erlang:halt(19) end end),
    ok.

pending() ->
    spawn(fun() -> receive after 25 -> beam_host:request(pending) end end),
    ok.
