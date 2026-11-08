%% SPDX-License-Identifier: Apache-2.0
-module(bare_lifecycle).
-export([generation/0, configuration/11, allocator_tags/1, atom_absent/1, leave_state/1,
         callback/0, halt/0, identities/0, old_identities/1, local_arguments/0,
         background_halt/0, pending/0, byte_patterns/0, start_enif_receiver/0,
         await_enif_message/0]).

byte_patterns() ->
    [] = [M || M <- beam:pre_loaded(), M =:= binary orelse M =:= beam_binary],
    false = beam:module_loaded(binary),
    ok = shared_pattern(),
    true = erlang:garbage_collect(),
    ok = abandoned_search(),
    true = erlang:garbage_collect(),
    Retained = beam_binary:compile_pattern([<<"left-live">>, <<"second-pattern">>]),
    beam_persistent_term:put(byte_pattern, Retained),
    Parent = self(),
    Owner = erlang:spawn(fun() ->
        {0, 9} = beam_binary:match(<<"left-live">>, Retained),
        Parent ! {pattern_retained, self()},
        receive stop -> ok end
    end),
    receive {pattern_retained, Owner} -> ok
    after 5000 -> error(pattern_retention_timeout)
    end.

shared_pattern() ->
    Parent = self(),
    Pattern = beam_binary:compile_pattern([<<"aba">>, <<"ababa">>]),
    {Pid, Monitor} = erlang:spawn_monitor(fun() ->
        receive
            {Parent, Shared} ->
                {2, 5} = beam_binary:match(<<"xxababayy">>, Shared),
                true = erlang:garbage_collect(),
                {0, 3} = beam_binary:match(<<"aba">>, Shared),
                Parent ! {pattern_ok, self()}
        end
    end),
    Pid ! {Parent, Pattern},
    receive {pattern_ok, Pid} -> ok after 5000 -> error(pattern_share_timeout) end,
    receive {'DOWN', Monitor, process, Pid, normal} -> ok
    after 5000 -> error(pattern_owner_timeout)
    end.

abandoned_search() ->
    Parent = self(),
    Subject = beam_binary:copy(<<0>>, 16 * 1024 * 1024),
    Pattern = beam_binary:compile_pattern([<<0>>, <<0, 0>>]),
    {Pid, Monitor} = erlang:spawn_monitor(fun() ->
        Parent ! {search_started, self()},
        Matches = beam_binary:matches(Subject, Pattern),
        Parent ! {search_completed, self(), length(Matches)},
        receive stop -> ok end
    end),
    receive {search_started, Pid} -> ok after 5000 -> error(search_start_timeout) end,
    ok = search_active(Pid, 100),
    true = exit(Pid, kill),
    receive {'DOWN', Monitor, process, Pid, killed} -> ok
    after 5000 -> error(search_stop_timeout)
    end,
    receive {search_completed, Pid, _} -> error(search_completed_before_abandon)
    after 0 -> ok
    end.

search_active(_, 0) -> error(search_never_became_active);
search_active(Pid, Attempts) ->
    receive {search_completed, Pid, _} -> error(search_completed_before_abandon)
    after 0 -> ok
    end,
    case process_info(Pid, [current_function, reductions]) of
        [{current_function, {Module, Function, _}}, {reductions, Reductions}]
          when Reductions >= 1000,
               (Module =:= erlang andalso Function =:= binary_find_trap) orelse
               (Module =:= beam_binary andalso Function =:= matches) ->
            ok;
        undefined -> error(search_owner_disappeared);
        _ ->
            receive after 1 -> ok end,
            search_active(Pid, Attempts - 1)
    end.

generation() -> ?GENERATION.

allocator_tags(Mode) ->
    ok = allocator_tags([binary_alloc, std_alloc, ets_alloc, eheap_alloc,
                         ll_alloc, driver_alloc, fix_alloc, sl_alloc], Mode),
    ok = allocator_instances(beam:system_info({allocator, temp_alloc}), false),
    ok = allocator_instances(beam:system_info({allocator, literal_alloc}), false),
    false = beam:system_info({allocator, test_alloc}),
    ok.

allocator_tags([], _) -> ok;
allocator_tags([Allocator | Rest], Mode) ->
    Expected = case Mode of
                   0 -> Allocator =:= binary_alloc orelse Allocator =:= driver_alloc;
                   1 -> true;
                   2 -> false
               end,
    ok = allocator_instances(beam:system_info({allocator, Allocator}), Expected),
    allocator_tags(Rest, Mode).

allocator_instances([], _) -> ok;
allocator_instances([{instance, _, Info} | Rest], Expected) ->
    Expected = property(atags, property(options, Info)),
    allocator_instances(Rest, Expected).

property(Key, [{Key, Value} | _]) -> Value;
property(Key, [_ | Rest]) -> property(Key, Rest).

configuration(S, SO, D, DO, DI, A, P, Atoms, MinHeap, MinBinHeap, MaxHeap) ->
    S = beam:system_info(schedulers),
    SO = beam:system_info(schedulers_online),
    D = beam:system_info(dirty_cpu_schedulers),
    DO = beam:system_info(dirty_cpu_schedulers_online),
    DI = beam:system_info(dirty_io_schedulers),
    A = beam:system_info(thread_pool_size),
    P = beam:system_info(process_limit),
    Atoms = beam:system_info(atom_limit),
    {min_heap_size, MinHeap} = process_info(self(), min_heap_size),
    {min_bin_vheap_size, MinBinHeap} = process_info(self(), min_bin_vheap_size),
    {max_heap_size, #{size := MaxHeap}} = process_info(self(), max_heap_size),
    false = beam:system_flag(microstate_accounting, true),
    Stats = beam:statistics(microstate_accounting),
    S = length([M || #{type := scheduler} = M <- Stats]),
    D = length([M || #{type := dirty_cpu_scheduler} = M <- Stats]),
    DI = length([M || #{type := dirty_io_scheduler} = M <- Stats]),
    A = length([M || #{type := async} = M <- Stats]),
    ok = unique_microstates(Stats, #{}),
    true = beam:system_flag(microstate_accounting, false),
    %% Leave accounting enabled to exercise termination of active records.
    false = beam:system_flag(microstate_accounting, true),
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
    undefined = beam_ets:info(lifecycle_table),
    absent = beam_persistent_term:get(lifecycle_value, absent),
    absent = beam_persistent_term:get(lifecycle_trace, absent),
    _ = binary_to_atom(Name, utf8),
    Atomics = beam_atomics:new(3, []),
    Counter = beam_counters:new(3, [write_concurrency]),
    ok = beam_atomics:put(Atomics, 1, ?GENERATION),
    ok = beam_counters:add(Counter, 1, ?GENERATION),
    %% Keep native arrays alive through VM-wide roots after this call exits.
    Value = {Name, <<(beam:unique_integer([positive])):64, 0:524288>>,
             make_ref(), Atomics, Counter},
    ok = beam_persistent_term:put(lifecycle_value, Value),
    ok = beam_persistent_term:put(lifecycle_value, {replaced, Value}),
    Parent = self(),
    Pid = erlang:spawn(fun() ->
        lifecycle_table = beam_ets:new(lifecycle_table, [named_table, public]),
        true = beam_ets:insert(lifecycle_table, {value, Value}),
        Parent ! ready,
        counter()
    end),
    true = register(lifecycle_counter, Pid),
    receive ready -> ok end,
    %% Retain a native session and breakpoint without requiring a tracer NIF.
    Session = beam_trace:trace_session_create(lifecycle_trace, undefined, []),
    1 = beam_trace:trace_pattern(Session, {?MODULE, generation, 0}, true, [local]),
    ok = beam_persistent_term:put(lifecycle_trace, Session),
    _ = beam_time:send_after(60000, lifecycle_counter, {Value, fun counter/0, Session}),
    ok.

counter() -> receive _ -> counter() end.

callback() ->
    {ok, 42} = beam_host:request(ping),
    ok.

start_enif_receiver() ->
    Receiver = erlang:spawn(fun enif_receiver/0),
    true = register(lifecycle_enif_receiver, Receiver),
    ok.

enif_receiver() ->
    %% Check the payload after the host has cleared and freed its environment.
    receive
        {await_embedding_message, Caller} ->
            receive
                {embedding_message, [17, 42]} -> Caller ! {embedding_ack, self()}
            after 5000 -> error(embedding_message_timeout)
            end
    after 5000 -> error(embedding_receiver_timeout)
    end.

await_enif_message() ->
    Receiver = whereis(lifecycle_enif_receiver),
    true = is_pid(Receiver),
    Receiver ! {await_embedding_message, self()},
    receive {embedding_ack, Receiver} -> ok
    after 5000 -> error(embedding_ack_timeout)
    end.

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
    erlang:spawn(fun() -> receive after 25 -> erlang:halt(19) end end),
    ok.

pending() ->
    erlang:spawn(fun() -> receive after 25 -> beam_host:request(pending) end end),
    ok.
