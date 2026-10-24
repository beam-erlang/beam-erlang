%% SPDX-License-Identifier: Apache-2.0
%% Acceptance programs for the returning boot. Loaded explicitly, never embedded.
-module(returning_programs).
-export([ret_context/1, finish_then_halt/1, service_then_halt/1, return_then_halt/1, exception_then_halt/1, library/1, ret/1, boom/1, finish/1, halt/1, kill_output/1, cleanup/1,
         greeting/1, counter/1, counter_fault/1, code_client/1, code_missing/1, code_bad/1,
         file_report/2, file_edges/2, file_omitted/1, file_owner_death/1,
         echo/1, echo_partial/1, echo_peer/1, echo_owner/1, echo_fault/1, echo_abandon/1,
         record_service/1, record_fault/1, record_unreadable/1,
         zlib_explicit/1, zlib_abandon/1, zlib_persist/1, zlib_owner/1,
         output_cases/1, omitted/1, services/1, preloaded/1]).

library(_) ->
    true = preloaded_ok(),
    false = erlang:module_loaded(beam_boot),
    false = erlang:module_loaded(beam_file),
    false = erlang:module_loaded(beam_socket),
    {error, #{kind := invalid_data}} = beam_text:from_utf8(<<16#80>>),
    {ok, <<"ok">>} = beam_text:from_utf8(<<"ok">>),
    <<"ab">> = beam_text:concat([<<"a">>, <<"b">>]),
    <<"42">> = beam_format:integer(42),
    <<"...">> = beam_format:term([1, 2], #{depth => 0, bytes => 8}),
    <<"..">> = beam_format:term(123, #{depth => 2, bytes => 2}),
    <<"bc">> = beam_bytes:slice(<<"abcd">>, 1, 2),
    {ok, 2} = beam_map:find(#{a => 1, b => 2}, b),
    not_found = beam_map:find(#{}, missing),
    [2, 4] = beam_list:map([1, 2], fun(N) -> N * 2 end),
    6 = beam_list:fold([1, 2, 3], 0, fun(N, Acc) -> N + Acc end),
    {'EXIT', {x, _}} = (catch beam_list:map([1], fun(_) -> error(x) end)),
    Deadline = beam_time:deadline_after(1000),
    false = beam_time:expired(Deadline),
    {Pid, Mon} = beam:spawn_monitor(fun() -> receive {From, N} -> From ! {self(), N + 1} end end),
    beam:send(Pid, {self(), 2}),
    3 = receive {Pid, Value} -> Value end,
    ok = beam:demonitor(Mon),
    {HugeText, HugeReds, MapReds, FloatText, Nested, Opened} = format_bounds(),
    {HugeText, HugeReds, MapReds, FloatText, Nested, Opened, <<"LIBRARY_OK">>}.

preloaded(_) ->
    Names = erlang:pre_loaded(),
    9 = count(Names),
    true = member(erlang, Names),
    true = member(persistent_term, Names),
    false = member(lists, Names),
    false = member(maps, Names),
    Names.

ret_context(Value) ->
    42 = erts_internal:vm_context(),
    ret(Value).

ret(Value) ->
    Value.

boom(_) ->
    erlang:error(boom).

finish(_) ->
    beam_boot:finish(4).

halt(_) ->
    erlang:halt(9).

kill_output(_) ->
    case whereis(beam_service_output) of
        Pid when is_pid(Pid) -> exit(Pid, kill);
        _ -> ok
    end,
    receive after 1000 -> ok end,
    beam_output:write(standard_output, <<"late">>, infinity).

cleanup(_) ->
    persistent_term:put(beam_cleanup_fault, <<"cleanup-fault">>),
    ok.

greeting(Name) ->
    ok = beam_output:write(standard_output, <<"Hello, ", Name/binary, "!\n">>, infinity),
    Name.

counter(_) ->
    {First, FirstMonitor, Eight} = run_worker(5, 3),
    exit(First, kill),
    await_down(First, FirstMonitor, killed),
    {_Second, _SecondMonitor, Seven} = run_worker(5, 2),
    %% Leave the replacement live to exercise entry-scoped teardown.
    {Eight, Seven, 1}.

counter_fault(Mode) ->
    {Pid, Mon} = beam:spawn_monitor(fun() -> faulty_worker(Mode) end),
    Ref = make_ref(),
    Pid ! {self(), Ref, 3},
    await_reply(Pid, Mon, Ref).

code_client(_) ->
    {ok, Bytes} = beam_code:fetch(memory, uppercase_transform, infinity),
    ok = beam_code:load(uppercase_transform, Bytes, memory),
    uppercase_transform:apply(<<"beam">>).

code_missing(_) ->
    {error, #{kind := not_found}} = beam_code:fetch(memory, missing_transform, infinity),
    <<"still">> = kept:value().

code_bad(_) ->
    {ok, Bytes} = beam_code:fetch(memory, not_a_module, infinity),
    {error, #{kind := invalid_data}} = beam_code:load(not_a_module, Bytes, memory),
    <<"still">> = kept:value().

file_report(Input, Output) ->
    false = erlang:module_loaded(beam_socket),
    false = erlang:module_loaded(prim_socket),
    false = erlang:module_loaded(zlib),
    {ok, Bytes} = beam_file:read_file(Input, infinity),
    Lines = count_lines(Bytes),
    Size = byte_size(Bytes),
    Report = <<"lines=", (integer_to_binary(Lines))/binary,
               " bytes=", (integer_to_binary(Size))/binary, "\n">>,
    {ok, Handle} = beam_file:open(Output, infinity, #{mode => write}),
    ok = beam_file:write(Handle, Report, infinity),
    ok = beam_file:close(Handle, infinity),
    {Lines, Size}.

file_edges(Missing, Full) ->
    {error, #{kind := MissingKind}} = beam_file:read_file(Missing, infinity),
    {error, #{kind := InvalidKind}} = beam_file:read_file(<<"relative">>, infinity),
    {error, #{kind := WriteKind}} = beam_file:write_file(Full, <<"x">>, infinity),
    {ok, Handle} = beam_file:open(<<"/etc/hosts">>, infinity, #{mode => read}),
    Parent = self(),
    spawn(fun() -> Parent ! {edge, beam_file:read(Handle, 4, infinity)} end),
    {error, #{kind := OwnerKind}} = receive {edge, Result} -> Result end,
    ok = beam_file:close(Handle, infinity),
    {atom_to_binary(MissingKind), atom_to_binary(InvalidKind),
     atom_to_binary(WriteKind), atom_to_binary(OwnerKind)}.

file_omitted(_) ->
    false = erlang:module_loaded(beam_file),
    false = erlang:module_loaded(beam_socket),
    {'EXIT', {undef, _}} = (catch beam_file:read_file(<<"/etc/hosts">>, infinity)),
    {'EXIT', {undef, _}} = (catch prim_file:read_file("/etc/hosts")),
    {'EXIT', {undef, _}} = (catch beam_socket:sockname(undefined)),
    <<"undef">>.

file_owner_death(_) ->
    exit(whereis(beam_file_owner), kill),
    receive after 200 -> ok end,
    beam_file:read_file(<<"/etc/hosts">>, infinity).

echo(_) ->
    false = erlang:module_loaded(beam_file),
    false = erlang:module_loaded(prim_file),
    false = erlang:module_loaded(zlib),
    echo_session(normal).

echo_partial(_) ->
    {1, 4, [<<"part">>]} = echo_session(partial),
    <<"part">>.

echo_peer(_) ->
    try echo_session(<<"header">>) of
        _ -> error(truncated_header_was_accepted)
    catch
        error:truncated_header -> <<"PEER_EOF">>
    end.

echo_abandon(_) ->
    {ok, Listen} = beam_socket:listen(#{addr => {127, 0, 0, 1}, port => 0},
                                    beam_time:deadline_after(5000)),
    {ok, #{port := Port}} = beam_socket:sockname(Listen),
    %% A persistent root retains a real open listener until VM teardown.
    persistent_term:put(returning_abandoned_listener, Listen),
    Port.

echo_fault(Mode) ->
    echo_session(Mode).

echo_owner(_) ->
    exit(whereis(socket_registry), kill),
    receive after 200 -> ok end,
    beam_socket:listen(#{addr => {127, 0, 0, 1}, port => 0}, infinity).

echo_session(Mode) ->
    Deadline = beam_time:deadline_after(5000),
    {ok, Listen} = beam_socket:listen(#{addr => {127, 0, 0, 1}, port => 0}, Deadline),
    {ok, #{port := Port}} = beam_socket:sockname(Listen),
    Parent = self(),
    {Client, Mon} = beam:spawn_monitor(fun() -> echo_client(Port, Parent, Mode, Deadline) end),
    try
        receive
            {connected, Client} -> ok;
            {'DOWN', Mon, process, Client, Reason} -> error({client_failed, Reason})
        after beam_time:remaining(Deadline) -> error(client_connect_timeout)
        end,
        {ok, Accepted} = beam_socket:accept(Listen, Deadline),
        Result = try collect(Accepted, 0, 0, [], Deadline)
                 after close_socket(Accepted)
                 end,
        receive
            {client_done, Client} -> ok;
            {'DOWN', Mon, process, Client, Reason2} -> error({client_failed, Reason2})
        after beam_time:remaining(Deadline) -> error(client_completion_timeout)
        end,
        await_down(Client, Mon, normal),
        Result
    after
        close_socket(Listen),
        retire_worker(Client, Mon)
    end.

close_socket(Socket) ->
    %% An expired operation deadline must not suppress explicit cleanup.
    ok = beam_socket:close(Socket, beam_time:deadline_after(5000)).

record_service(Path) ->
    {ok, Alpha} = store(Path, 1, <<"alpha">>),
    Rebuilt = reconstruct(Path, 1),
    {ok, Beta} = store(Path, 2, <<"beta">>),
    Interrupted = interrupt(Path),
    {Alpha, Rebuilt, Beta, 2, 1, Interrupted}.

record_unreadable(Path) ->
    try record_service(Path) of
        _ -> error(unreadable_record_succeeded)
    catch
        error:{worker_failed, {{record_read_failed,
                               #{kind := permission_denied, service := file}}, _}} ->
            error(record_permission_denied)
    end.

record_fault(Mode) ->
    {Pid, Mon} = beam:spawn_monitor(fun() -> faulty_worker(Mode) end),
    Ref = make_ref(),
    Pid ! {self(), Ref, {store, 1, <<"unused">>}},
    await_reply(Pid, Mon, Ref).

zlib_explicit(_) ->
    Stream = zlib:open(),
    ok = zlib:deflateInit(Stream),
    _ = zlib:deflate(Stream, <<"abc">>, finish),
    ok = zlib:deflateEnd(Stream),
    ok = zlib:close(Stream),
    closed.

zlib_abandon(_) ->
    Stream = zlib:open(),
    ok = zlib:deflateInit(Stream),
    beam:spawn(fun() ->
                       Abandoned = zlib:open(),
                       ok = zlib:deflateInit(Abandoned)
               end),
    receive after 50 -> ok end,
    ok = zlib:deflateEnd(Stream),
    ok = zlib:close(Stream),
    abandoned.

zlib_persist(_) ->
    Stream = zlib:open(),
    ok = zlib:inflateInit(Stream),
    persistent_term:put(beam_zlib_hold, Stream),
    held.

zlib_owner(_) ->
    exit(whereis(beam_service_zlib), kill),
    receive after 200 -> ok end,
    zlib:open().

format_bounds() ->
    Huge = 1 bsl 2000,
    Full = erlang:integer_to_binary(Huge),
    Shown = beam_format:term(Huge, #{depth => 4, bytes => 8}),
    <<Prefix:5/binary, "...">> = Shown,
    <<Prefix:5/binary, _/binary>> = Full,
    <<Sliced:8/binary, _/binary>> = Full,
    true = Shown =/= Sliced,
    %% 2^1000000 has 301030 decimal digits. Rendering that text, or a power of
    %% ten of the same size, is millions of reductions. Stopping at the first
    %% five digits stays far below that.
    Million = 1 bsl 1000000,
    {reductions, BeforeHuge} = erlang:process_info(self(), reductions),
    <<"99006...">> = beam_format:term(Million, #{depth => 4, bytes => 8}),
    {reductions, AfterHuge} = erlang:process_info(self(), reductions),
    HugeReds = AfterHuge - BeforeHuge,
    %% integer_to_binary(1 bsl 1000000) is about four million reductions.
    true = HugeReds < 500000,
    AtomText = beam_format:term('aaaaaaaaaaaaaaaaaaaa', #{depth => 2, bytes => 8}),
    <<"aaaaa...">> = AtomText,
    <<AtomSlice:8/binary, _/binary>> = erlang:atom_to_binary('aaaaaaaaaaaaaaaaaaaa'),
    true = AtomText =/= AtomSlice,
    %% Budget 4 cannot hold a short float. The result is the marker.
    %% Slicing float_to_binary([short]) would keep a digit.
    <<"...">> = beam_format:term(1.0e300, #{depth => 2, bytes => 4}),
    %% Fixed-point decimals badarg for this magnitude. The short form is used
    %% only when it fits; otherwise the marker is the whole result.
    Short = erlang:float_to_binary(1.0e300, [short]),
    FloatText = beam_format:term(1.0e300, #{depth => 2, bytes => 8}),
    true = is_binary(FloatText) andalso byte_size(FloatText) =< 8,
    case byte_size(Short) =< 8 of
        true -> Short = FloatText;
        false -> <<"...">> = FloatText
    end,
    Nested = beam_format:term([1.0e300], #{depth => 3, bytes => 8}),
    true = is_binary(Nested) andalso byte_size(Nested) =< 8,
    <<"[", _/binary>> = Nested,
    <<"#{1 => 2}">> = beam_format:term(#{1 => 2}, #{depth => 3, bytes => 40}),
    <<"[1, 2]">> = beam_format:term([1, 2], #{depth => 3, bytes => 40}),
    <<"{1, 2}">> = beam_format:term({1, 2}, #{depth => 3, bytes => 40}),
    <<"#{}">> = beam_format:term(#{}, #{depth => 3, bytes => 40}),
    Opened = <<"#{1 => 2}|[1, 2]|{1, 2}|#{}">>,
    Map = wide_map(4000, 0, #{}),
    {reductions, Before} = erlang:process_info(self(), reductions),
    Text = beam_format:term(Map, #{depth => 3, bytes => 8}),
    {reductions, After} = erlang:process_info(self(), reductions),
    MapReds = After - Before,
    true = byte_size(Text) =< 8,
    %% Walking all 4000 pairs is about 950000 reductions.
    true = MapReds < 8000,
    {<<"99006...">>, HugeReds, MapReds, FloatText, Nested, Opened}.

wide_map(0, _Key, Map) ->
    Map;
wide_map(Count, Key, Map) ->
    wide_map(Count - 1, Key + 1, Map#{Key => Key}).

output_cases(_) ->
    ok = beam_output:write(standard_output, <<"ready">>, infinity),
    {error, #{kind := not_found}} = beam_output:write(missing_destination, <<"x">>, infinity),
    {error, #{kind := unavailable}} = beam_output:write(standard_output, <<"wait">>, infinity),
    {error, #{kind := busy}} = beam_output:write(standard_output, <<"full">>, infinity),
    {error, #{kind := io}} = beam_output:write(standard_output, <<"fail">>, infinity),
    cases.

omitted(_) ->
    try io:format("no output~n") of
        Result -> {ok, Result}
    catch
        Class:Reason -> {Class, Reason}
    end.

services(_) ->
    beam_boot:services().

run_worker(Start, Add) ->
    {Pid, Mon} = beam:spawn_monitor(fun() -> worker(Start) end),
    Ref = make_ref(),
    Pid ! {self(), Ref, Add},
    {Pid, Mon, await_reply(Pid, Mon, Ref)}.

await_reply(Pid, Mon, Ref) ->
    receive
        {Ref, Reply} -> Reply;
        {'DOWN', Mon, process, Pid, Reason} -> error({worker_failed, Reason})
    after 5000 -> error(worker_timeout)
    end.

await_down(Pid, Mon, Expected) ->
    receive
        {'DOWN', Mon, process, Pid, Expected} -> ok;
        {'DOWN', Mon, process, Pid, Reason} -> error({worker_stop_failed, Reason})
    after 5000 -> error(worker_stop_timeout)
    end.

retire_worker(Pid, Mon) ->
    CleanupMonitor = erlang:monitor(process, Pid),
    exit(Pid, kill),
    receive
        {'DOWN', CleanupMonitor, process, Pid, _} -> ok
    after 5000 -> error(worker_cleanup_timeout)
    end,
    erlang:demonitor(Mon, [flush]).

faulty_worker(<<"crash">>) ->
    receive {_From, _Ref, _Request} -> exit(deliberate_worker_failure) end;
faulty_worker(<<"stall">>) ->
    receive {_From, _Ref, _Request} -> receive never -> ok end end.

worker(Value) ->
    receive
        {From, Ref, Add} ->
            From ! {Ref, Value + Add},
            worker(Value + Add)
    end.

count_lines(Bytes) ->
    count_lines(Bytes, 0).

count_lines(<<>>, Count) ->
    Count;
count_lines(<<$\n, Rest/binary>>, Count) ->
    count_lines(Rest, Count + 1);
count_lines(<<_, Rest/binary>>, Count) ->
    count_lines(Rest, Count).

echo_client(_Port, _Parent, <<"crash">>, _Deadline) ->
    exit(deliberate_client_failure);
echo_client(Port, Parent, Mode, Deadline) ->
    {ok, Socket} = beam_socket:connect(#{addr => {127, 0, 0, 1}, port => Port}, Deadline),
    Parent ! {connected, self()},
    try send_workload(Socket, Mode, Deadline)
    after close_socket(Socket)
    end,
    Parent ! {client_done, self()}.

send_workload(Socket, normal, Deadline) ->
    send_frame(Socket, <<"one">>, Deadline),
    send_frame(Socket, <<"two">>, Deadline),
    send_frame(Socket, <<"three">>, Deadline);
send_workload(Socket, partial, Deadline) ->
    send_parts(Socket, [<<0>>, <<0, 0>>, <<4>>, <<"p">>, <<"ar">>, <<"t">>], Deadline);
send_workload(Socket, <<"header">>, Deadline) ->
    send_parts(Socket, [<<0>>, <<0>>], Deadline);
send_workload(Socket, <<"body">>, Deadline) ->
    send_parts(Socket, [<<4:32>>, <<"pa">>], Deadline);
send_workload(Socket, <<"empty-body">>, Deadline) ->
    send_parts(Socket, [<<4:32>>], Deadline);
send_workload(Socket, <<"oversized">>, Deadline) ->
    send_parts(Socket, [<<4097:32>>], Deadline);
send_workload(_Socket, <<"stall">>, _Deadline) ->
    receive never -> ok end.

send_parts(_Socket, [], _Deadline) -> ok;
send_parts(Socket, [Part | Rest], Deadline) ->
    ok = beam_socket:send(Socket, Part, Deadline),
    send_parts(Socket, Rest, Deadline).

send_frame(Socket, Bytes, Deadline) ->
    Size = byte_size(Bytes),
    ok = beam_socket:send(Socket, <<Size:32, Bytes/binary>>, Deadline).

collect(Socket, Frames, Bytes, Acc, Deadline) ->
    case read_frame(Socket, Deadline) of
        eof -> {Frames, Bytes, Acc};
        {ok, Frame} ->
            collect(Socket, Frames + 1, Bytes + byte_size(Frame), Acc ++ [Frame], Deadline)
    end.

read_frame(Socket, Deadline) ->
    case read_exact(Socket, 4, <<>>, Deadline) of
        eof -> eof;
        truncated -> error(truncated_header);
        {ok, <<Size:32>>} when Size =< 4096 ->
            case read_exact(Socket, Size, <<>>, Deadline) of
                {ok, _} = Result -> Result;
                _ -> error(truncated_body)
            end;
        {ok, _} -> error(oversized_frame)
    end.

read_exact(_Socket, 0, Acc, _Deadline) ->
    {ok, Acc};
read_exact(Socket, Need, Acc, Deadline) ->
    %% Single-byte reads preserve the distinction between clean EOF and a
    %% truncated header even when the underlying exact-length read closes.
    case beam_socket:recv(Socket, 1, Deadline) of
        eof when Acc =:= <<>> -> eof;
        eof -> truncated;
        {ok, Data} ->
            read_exact(Socket, Need - byte_size(Data), <<Acc/binary, Data/binary>>, Deadline);
        {error, #{kind := timeout}} -> error(socket_timeout);
        {error, Reason} -> error({socket_read_failed, Reason})
    end.

store(Path, Id, Data) ->
    {Pid, Mon} = beam:spawn_monitor(fun() -> record_worker(Path) end),
    Ref = make_ref(),
    Pid ! {self(), Ref, {store, Id, Data}},
    Result = await_reply(Pid, Mon, Ref),
    exit(Pid, shutdown),
    await_down(Pid, Mon, shutdown),
    Result.

interrupt(Path) ->
    {Pid, Mon} = beam:spawn_monitor(fun() -> hold_request(Path) end),
    Ref = make_ref(),
    Pid ! {self(), Ref, {store, 9, <<"never">>}},
    accepted = await_reply(Pid, Mon, Ref),
    exit(Pid, kill),
    await_down(Pid, Mon, killed),
    interrupted.

hold_request(Path) ->
    receive
        {From, Ref, Request} ->
            From ! {Ref, accepted},
            receive
                continue -> apply_store(Path, From, Ref, Request)
            end
    end.

record_worker(Path) ->
    receive
        {From, Ref, Request} ->
            apply_store(Path, From, Ref, Request),
            record_worker(Path)
    end.

apply_store(Path, From, Ref, {store, Id, Data}) ->
    Line = <<(integer_to_binary(Id))/binary, " ", Data/binary, "\n">>,
    Deadline = beam_time:deadline_after(5000),
    Existing = case beam_file:read_file(Path, Deadline) of
                   {ok, Bytes} -> Bytes;
                   {error, #{kind := not_found, service := file}} -> <<>>;
                   {error, Error} -> error({record_read_failed, Error})
               end,
    {ok, Handle} = beam_file:open(Path, Deadline, #{mode => write}),
    try
        ok = beam_file:write(Handle, <<Existing/binary, Line/binary>>, Deadline)
    after
        ok = beam_file:close(Handle, beam_time:deadline_after(5000))
    end,
    From ! {Ref, {ok, Data}}.

reconstruct(Path, Id) ->
    {ok, Bytes} = beam_file:read_file(Path, beam_time:deadline_after(5000)),
    scan(Bytes, integer_to_binary(Id)).

scan(<<Id:1/binary, " ", Rest/binary>>, Id) ->
    line_data(Rest, <<>>);
scan(<<_, Rest/binary>>, Id) ->
    scan(Rest, Id);
scan(<<>>, _Id) ->
    missing.

line_data(<<$\n, _/binary>>, Acc) ->
    Acc;
line_data(<<Byte, Rest/binary>>, Acc) ->
    line_data(Rest, <<Acc/binary, Byte>>);
line_data(<<>>, Acc) ->
    Acc.

preloaded_ok() ->
    Names = erlang:pre_loaded(),
    count(Names) =:= 9 andalso member(erlang, Names) andalso
        member(persistent_term, Names) andalso not member(lists, Names).

count([]) -> 0;
count([_ | Rest]) -> 1 + count(Rest).

member(_Name, []) -> false;
member(Name, [Name | _]) -> true;
member(Name, [_ | Rest]) -> member(Name, Rest).

%% DOWN is emitted only after the entry has completed. Its recorded outcome
%% must survive a halt from retiring application children.
return_then_halt(_) -> arm_late_halt(16), <<"won">>.
exception_then_halt(_) -> arm_late_halt(16), error(won).
arm_late_halt(0) -> ok;
arm_late_halt(N) ->
    Parent = self(),
    spawn_opt(fun() ->
        Ref = monitor(process, Parent),
        Parent ! {armed, self()},
        receive {'DOWN', Ref, process, Parent, _} -> erlang:halt(17) end
    end, [{priority, max}]),
    receive {armed, _} -> ok end,
    arm_late_halt(N - 1).

finish_then_halt(_) -> arm_late_halt(16), beam_boot:finish(6).
service_then_halt(_) -> arm_late_halt(16), exit(whereis(beam_service_output), kill), receive after 5000 -> error(component_did_not_stop) end.
