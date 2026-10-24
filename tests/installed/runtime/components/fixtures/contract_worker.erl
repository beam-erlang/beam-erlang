%% SPDX-License-Identifier: Apache-2.0
-module(contract_worker).
-export([files/1, sockets/1, callbacks/1, native_faults/1, abandon/1, returning/1, busy/1, cleanup_fail/1, returning_fault/1]).

returning(Path) -> files(Path), sockets(Path), 42.

files(Path) ->
    Expired = beam_time:deadline_after(0),
    badarg(fun() -> beam_file:info(not_binary, Expired) end),
    badarg(fun() -> beam_file:rename(Path, not_binary, infinity) end),
    badarg(fun() -> beam_file:open(Path, nonsense, #{mode => read}) end),
    {error, #{kind := invalid_data}} = beam_file:info(<<"relative">>, infinity),
    {error, #{kind := invalid_data}} = beam_file:info(<<"/", 255>>, infinity),
    {error, #{kind := invalid_data}} = beam_file:info(<<"/tmp/a", 0, "b">>, infinity),
    {error, #{kind := not_found, reason := enoent,
              native := #{domain := posix, code := enoent}}} = beam_file:info(<<Path/binary,".missing">>, infinity),
    ok = beam_file:write_file(Path, <<"abc">>, infinity),
    {ok, #{size := 3, type := regular}} = beam_file:info(Path, infinity),
    {ok, H} = beam_file:open(Path, infinity, #{mode => read}),
    {ok, <<>>} = beam_file:read(H, 0, Expired),
    {error, #{kind := timeout}} = beam_file:read(H, 1, Expired),
    {ok, <<"abc">>} = beam_file:read(H, 100, infinity),
    eof = beam_file:read(H, 100, infinity),
    Parent = self(),
    {Pid, Mon} = spawn_monitor(fun() ->
        Parent ! {self(), beam_file:read(H#{owner => self()}, 0, Expired)},
        Parent ! {self(), beam_file:close(H#{owner => self()}, Expired)}
    end),
    receive {Pid, {error, #{kind := not_owner}}} -> ok after 5000 -> error(owner_timeout) end,
    receive {Pid, {error, #{kind := not_owner}}} -> ok after 5000 -> error(close_owner_timeout) end,
    receive {'DOWN', Mon, process, Pid, normal} -> ok after 5000 -> error(owner_down_timeout) end,
    ok = beam_file:close(H, Expired),
    ok = beam_file:close(H, Expired),
    {error, #{kind := closed}} = beam_file:read(H, 0, infinity),
    {ok, W} = beam_file:open(Path, infinity, #{mode => write}),
    ok = beam_file:write(W, <<>>, Expired),
    {error, #{kind := timeout, bytes_written := 0, progress_unknown := false}} = beam_file:write(W, <<"x">>, Expired),
    ok = beam_file:close(W, Expired),
    {error, #{kind := closed, bytes_written := 0, progress_unknown := false}} = beam_file:write(W, <<>>, infinity),
    ok = unrepresentable_entry(Path),
    {ok, [<<"valid λ"/utf8>>]} = beam_file:list_directory(<<Path/binary,".valid-dir">>, infinity),
    Directory = <<Path/binary,".metadata">>,
    ok = beam_file:make_directory(Directory, infinity),
    {error, #{kind := busy, native := #{code := eexist}}} = beam_file:make_directory(Directory, infinity),
    ok = beam_file:write_file(Path, <<"metadata">>, infinity),
    Target = <<Directory/binary,"/renamed">>,
    ok = beam_file:rename(Path, Target, infinity),
    {error, #{kind := not_found}} = beam_file:info(Path, infinity),
    {ok, <<"metadata">>} = beam_file:read_file(Target, infinity),
    ok = beam_file:rename(Target, Path, infinity),
    ok = beam_file:write_file(Target, <<>>, infinity),
    ok = beam_file:remove_file(Target, infinity),
    ok = beam_file:remove_directory(Directory, infinity),
    {error, #{kind := timeout}} = beam_file:make_directory(Directory, Expired),
    {error, #{kind := not_found}} = beam_file:info(Directory, infinity),
    42.

sockets(_) ->
    D = beam_time:deadline_after(5000),
    E = beam_time:deadline_after(0),
    badarg(fun() -> beam_socket:listen(#{addr => {127.0,0,0,1},port => 0}, D) end),
    {ok, L} = beam_socket:listen(#{addr => {127,0,0,1},port => 0}, D),
    {ok, Address} = beam_socket:sockname(L),
    {error, #{kind := busy, native := #{code := eaddrinuse}}} = beam_socket:listen(Address, D),
    {error, #{kind := timeout}} = beam_socket:accept(L, beam_time:deadline_after(5)),
    Parent = self(),
    {Pid, Mon} = spawn_monitor(fun() ->
        {error, #{kind := not_owner}} = beam_socket:recv(L#{owner => self()}, 0, E),
        {ok, C} = beam_socket:connect(Address, D),
        ok = beam_socket:send(C, <<"a">>, D),
        Parent ! {self(), connected},
        receive continue -> ok after 5000 -> error(peer_wait) end,
        ok = beam_socket:send(C, <<"bc">>, D),
        receive stop -> ok after 5000 -> error(peer_stop) end,
        ok = beam_socket:close(C, E),
        ok = beam_socket:close(C, E)
    end),
    receive {Pid, connected} -> ok after 5000 -> error(connect_wait) end,
    {ok, S} = beam_socket:accept(L, D),
    {ok, <<>>} = beam_socket:recv(S, 0, E),
    ok = beam_socket:send(S, <<>>, E),
    {ok, <<"a">>} = beam_socket:recv(S, 100, D),
    {error, #{kind := timeout}} = beam_socket:recv(S, 100, beam_time:deadline_after(5)),
    Pid ! continue,
    {ok, <<"bc">>} = beam_socket:recv(S, 100, D),
    Pid ! stop,
    eof = beam_socket:recv(S, 100, D),
    receive {'DOWN', Mon, process, Pid, normal} -> ok after 5000 -> error(peer_down) end,
    {messages, Messages} = process_info(self(), messages),
    [] = [M || {'$socket', _, _, _} = M <- Messages],
    ok = beam_socket:close(S, E),
    ok = beam_socket:close(S, E),
    {error, #{kind := closed}} = beam_socket:recv(S, 0, E),
    ok = beam_socket:close(L, E),
    {error, #{kind := io, native := #{code := econnrefused}}} = beam_socket:connect(Address, D),
    partial_send(),
    42.

partial_send() ->
    D = beam_time:deadline_after(10000),
    {ok, L} = beam_socket:listen(#{addr => {127,0,0,1},port => 0}, D),
    {ok, Address} = beam_socket:sockname(L),
    Parent = self(),
    Size = 8 * 1024 * 1024,
    {Pid, Mon} = spawn_monitor(fun() ->
        {ok, C} = beam_socket:connect(Address, D),
        Parent ! {self(), ready},
        receive drain -> ok after 5000 -> error(drain_wait) end,
        Size = drain(C, 0, D),
        Parent ! {self(), drained},
        ok = beam_socket:close(C, infinity)
    end),
    receive {Pid, ready} -> ok after 5000 -> error(partial_ready) end,
    {ok, S} = beam_socket:accept(L, D),
    %% Bound kernel buffering without changing the public service options.
    ok = socket:setopt(maps:get(socket, S), socket, sndbuf, 4096),
    Data = binary:copy(<<"x">>, Size),
    {error, #{kind := timeout, bytes_written := Count, progress_unknown := false}} =
        beam_socket:send(S, Data, beam_time:deadline_after(20)),
    true = Count > 0 andalso Count < Size,
    Pid ! drain,
    ok = beam_socket:send(S, binary:part(Data, Count, Size - Count), D),
    ok = beam_socket:close(S, infinity),
    receive {Pid, drained} -> ok after 10000 -> error(drain_count) end,
    receive {'DOWN', Mon, process, Pid, normal} -> ok after 5000 -> error(drain_down) end,
    ok = beam_socket:close(S, infinity),
    ok = beam_socket:close(L, infinity).
drain(Socket, Count, Deadline) ->
    case beam_socket:recv(Socket, 65536, Deadline) of
        {ok, Bytes} ->
            true = Bytes =:= binary:copy(<<"x">>, byte_size(Bytes)),
            drain(Socket, Count + byte_size(Bytes), Deadline);
        eof -> Count;
        Other -> error({partial_drain, Count, Other})
    end.

callbacks(_) ->
    E = beam_time:deadline_after(0),
    badarg(fun() -> beam_output:write(out, <<>>, invalid) end),
    badarg(fun() -> beam_code:fetch(memory, helper, invalid) end),
    ok = beam_output:write(out, <<>>, E),
    {error, #{kind := timeout, bytes_written := 0, progress_unknown := false}} = beam_output:write(out, <<"x">>, E),
    ok = beam_output:write(slow, <<"x">>, beam_time:deadline_after(10)),
    {error, #{kind := io, bytes_written := 0, progress_unknown := true,
              native := #{domain := host, code := io}}} = beam_output:write(fail, <<"x">>, infinity),
    {ok, <<"not bytecode">>} = beam_code:fetch(slow, arbitrary, beam_time:deadline_after(10)),
    {error, #{kind := invalid_data}} = beam_code:load(arbitrary, <<"not bytecode">>, memory),
    false = erlang:module_loaded(arbitrary),
    42.

native_faults(Path) ->
    ok = prim_file:beam_test_io({none,0,0}),
    ok = beam_file:write_file(Path, <<"abc">>, infinity),
    {ok, R} = beam_file:open(Path, infinity, #{mode => read}),
    ok = prim_file:beam_test_io({read_before,80,0}),
    {error, #{kind := timeout}} = beam_file:read(R, 1, beam_time:deadline_after(20)),
    ok = prim_file:beam_test_io({read_after,80,0}),
    {ok, <<"a">>} = beam_file:read(R, 1, beam_time:deadline_after(20)),
    ok = prim_file:beam_test_io({none,0,0}),
    {ok, <<"bc">>} = beam_file:read(R, 100, infinity),
    ok = beam_file:close(R, infinity),
    {ok, W} = beam_file:open(Path, infinity, #{mode => write}),
    Data = binary:copy(<<"x">>, 200000),
    ok = prim_file:beam_test_io({write_before,0,1}),
    {error, #{kind := io, bytes_written := 65536, progress_unknown := false,
              native := #{code := enospc}}} = beam_file:write(W, Data, infinity),
    ok = prim_file:beam_test_io({write_after,80,0}),
    {error, #{kind := timeout, bytes_written := 65536, progress_unknown := false}} = beam_file:write(W, Data, beam_time:deadline_after(20)),
    ok = prim_file:beam_test_io({write_after,80,0}),
    ok = beam_file:write(W, <<"complete">>, beam_time:deadline_after(20)),
    ok = prim_file:beam_test_io({close_before,0,0}),
    {error, #{kind := io, native := #{code := eio}}} = beam_file:close(W, infinity),
    ok = prim_file:beam_test_io({none,0,0}),
    ok = beam_file:close(W, infinity),
    {ok, #{size := 131080}} = beam_file:info(Path, infinity),
    queued(Path),
    owner_cancel(Path),
    42.

queued(Path) ->
    %% The test host configures one dirty I/O scheduler. The first syscall
    %% settles late; the second request expires while waiting in that queue.
    ok = prim_file:beam_test_io({read_after,100,0}),
    Parent = self(),
    {Pid, Mon} = spawn_monitor(fun() ->
        {ok, R} = beam_file:open(Path, infinity, #{mode => read}),
        Parent ! {self(), ready},
        {ok, _} = beam_file:read(R, 1, infinity),
        ok = beam_file:close(R, infinity)
    end),
    receive {Pid, ready} -> ok after 5000 -> error(queue_ready) end,
    await_native(),
    %% Opening also goes through the dirty queue and must check its original
    %% deadline there, before any filesystem side effect.
    {error, #{kind := timeout}} = beam_file:open(<<Path/binary,".queued">>, beam_time:deadline_after(20), #{mode => write}),
    receive {'DOWN', Mon, process, Pid, normal} -> ok after 5000 -> error(queue_down) end,
    ok = prim_file:beam_test_io({none,0,0}),
    {error, #{kind := not_found}} = beam_file:info(<<Path/binary,".queued">>, infinity).
await_native() ->
    case prim_file:beam_test_io(entered) of
        0 -> receive after 1 -> await_native() end;
        _ -> ok
    end.

abandon(Path) ->
    {ok, H} = beam_file:open(Path, infinity, #{mode => read}),
    persistent_term:put(contract_abandoned, H),
    42.
badarg(Fun) ->
    try Fun() of Result -> error({expected_badarg, Result})
    catch error:badarg -> ok
    end.

busy(Path) ->
    ok = prim_file:beam_test_io({write_after,100,0}),
    spawn(fun() ->
        {ok, H} = beam_file:open(Path, infinity, #{mode => write}),
        beam_file:write(H, binary:copy(<<"x">>, 200000), infinity)
    end),
    await_native(),
    42.
cleanup_fail(_) -> prim_file:beam_test_io({close_before,0,0}), 42.

owner_cancel(Path) ->
    Parent = self(),
    ok = prim_file:beam_test_io({write_after,100,0}),
    {Pid, Mon} = spawn_monitor(fun() ->
        {ok, W} = beam_file:open(Path, infinity, #{mode => write}),
        Parent ! {self(), ready},
        beam_file:write(W, binary:copy(<<"x">>, 200000), infinity)
    end),
    receive {Pid, ready} -> ok after 5000 -> error(cancel_ready) end,
    await_native(),
    exit(Pid, kill),
    receive {'DOWN', Mon, process, Pid, killed} -> ok after 5000 -> error(cancel_down) end,
    ok = prim_file:beam_test_io({none,0,0}),
    {ok, #{size := 65536}} = beam_file:info(Path, infinity).

returning_fault(Path) ->
    files(Path),
    sockets(Path),
    native_faults(Path),
    abandon(Path),
    busy(Path),
    cleanup_fail(Path),
    42.

%% APFS rejects creation of raw non-UTF-8 names. The host probes the filesystem
%% before selecting this input variation; malformed API paths remain tested above.
-ifdef(ACCEPTANCE_RAW_NAMES).
unrepresentable_entry(Path) ->
    {error, #{kind := invalid_data, reason := unrepresentable_name}} =
        beam_file:list_directory(<<Path/binary,".invalid-dir">>, infinity),
    ok.
-else.
unrepresentable_entry(Path) ->
    {ok, []} = beam_file:list_directory(<<Path/binary,".invalid-dir">>, infinity),
    ok.
-endif.
