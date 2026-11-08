%% SPDX-License-Identifier: Apache-2.0
-module(bare_management).
-export([stop_worker/0, hold/1, retain/0, retained/0, release/0,
         gzip_checks/1, gzip_invalid/1, gzip_size/1, gzip_interrupt/1, native_load/1, pending_on_load/1, counts/0, set_counts/2,
         block/0, block_normal/0, unblock/0, atom_count/0,
         start_changes/0, stop_changes/0, background_halt/0,
         start_dirty/1, stop_dirty/0, start_purge/0, finish_purge/0,
         counter_step/0, counter_switch/0, unauthorized_purge/0,
         gate_reserved_kill/0, gate_spoof/0, gate_fairness/0,
         gate_active_kill/0, gate_active_halt/0,
         finish_gate_control/0, start_code_changes/2,
         code_change_progress/0, stop_code_changes/0,
         process_boot/2, process_probe/1, process_rename/2, process_stop/1,
         process_default_leader/0, process_return/0, process_wait_absent/1,
         process_churn_start/0, process_churn_progress/0, process_churn_stop/0]).

%% Independent public process-API fixtures. The native host creates process_boot
%% directly; entry calls only provide bounded application acknowledgements.
process_boot(Name, Value) ->
    process_registration(Name),
    process_loop(Name, Value).

process_registration(undefined) -> ok;
process_registration(Name) -> true = register(Name, self()), ok.

process_loop(Name, Value) ->
    receive
        {native_process_request, From, Ref, probe} ->
            From ! {Ref, {Value, group_leader()}},
            process_loop(Name, Value);
        {native_process_request, From, Ref, {rename, NewName}} ->
            case Name of undefined -> ok; _ -> true = unregister(Name) end,
            process_registration(NewName),
            From ! {Ref, ok},
            process_loop(NewName, Value);
        native_process_stop -> ok
    after 5000 -> error(native_process_idle_timeout)
    end.

process_probe(Pid) -> process_request(Pid, probe).
process_rename(Pid, Name) -> process_request(Pid, {rename, Name}).
process_request(Pid, Operation) ->
    Ref = monitor(process, Pid),
    Pid ! {native_process_request, self(), Ref, Operation},
    receive
        {Ref, Reply} -> demonitor(Ref, [flush]), Reply;
        {'DOWN', Ref, process, Pid, Reason} -> error({native_process_failed, Reason})
    after 5000 ->
        demonitor(Ref, [flush]), error(native_process_request_timeout)
    end.

process_stop(Pid) ->
    Ref = monitor(process, Pid),
    Pid ! native_process_stop,
    receive
        {'DOWN', Ref, process, Pid, normal} -> ok;
        {'DOWN', Ref, process, Pid, Reason} -> error({native_process_stop_failed, Reason})
    after 5000 ->
        demonitor(Ref, [flush]), error(native_process_stop_timeout)
    end.

process_default_leader() -> group_leader().
process_return() -> ok.
process_wait_absent(Pid) ->
    %% Publication can precede immediate exit or undef. A monitor established
    %% afterward guarantees eventual absence, not recovery of an earlier reason.
    Ref = monitor(process, Pid),
    receive {'DOWN', Ref, process, Pid, _Reason} -> ok
    after 5000 -> demonitor(Ref, [flush]), error(native_process_exit_timeout)
    end.

process_churn_start() ->
    Parent = self(),
    {Pid, Ref} = erlang:spawn_monitor(fun() ->
        true = register(native_process_churn, self()),
        Parent ! {native_churn_ready, self()},
        process_churn(0)
    end),
    receive
        {native_churn_ready, Pid} -> demonitor(Ref, [flush]), ok;
        {'DOWN', Ref, process, Pid, Reason} -> error({native_churn_failed, Reason})
    after 5000 -> error(native_churn_start_timeout)
    end.

process_churn(Count) ->
    receive
        {native_churn_progress, From, Ref} ->
            From ! {Ref, Count}, process_churn(Count);
        native_process_stop -> ok
    after 0 ->
        {Pid, Ref} = erlang:spawn_monitor(fun() -> ok end),
        receive {'DOWN', Ref, process, Pid, normal} -> process_churn(Count + 1)
        after 5000 -> error(native_churn_child_timeout)
        end
    end.

process_churn_progress() ->
    Pid = whereis(native_process_churn),
    Ref = monitor(process, Pid),
    Pid ! {native_churn_progress, self(), Ref},
    receive
        {Ref, Count} -> demonitor(Ref, [flush]), Count;
        {'DOWN', Ref, process, Pid, Reason} -> error({native_churn_failed, Reason})
    after 5000 -> demonitor(Ref, [flush]), error(native_churn_progress_timeout)
    end.

process_churn_stop() -> process_stop(whereis(native_process_churn)).

start_code_changes(A, B) when is_binary(A), is_binary(B) ->
    Parent = self(),
    {Pid, Ref} = erlang:spawn_monitor(fun() ->
        true = register(managed_loader, self()),
        Parent ! {loader_ready, self()},
        code_changes(A, B, 0)
    end),
    receive
        {loader_ready, Pid} -> true = demonitor(Ref, [flush]), ok;
        {'DOWN', Ref, process, Pid, Reason} -> error({loader_failed, Reason})
    end.

code_changes(Next, Other, Count) ->
    receive
        {progress, From, Ref} ->
            From ! {loader_progress, Ref, Count},
            code_changes(Next, Other, Count);
        {stop, From, Ref} ->
            From ! {loader_progress, Ref, Count},
            ok
    after 0 ->
        true = erts_code_purger:soft_purge(bare_managed),
        {module, bare_managed} = erlang:load_module(bare_managed, Next),
        code_changes(Other, Next, Count + 1)
    end.

code_change_progress() -> loader_request(progress).
stop_code_changes() -> loader_request(stop).
loader_request(Operation) ->
    Pid = whereis(managed_loader),
    Mon = monitor(process, Pid),
    Ref = make_ref(),
    Pid ! {Operation, self(), Ref},
    Count = receive
        {loader_progress, Ref, N} -> N;
        {'DOWN', Mon, process, Pid, Reason} -> error({loader_failed, Reason})
    end,
    case Operation of
        stop -> receive {'DOWN', Mon, process, Pid, normal} -> Count end;
        progress -> true = demonitor(Mon, [flush]), Count
    end.

unauthorized_purge() ->
    try beam_internal:module_soft_purge(bare_managed) of
        _ -> error(unauthorized_purge_accepted)
    catch error:notsup -> ok end,
    try erts_internal:purge_module(bare_managed, beam_host_admit) of
        _ -> error(unauthorized_gate_accepted)
    catch error:notsup -> ok end,
    ok.

gate_reserved_kill() -> start_gate_control(kill).
gate_spoof() -> start_gate_control(spoof).
gate_fairness() -> start_gate_control(fairness).
gate_active_kill() -> start_active_gate_control(kill).
gate_active_halt() -> start_active_gate_control(halt).

start_gate_control(Mode) ->
    Parent = self(),
    Purger = whereis(erts_code_purger),
    {Pid, Ref} = erlang:spawn_monitor(fun() -> gate_control(Mode, Purger, Parent) end),
    receive
        {gate_ready, Pid} -> true = demonitor(Ref, [flush]), ok;
        {'DOWN', Ref, process, Pid, Reason} -> error({gate_start_failed, Reason})
    end.

gate_control(Mode, Purger, Parent) ->
    true = register(managed_gate, self()),
    %% A returning entry must not own the suspension: process exit would
    %% automatically release it before the C host starts its next operation.
    true = erlang:suspend_process(Purger),
    TestRef = make_ref(),
    %% Queue this ordinary request before the later host request/wakeup. It
    %% must remain selected after the reserved host operation is serviced.
    case Mode of
        fairness -> Purger ! {test_purge, bare_managed, self(), false, TestRef};
        _ -> ok
    end,
    Parent ! {gate_ready, self()},
    Result = try
        Helper = await_purge_helper(beam_time:monotonic_time(millisecond) + 10000),
        case Mode of
            kill ->
                Mon = monitor(process, Helper),
                exit(Helper, kill),
                receive {'DOWN', Mon, process, Helper, killed} -> ok end;
            spoof ->
                Helper ! beam_host_purge,
                await_empty_helper(Helper, beam_time:monotonic_time(millisecond) + 10000);
            fairness -> ok
        end,
        ok
    catch Class:Reason -> {gate_failed, Class, Reason}
    after
        true = beam:resume_process(Purger)
    end,
    case Mode of
        fairness -> receive {started, TestRef} -> ok end;
        _ -> ok
    end,
    receive
        {finish, From} ->
            case Mode of
                fairness ->
                    Purger ! {continue, TestRef},
                    receive {continued, TestRef} -> ok end,
                    Purger ! {complete, TestRef},
                    receive {test_purge, true, TestRef} -> ok end;
                _ -> ok
            end,
            From ! {gate_control_result, self(), Result}
    end.

await_purge_helper(Deadline) ->
    case purge_helper(beam:processes(), whereis(managed_dirty)) of
        undefined ->
            true = beam_time:monotonic_time(millisecond) < Deadline,
            beam:yield(),
            await_purge_helper(Deadline);
        Pid -> Pid
    end.

purge_helper([], _) -> undefined;
purge_helper([Skip|Rest], Skip) -> purge_helper(Rest, Skip);
purge_helper([Pid|Rest], Skip) ->
    case beam:process_info(Pid, current_function) of
        {current_function, {beam_internal, module_soft_purge_wait, 1}} -> Pid;
        _ -> purge_helper(Rest, Skip)
    end.

await_empty_helper(Pid, Deadline) ->
    case {beam:process_info(Pid, status), beam:process_info(Pid, message_queue_len)} of
        {{status, waiting}, {message_queue_len, 0}} -> ok;
        {undefined, _} -> error(spoof_completed_request);
        _ ->
            true = beam_time:monotonic_time(millisecond) < Deadline,
            beam:yield(),
            await_empty_helper(Pid, Deadline)
    end.

finish_gate_control() ->
    Pid = whereis(managed_gate),
    Ref = monitor(process, Pid),
    Pid ! {finish, self()},
    receive {gate_control_result, Pid, Result} -> ok = Result end,
    receive {'DOWN', Ref, process, Pid, normal} -> ok end.

start_active_gate_control(Mode) ->
    Parent = self(),
    {Pid, Ref} = erlang:spawn_monitor(fun() -> active_gate_control(Mode, Parent) end),
    receive
        {gate_ready, Pid} -> true = demonitor(Ref, [flush]), ok;
        {'DOWN', Ref, process, Pid, Reason} -> error({gate_start_failed, Reason})
    end.

active_gate_control(Mode, Parent) ->
    true = register(managed_gate, self()),
    Handlers = dirty_handlers(beam:processes()),
    [_,_,_] = Handlers,
    suspend_handlers(Handlers),
    Parent ! {gate_ready, self()},
    Result = try
        Deadline = beam_time:monotonic_time(millisecond) + 10000,
        Helper = await_purge_helper(Deadline),
        await_active_purge(whereis(erts_code_purger), Deadline),
        case Mode of
            kill ->
                Mon = monitor(process, Helper),
                exit(Helper, kill),
                receive {'DOWN', Mon, process, Helper, killed} -> ok end;
            halt ->
                beam:halt(43, [])
        end
    catch Class:Reason -> {gate_failed, Class, Reason}
    after
        %% On halt the VM deletes this controller; its suspension monitors
        %% release their targets as part of normal process teardown.
        resume_handlers(Handlers)
    end,
    receive {finish, From} -> From ! {gate_control_result, self(), Result} end.

dirty_handlers([]) -> [];
dirty_handlers([Pid|Rest]) ->
    case beam:process_info(Pid, initial_call) of
        {initial_call, {erts_dirty_process_signal_handler, start, 0}} ->
            [Pid|dirty_handlers(Rest)];
        _ -> dirty_handlers(Rest)
    end.

suspend_handlers([]) -> ok;
suspend_handlers([Pid|Rest]) ->
    true = erlang:suspend_process(Pid),
    suspend_handlers(Rest).

resume_handlers([]) -> ok;
resume_handlers([Pid|Rest]) ->
    _ = try beam:resume_process(Pid) catch _:_ -> false end,
    resume_handlers(Rest).

await_active_purge(Purger, Deadline) ->
    case beam:process_info(Purger, current_function) of
        {current_function, {erts_code_purger, cpc_receive, 4}} -> ok;
        Info ->
            case beam_time:monotonic_time(millisecond) < Deadline of
                true -> beam:yield(), await_active_purge(Purger, Deadline);
                false -> error({active_purge_not_observed, Info})
            end
    end.

counter_step() -> counter_request(increment).
counter_switch() -> counter_request(switch).
counter_request(Operation) ->
    managed_worker ! {Operation, self()},
    receive {counter_state, Generation, State} -> {Generation, State} end.

start_purge() ->
    Parent = self(),
    {Pid, Ref} = erlang:spawn_monitor(fun() -> controlled_purge(Parent) end),
    receive
        {purge_started, Pid} ->
            true = demonitor(Ref, [flush]),
            ok;
        {'DOWN', Ref, process, Pid, Reason} ->
            error({purge_start_failed, Reason})
    end.

controlled_purge(Parent) ->
    true = register(managed_purge, self()),
    Ref = make_ref(),
    %% The existing runtime test protocol pauses after preparing the old
    %% module's local funs, while the purger owns staging permission.
    erts_code_purger ! {test_purge, bare_managed, self(), false, Ref},
    receive {started, Ref} -> Parent ! {purge_started, self()} end,
    receive
        {finish, From} ->
            erts_code_purger ! {continue, Ref},
            receive {continued, Ref} -> ok end,
            erts_code_purger ! {complete, Ref},
            receive
                {test_purge, Result, Ref} ->
                    From ! {purge_finished, self(), Result}
            end
    end.

finish_purge() ->
    Pid = whereis(managed_purge),
    Ref = monitor(process, Pid),
    Pid ! {finish, self()},
    receive {purge_finished, Pid, true} -> ok end,
    receive {'DOWN', Ref, process, Pid, normal} -> ok end.

start_dirty(Type) ->
    Parent = self(),
    {Pid, Ref} = erlang:spawn_monitor(fun() ->
        true = register(managed_dirty, self()),
        %% The notification is sent by the native operation after it enters
        %% its dirty scheduler. It remains there until explicitly terminated.
        erts_debug:dirty(Type, alive_waitexitingonly, Parent)
    end),
    receive
        {alive, Pid} ->
            true = demonitor(Ref, [flush]),
            ok;
        {'DOWN', Ref, process, Pid, Reason} ->
            error({dirty_start_failed, Reason})
    end.

stop_dirty() ->
    Pid = whereis(managed_dirty),
    Ref = monitor(process, Pid),
    exit(Pid, kill),
    receive {'DOWN', Ref, process, Pid, killed} -> ok end.

hold(Parent) ->
    true = register(managed_worker, self()),
    Parent ! ready,
    receive stop -> ok end.

stop_worker() ->
    Pid = whereis(managed_worker),
    Ref = monitor(process, Pid),
    Pid ! stop,
    receive {'DOWN', Ref, process, Pid, normal} -> ok end.

retain() ->
    Value = bare_managed:literal(),
    Fun = bare_managed:closure(),
    beam_persistent_term:put(managed_literal, Value),
    Parent = self(),
    erlang:spawn(fun() ->
        true = register(managed_keeper, self()),
        managed_table = beam_ets:new(managed_table, [named_table, public]),
        true = beam_ets:insert(managed_table, {value, Value}),
        Parent ! ready,
        keeper(Value, Fun)
    end),
    receive ready -> ok end.

keeper(Value, Fun) ->
    receive
        {check, From} ->
            [{value, Value}] = beam_ets:lookup(managed_table, value),
            Value = beam_persistent_term:get(managed_literal),
            {managed_literal, 1, <<"retained module literal">>, [11,22,33]} = Value,
            Failed = try Fun() of _ -> false catch error:_ -> true end,
            From ! {checked, Failed},
            keeper(Value, Fun);
        stop -> ok
    end.

retained() ->
    managed_keeper ! {check, self()},
    receive {checked, true} -> ok; {checked, false} -> function_still_callable end.

release() ->
    Pid = whereis(managed_keeper),
    Ref = monitor(process, Pid),
    Pid ! stop,
    receive {'DOWN', Ref, process, Pid, normal} -> ok end,
    true = beam_persistent_term:erase(managed_literal),
    ok.

native_load(Binary) ->
    {module, bare_managed} = erlang:load_module(bare_managed, Binary),
    ok.

pending_on_load(Binary) ->
    {error, on_load} = erlang:load_module(bare_on_load, Binary),
    ok.

counts() ->
    {beam:system_info(schedulers), beam:system_info(schedulers_online),
     beam:system_info(dirty_cpu_schedulers),
     beam:system_info(dirty_cpu_schedulers_online),
     beam:system_info(dirty_io_schedulers)}.

set_counts(Normal, Dirty) ->
    beam:system_flag(schedulers_online, Normal),
    beam:system_flag(dirty_cpu_schedulers_online, Dirty),
    ok.

block() -> start_block(block).
block_normal() -> start_block(block_normal).
start_block(How) ->
    Parent = self(),
    erlang:spawn(fun() ->
        true = register(managed_blocker, self()),
        beam:system_flag(multi_scheduling, How),
        Parent ! ready,
        receive stop -> ok end
    end),
    receive ready -> ok end.
unblock() ->
    Pid = whereis(managed_blocker),
    Ref = monitor(process, Pid),
    Pid ! stop,
    receive {'DOWN', Ref, process, Pid, normal} -> ok end,
    ok.

atom_count() -> beam:system_info(atom_count).

start_changes() ->
    Parent = self(),
    erlang:spawn(fun() ->
        true = register(managed_changer, self()),
        Parent ! ready,
        changes(2)
    end),
    receive ready -> ok end.
changes(N) ->
    receive stop -> ok
    after 0 ->
        beam:system_flag(schedulers_online, N),
        beam:system_flag(dirty_cpu_schedulers_online, 1),
        changes(3 - N)
    end.
stop_changes() ->
    Pid = whereis(managed_changer),
    Ref = monitor(process, Pid),
    Pid ! stop,
    receive {'DOWN', Ref, process, Pid, normal} -> ok end.

background_halt() ->
    erlang:spawn(fun() -> receive after 10 -> erlang:halt(31) end end),
    ok.

gzip_checks(<<RawSize:32, GzipSize:32, Raw:RawSize/binary,
              Gzip:GzipSize/binary, Members/binary>>) ->
    false = beam:module_loaded(zlib),
    Raw = erts_internal:beamfile_gunzip(Gzip),
    Raw = erts_internal:beamfile_gunzip(Members),
    true = is_reference(erlang:prepare_loading(bare_gzip, Raw)),
    true = is_reference(erlang:prepare_loading(bare_gzip, Gzip)),
    true = is_reference(erlang:prepare_loading(bare_gzip, Members)),
    true = is_reference(erlang:prepare_loading(bare_gzip, [Gzip])),
    {module, bare_gzip} = erlang:load_module(bare_gzip, Gzip),
    42 = bare_gzip:value(),
    PrefixSize = byte_size(Gzip) - 8,
    <<Prefix:PrefixSize/binary, Crc:8, Rest/binary>> = Gzip,
    BadCrc = <<Prefix/binary, (Crc bxor 1), Rest/binary>>,
    Last = byte_size(Gzip) - 1,
    <<Before:Last/binary, Length:8>> = Gzip,
    BadLength = <<Before/binary, (Length bxor 1)>>,
    gzip_reject([<<>>, <<1,2,3>>, binary_part(Gzip, 0, 5),
                 binary_part(Gzip, 0, byte_size(Gzip)-1), BadCrc,
                 BadLength, <<Gzip/binary, 17>>, <<"not BEAM">>]),
    ok = try erlang:prepare_loading(bare_gzip, not_binary) of
             _ -> expected_badarg
         catch error:badarg -> ok end,
    false = beam:module_loaded(zlib),
    ok.

gzip_invalid(Bad) -> gzip_reject([Bad]).

gzip_reject([]) -> ok;
gzip_reject([Bad|Rest]) ->
    {error, badfile} = erlang:prepare_loading(bare_gzip, Bad),
    42 = bare_gzip:value(),
    gzip_reject(Rest).

gzip_size(<<Size:32, Gzip/binary>>) ->
    Plain = erts_internal:beamfile_gunzip(Gzip),
    Size = byte_size(Plain),
    <<0:Size/unit:8>> = Plain,
    false = beam:module_loaded(zlib),
    ok.

gzip_interrupt(Gzip) ->
    OldDirty = beam:system_info(dirty_cpu_schedulers_online),
    OldSchedulers = beam:system_flag(schedulers_online, 1),
    try
        {Pid, Ref} = erlang:spawn_monitor(fun() ->
            receive go -> ok end,
            _ = erts_internal:beamfile_gunzip(Gzip)
        end),
        Pid ! go,
        Deadline = beam_time:monotonic_time(millisecond) + 10000,
        gzip_wait_trap(Pid, Ref, Deadline)
    after
        beam:system_flag(schedulers_online, OldSchedulers),
        beam:system_flag(dirty_cpu_schedulers_online, OldDirty)
    end.

gzip_wait_trap(Pid, Ref, Deadline) ->
    %% With one scheduler, this enquiry can observe the native continuation
    %% only after it has yielded and handled the enquiry signal.
    case beam:process_info(Pid, current_function) of
        {current_function, {erts_internal, beamfile_gunzip, 1}} ->
            exit(Pid, kill),
            receive {'DOWN', Ref, process, Pid, killed} -> ok end;
        _ ->
            receive
                {'DOWN', Ref, process, Pid, Reason} ->
                    error({gzip_did_not_yield, Reason})
            after 0 ->
                case beam_time:monotonic_time(millisecond) < Deadline of
                    true -> gzip_wait_trap(Pid, Ref, Deadline);
                    false -> exit(Pid, kill), error(gzip_trap_timeout)
                end
            end
    end.
