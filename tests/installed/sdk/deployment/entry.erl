-module(entry).
-export([bare/0, boot/0, file/0, network/0, zlib/0, program/0, otp/0]).
bare() -> 42.
boot() -> entered(), 42.
file() ->
    entered(),
    Deadline = beam_time:deadline_after(5000),
    {ok, Bytes} = beam_file:read_file(<<"/etc/hosts">>, Deadline),
    true = byte_size(Bytes) > 0,
    42.
network() ->
    entered(),
    Deadline = beam_time:deadline_after(5000),
    {ok, Listener} = beam_socket:listen(#{addr => {127,0,0,1}, port => 0}, Deadline),
    {ok, #{port := Port}} = beam_socket:sockname(Listener),
    Parent = self(),
    spawn(fun() ->
        {ok, Socket} = beam_socket:connect(#{addr => {127,0,0,1}, port => Port}, Deadline),
        ok = beam_socket:send(Socket, <<42>>, Deadline),
        ok = beam_socket:close(Socket, Deadline), Parent ! client_done
    end),
    {ok, Accepted} = beam_socket:accept(Listener, Deadline),
    {ok, <<42>>} = beam_socket:recv(Accepted, 1, Deadline),
    ok = beam_socket:close(Accepted, Deadline),
    ok = beam_socket:close(Listener, Deadline),
    receive client_done -> 42 after 5000 -> error(client_timeout) end.
zlib() ->
    entered(),
    Stream = zlib:open(),
    ok = zlib:deflateInit(Stream),
    [_|_] = zlib:deflate(Stream, <<"deployment">>, finish),
    ok = zlib:deflateEnd(Stream), ok = zlib:close(Stream), 42.
program() ->
    entered(),
    program_work().
program_work() ->
    Port = open_port({spawn_executable, "/bin/echo"},
                     [binary, use_stdio, exit_status, {args, ["DEPLOY_PROGRAM"]}]),
    receive {Port, {data, <<"DEPLOY_PROGRAM\n">>}} -> ok after 5000 -> error(program_output) end,
    receive {Port, {exit_status, 0}} -> 42 after 5000 -> error(program_status) end.
otp() ->
    io:format("DEPLOY_ENTERED~n"),
    ["--runtime=beam", "-boot", "literal space", [26085,26412]] = init:get_plain_arguments(),
    ok = inet_db:set_lookup([native]),
    {ok, _} = inet:gethostbyname("localhost"),
    42 = program_work(),
    io:format("DEPLOY_OK~n"),
    erlang:halt(0).

entered() ->
    ok = beam_output:write(standard_output, <<"DEPLOY_ENTERED\n">>, infinity).
