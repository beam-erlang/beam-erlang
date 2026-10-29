%% SPDX-License-Identifier: Apache-2.0
-module(component_worker).
-export([start/3, arm/0, count/0, fail_file/0, fail_socket/0,
         compress/0, passive/0, on_load/0, activated/0, support_ready/0]).

start(File, Network, Path) ->
    undefined = whereis(component_worker),
    undefined = persistent_term:get(component_resource, undefined),
    Parent = self(),
    {Pid, Ref} = spawn_monitor(fun() -> init(Parent, File, Network, Path) end),
    receive
        {Pid, ready, Port} -> demonitor(Ref, [flush]), Port;
        {'DOWN', Ref, process, Pid, Reason} -> error(Reason)
    after 5000 -> error(worker_timeout)
    end.

init(Parent, File, Network, Path) ->
    register(component_worker, self()),
    Fd = case File of
             1 -> {ok, F} = beam_file:open(Path, infinity, #{mode => write}), F;
             0 -> undefined
         end,
    {Listener, Port} = case Network of
        1 ->
            {ok, L} = beam_socket:listen(#{addr => {127,0,0,1}, port => 0}, infinity),
            {ok, #{port := P}} = beam_socket:sockname(L),
            {L, P};
        0 -> {undefined, 0}
    end,
    persistent_term:put(component_resource, {Fd, Listener}),
    Parent ! {self(), ready, Port},
    loop(Fd, Listener, 0).

loop(Fd, Listener, Count) ->
    receive
        {From, Ref, arm} ->
            case Fd of
                undefined -> ok;
                _ -> ok = beam_file:write(Fd, <<"retained\n">>, infinity)
            end,
            From ! {Ref, Count + 1},
            case Listener of
                undefined -> ok;
                _ ->
                    Deadline = beam_time:deadline_after(5000),
                    {ok, Peer} = beam_socket:accept(Listener, Deadline),
                    <<"ping">> = read_exact(Peer, 4, Deadline, <<>>),
                    ok = beam_socket:send(Peer, <<"pong">>, Deadline),
                    ok = beam_socket:close(Peer, Deadline)
            end,
            loop(Fd, Listener, Count + 1);
        {From, Ref, count} ->
            From ! {Ref, Count},
            loop(Fd, Listener, Count)
    end.

arm() -> request(arm).
count() -> request(count).
request(Op) ->
    Ref = make_ref(),
    component_worker ! {self(), Ref, Op},
    receive {Ref, Count} -> Count after 6000 -> error(worker_timeout) end.

fail_file() -> exit(whereis(beam_file_owner), kill), wait_failure().
fail_socket() -> exit(whereis(socket_registry), kill), wait_failure().
wait_failure() -> receive after 5000 -> error(component_did_not_stop) end.

%% TCP reads acknowledge available bytes, so the protocol owns framing.
read_exact(_, 0, _, Acc) -> Acc;
read_exact(Socket, Remaining, Deadline, Acc) ->
    case beam_socket:recv(Socket, Remaining, Deadline) of
        {ok, Part} -> read_exact(Socket, Remaining - byte_size(Part), Deadline, <<Acc/binary, Part/binary>>);
        Other -> error({incomplete_request, Other})
    end.


passive() ->
    false = is_pid(whereis(beam_output_server)),
    false = persistent_term:get(beam_code_enabled, false),
    undefined = whereis(beam_output_server),
    undefined = whereis(beam_file_owner),
    undefined = whereis(socket_registry),
    undefined = persistent_term:get(beam_boot_origins, undefined),
    false = erlang:module_loaded(beam_boot),
    false = erlang:module_loaded(beam_file),
    false = erlang:module_loaded(beam_socket),
    false = erlang:module_loaded(zlib),
    9 = length(erlang:pre_loaded()),
    {error, #{kind := unavailable, service := output}} = beam_output:write(standard_output, <<"unused">>, infinity),
    {error, #{kind := unavailable, service := code}} = beam_code:fetch(<<"unused">>, ?MODULE, infinity),
    42.

on_load() -> persistent_term:put(activation_probe, true), ok.
activated() -> true = persistent_term:get(activation_probe), 42.
support_ready() -> true = is_pid(whereis(beam_output_server)), 42.

compress() ->
    Bytes = <<"combined file, socket and compression lifetime">>,
    Bytes = zlib:gunzip(zlib:gzip(Bytes)),
    Stream = zlib:open(),
    ok = zlib:deflateInit(Stream),
    persistent_term:put(combined_stream, Stream),
    42.
