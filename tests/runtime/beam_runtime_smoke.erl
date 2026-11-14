%% Compiled by the VM under test using only the copied upstream bootstrap.
-module(beam_runtime_smoke).
-export([run/6]).
-include_lib("kernel/include/file.hrl").

run(Scratch, Flavor, Backend, Fixture, BuildType, KernelPoll) ->
    Flavor = erlang:system_info(emu_flavor),
    ExpectedDiagnostics = case BuildType of
                             debug -> {debug, true, true};
                             opt -> {opt, false, false}
                         end,
    ExpectedDiagnostics = {erlang:system_info(build_type),
                           erlang:system_info(debug_compiled),
                           erlang:system_info(lock_checking)},
    KernelPoll = erlang:system_info(kernel_poll),
    [file] = inet_db:res_option(lookup),
    "beam.test" = inet_db:res_option(domain),
    undefined = whereis(inet_gethost_native),
    utf8 = file:native_name_encoding(),
    allocator_options(),
    CheckIO = erlang:system_info(check_io),
    true = lists:any(
        fun(Info) ->
            lists:member({primary, Backend}, Info) andalso
                lists:member({fallback, false}, Info)
        end, CheckIO),
    monotonic_timer_and_message(),
    platform_core_headers(),
    file_roundtrip(Scratch),
    tcp_roundtrip(inet),
    tcp_roundtrip(socket),
    child_exit(),
    compile_and_load(Scratch),
    assert_fixture_paths(Fixture, Scratch),
    ok.

%% Linux boots request these glibc settings; missing feature probes must not
%% silently turn ordinary emulator arguments into no-ops.
allocator_options() ->
    case erlang:system_info(allocator) of
        {glibc, _, _, Settings} ->
            {sys_alloc, Options} = lists:keyfind(sys_alloc, 1, Settings),
            {tt, 524288} = lists:keyfind(tt, 1, Options),
            {tp, 8192} = lists:keyfind(tp, 1, Options),
            ok;
        _ -> ok
    end.

monotonic_timer_and_message() ->
    Start = erlang:monotonic_time(nanosecond),
    Token = make_ref(),
    erlang:send_after(10, self(), {timer, Token}),
    receive {timer, Token} -> ok after 3000 -> error(timer_timeout) end,
    true = erlang:monotonic_time(nanosecond) > Start,
    Parent = self(),
    {Pid, Monitor} = spawn_monitor(fun() ->
        Parent ! {computed, self(), lists:sum(lists:seq(1, 10000))}
    end),
    receive {computed, Pid, 50005000} -> ok after 3000 -> error(message_timeout) end,
    receive {'DOWN', Monitor, process, Pid, normal} -> ok
    after 3000 -> error(process_timeout)
    end.

platform_core_headers() ->
    case os:type() of
        {unix, darwin} ->
            performance_counter_clock(),
            inflate_dictionary();
        {win32, _} ->
            inflate_dictionary();
        {unix, linux} ->
            linux_clocks(),
            inflate_dictionary();
        _ ->
            ok
    end.

linux_clocks() ->
    Source = erlang:system_info(os_monotonic_time_source),
    clock_gettime = proplists:get_value(function, Source),
    'CLOCK_MONOTONIC' = proplists:get_value(clock_id, Source),
    1_000_000_000 = erlang:convert_time_unit(1, second, perf_counter),
    Before = proplists:get_value(time, Source),
    true = is_integer(Before) andalso Before >= 0,
    Counter = os:perf_counter(nanosecond),
    After = proplists:get_value(time, erlang:system_info(os_monotonic_time_source)),
    true = Before =< Counter andalso Counter =< After,
    ok.

performance_counter_clock() ->
    %% The raw OS clock exposes the uptime origin without Erlang monotonic
    %% time's offset. Checking only increasing counters misses a wall-clock
    %% fallback, and checking only their units misses the wrong clock origin.
    1_000_000_000 = erlang:convert_time_unit(1, second, perf_counter),
    Before = macos_uptime_nanoseconds(),
    Counter = os:perf_counter(nanosecond),
    After = macos_uptime_nanoseconds(),
    case Before =< Counter andalso Counter =< After of
        true -> ok;
        false -> error({perf_counter_clock_origin, Before, Counter, After})
    end.

macos_uptime_nanoseconds() ->
    Source = erlang:system_info(os_monotonic_time_source),
    clock_gettime = proplists:get_value(function, Source),
    'CLOCK_UPTIME_RAW' = proplists:get_value(clock_id, Source),
    Time = proplists:get_value(time, Source),
    true = is_integer(Time) andalso Time >= 0,
    Time.

inflate_dictionary() ->
    %% Inspect an active stream: a complete one-shot inflate need not retain
    %% its sliding window. Normal decompression alone misses dictionary support.
    Payload = <<"BEAM inflate dictionary roundtrip", 0, 255, 127>>,
    Deflater = zlib:open(),
    Stream = zlib:open(),
    try
        ok = zlib:deflateInit(Deflater),
        Compressed = zlib:deflate(Deflater, Payload, sync),
        ok = zlib:inflateInit(Stream),
        <<>> = zlib:inflateGetDictionary(Stream),
        Payload = iolist_to_binary(zlib:inflate(Stream, Compressed)),
        Payload = zlib:inflateGetDictionary(Stream),
        Trailer = zlib:deflate(Deflater, <<>>, finish),
        <<>> = iolist_to_binary(zlib:inflate(Stream, Trailer)),
        ok = zlib:inflateEnd(Stream),
        ok = zlib:deflateEnd(Deflater)
    after
        zlib:close(Stream),
        zlib:close(Deflater)
    end.

file_roundtrip(Scratch) ->
    Path = filename:join(Scratch, "file roundtrip λ.bin"),
    Payload = <<"BEAM file roundtrip", 0, 255, 127>>,
    ok = file:write_file(Path, Payload),
    {ok, Payload} = file:read_file(Path),
    Size = byte_size(Payload),
    {ok, #file_info{size = Size, type = regular}} = file:read_file_info(Path),
    {ok, Handle} = file:open(Path, [read, binary, raw]),
    try
        case os:type() of
            {unix, linux} -> ok = file:advise(Handle, 0, 0, normal);
            _ -> ok
        end,
        {ok, #file_info{size = Size, type = regular}} = file:read_file_info(Handle)
    after
        ok = file:close(Handle)
    end,
    ok = file:delete(Path).

tcp_roundtrip(InetBackend) ->
    Options = [{inet_backend, InetBackend}, binary, {active, false},
               {ip, {127, 0, 0, 1}}],
    {ok, Listen} = gen_tcp:listen(0, Options),
    try
        {ok, {{127, 0, 0, 1}, Port}} = inet:sockname(Listen),
        Parent = self(),
        {Pid, Monitor} = spawn_monitor(fun() ->
            {ok, Client} = gen_tcp:connect({127, 0, 0, 1}, Port, Options, 3000),
            ok = gen_tcp:send(Client, <<"ping">>),
            {ok, <<"pong">>} = gen_tcp:recv(Client, 4, 3000),
            ok = gen_tcp:close(Client),
            Parent ! {socket_ok, self()}
        end),
        {ok, Server} = gen_tcp:accept(Listen, 3000),
        try
            {ok, <<"ping">>} = gen_tcp:recv(Server, 4, 3000),
            ok = gen_tcp:send(Server, <<"pong">>)
        after
            gen_tcp:close(Server)
        end,
        receive {socket_ok, Pid} -> ok after 3000 -> error(socket_timeout) end,
        receive {'DOWN', Monitor, process, Pid, normal} -> ok
        after 3000 -> error(socket_process_timeout)
        end
    after
        gen_tcp:close(Listen)
    end.

child_exit() ->
    {Executable, Args, Expected} = case os:type() of
        {win32, _} ->
            Root = os:getenv("SystemRoot"),
            {filename:join([Root, "System32", "cmd.exe"]),
             ["/d", "/c", "echo beam-child"],
             <<"beam-child\r\n">>};
        _ ->
            {"/bin/sh", ["-c", "printf beam-child"], <<"beam-child">>}
    end,
    Port = open_port({spawn_executable, Executable},
                     [{args, Args}, binary, exit_status]),
    collect_child(Port, <<>>, Expected).

collect_child(Port, Output, Expected) ->
    receive
        {Port, {data, Data}} -> collect_child(Port, <<Output/binary, Data/binary>>, Expected);
        {Port, {exit_status, 0}} -> Expected = Output, ok;
        {Port, {exit_status, Status}} -> error({child_exit_status, Status, Output})
    after 3000 ->
        error(child_timeout)
    end.

compile_and_load(Scratch) ->
    Source = filename:join(Scratch, "beam_generated_smoke.erl"),
    ok = file:write_file(Source,
        <<"-module(beam_generated_smoke).\n-export([value/1]).\n"
          "value(N) -> (N * N) bxor 16#beef.\n">>),
    {ok, beam_generated_smoke, Binary, []} =
        compile:file(Source, [binary, return_errors, return_warnings]),
    {module, beam_generated_smoke} =
        code:load_binary(beam_generated_smoke, Source, Binary),
    Expected = (12 * 12) bxor 16#beef,
    Expected = beam_generated_smoke:value(12),
    true = code:delete(beam_generated_smoke),
    _ = code:purge(beam_generated_smoke),
    ok = file:delete(Source).

assert_fixture_paths(Fixture, Scratch) ->
    lists:foreach(
        fun(Path) ->
            case inside(Path, Fixture) of
                true -> ok;
                false -> error({unexpected_code_path, Path})
            end
        end,
        code:get_path()),
    lists:foreach(
        fun({_Module, preloaded}) -> ok;
           ({beam_runtime_smoke, _Path}) -> ok;
           ({_Module, Path}) when is_list(Path) ->
                case inside(Path, Fixture) orelse inside(Path, Scratch) of
                    true -> ok;
                    false -> error({unexpected_module_path, Path})
                end
        end, code:all_loaded()).

inside(Path, Root) ->
    lists:prefix(filename:split(filename:absname(Root)),
                 filename:split(filename:absname(Path))).
