%% SPDX-License-Identifier: Apache-2.0
-module(bare_management).
-export([stop_worker/0, hold/1, retain/0, retained/0, release/0,
         gzip_checks/1, gzip_invalid/1, gzip_size/1, gzip_interrupt/1, native_load/1, pending_on_load/1, counts/0, set_counts/2,
         block/0, block_normal/0, unblock/0, atom_count/0,
         start_changes/0, stop_changes/0, background_halt/0,
         start_dirty/1, stop_dirty/0, start_purge/0, finish_purge/0]).

start_purge() ->
    Parent = self(),
    {Pid, Ref} = spawn_monitor(fun() -> controlled_purge(Parent) end),
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
    {Pid, Ref} = spawn_monitor(fun() ->
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
    persistent_term:put(managed_literal, Value),
    Parent = self(),
    spawn(fun() ->
        true = register(managed_keeper, self()),
        managed_table = ets:new(managed_table, [named_table, public]),
        true = ets:insert(managed_table, {value, Value}),
        Parent ! ready,
        keeper(Value, Fun)
    end),
    receive ready -> ok end.

keeper(Value, Fun) ->
    receive
        {check, From} ->
            [{value, Value}] = ets:lookup(managed_table, value),
            Value = persistent_term:get(managed_literal),
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
    true = persistent_term:erase(managed_literal),
    ok.

native_load(Binary) ->
    {module, bare_managed} = erlang:load_module(bare_managed, Binary),
    ok.

pending_on_load(Binary) ->
    {error, on_load} = erlang:load_module(bare_on_load, Binary),
    ok.

counts() ->
    {erlang:system_info(schedulers), erlang:system_info(schedulers_online),
     erlang:system_info(dirty_cpu_schedulers),
     erlang:system_info(dirty_cpu_schedulers_online),
     erlang:system_info(dirty_io_schedulers)}.

set_counts(Normal, Dirty) ->
    erlang:system_flag(schedulers_online, Normal),
    erlang:system_flag(dirty_cpu_schedulers_online, Dirty),
    ok.

block() -> start_block(block).
block_normal() -> start_block(block_normal).
start_block(How) ->
    Parent = self(),
    spawn(fun() ->
        true = register(managed_blocker, self()),
        erlang:system_flag(multi_scheduling, How),
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

atom_count() -> erlang:system_info(atom_count).

start_changes() ->
    Parent = self(),
    spawn(fun() ->
        true = register(managed_changer, self()),
        Parent ! ready,
        changes(2)
    end),
    receive ready -> ok end.
changes(N) ->
    receive stop -> ok
    after 0 ->
        erlang:system_flag(schedulers_online, N),
        erlang:system_flag(dirty_cpu_schedulers_online, 1),
        changes(3 - N)
    end.
stop_changes() ->
    Pid = whereis(managed_changer),
    Ref = monitor(process, Pid),
    Pid ! stop,
    receive {'DOWN', Ref, process, Pid, normal} -> ok end.

background_halt() ->
    spawn(fun() -> receive after 10 -> erlang:halt(31) end end),
    ok.

gzip_checks(<<RawSize:32, GzipSize:32, Raw:RawSize/binary,
              Gzip:GzipSize/binary, Members/binary>>) ->
    false = erlang:module_loaded(zlib),
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
    false = erlang:module_loaded(zlib),
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
    false = erlang:module_loaded(zlib),
    ok.

gzip_interrupt(Gzip) ->
    OldDirty = erlang:system_info(dirty_cpu_schedulers_online),
    OldSchedulers = erlang:system_flag(schedulers_online, 1),
    try
        {Pid, Ref} = spawn_monitor(fun() ->
            receive go -> ok end,
            _ = erts_internal:beamfile_gunzip(Gzip)
        end),
        Pid ! go,
        Deadline = erlang:monotonic_time(millisecond) + 10000,
        gzip_wait_trap(Pid, Ref, Deadline)
    after
        erlang:system_flag(schedulers_online, OldSchedulers),
        erlang:system_flag(dirty_cpu_schedulers_online, OldDirty)
    end.

gzip_wait_trap(Pid, Ref, Deadline) ->
    %% With one scheduler, this enquiry can observe the native continuation
    %% only after it has yielded and handled the enquiry signal.
    case erlang:process_info(Pid, current_function) of
        {current_function, {erts_internal, beamfile_gunzip, 1}} ->
            exit(Pid, kill),
            receive {'DOWN', Ref, process, Pid, killed} -> ok end;
        _ ->
            receive
                {'DOWN', Ref, process, Pid, Reason} ->
                    error({gzip_did_not_yield, Reason})
            after 0 ->
                case erlang:monotonic_time(millisecond) < Deadline of
                    true -> gzip_wait_trap(Pid, Ref, Deadline);
                    false -> exit(Pid, kill), error(gzip_trap_timeout)
                end
            end
    end.
