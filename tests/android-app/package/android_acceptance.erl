%% SPDX-License-Identifier: Apache-2.0
-module(android_acceptance).
-export([exercise/3]).
exercise(Resource, Writable, Components) ->
    77 = android_shared:value(),
    undefined = whereis(android_acceptance_worker),
    Worker = spawn(fun worker/0),
    true = register(android_acceptance_worker, Worker),
    Ref = make_ref(), Worker ! {self(), Ref},
    receive {Ref, 99} -> ok after 2000 -> error(native_worker_timeout) end,
    Deadline = beam_time:deadline_after(4000),
    case Components band 1 of 0 -> ok; _ ->
    {ok, <<"Android product acceptance\n">>} = beam_file:read_file(Resource, Deadline),
    ok = beam_file:write_file(Writable, <<"file proof">>, Deadline),
    {ok, <<"file proof">>} = beam_file:read_file(Writable, Deadline),
    ok = beam_file:remove_file(Writable, Deadline)
    end,
    case Components band 4 of 0 -> ok; _ ->
    <<"zlib proof">> = zlib:uncompress(zlib:compress(<<"zlib proof">>))
    end,
    case Components band 2 of 0 -> ok; _ ->
    {ok, Listener} = beam_socket:listen(#{addr => {127,0,0,1}, port => 0}, Deadline),
    try
        {ok, #{port := Port}} = beam_socket:sockname(Listener),
        Parent = self(),
        {Peer, Monitor} = spawn_monitor(fun() ->
            {ok, Client} = beam_socket:connect(#{addr => {127,0,0,1}, port => Port}, Deadline),
            try <<"proof">> = gather(Client, 5, Deadline, <<>>)
            after beam_socket:close(Client, Deadline) end,
            Parent ! {self(), received}
        end),
        {ok, Accepted} = beam_socket:accept(Listener, Deadline),
        try ok = beam_socket:send(Accepted, <<"proof">>, Deadline)
        after beam_socket:close(Accepted, Deadline) end,
        receive {Peer, received} -> demonitor(Monitor,[flush]);
            {'DOWN',Monitor,process,Peer,Reason} -> error(Reason)
        after 4000 -> exit(Peer,kill),error(network_timeout) end
    after beam_socket:close(Listener, Deadline) end
    end,
    receive after 250 -> ok end,
    spawn(fun() -> receive after 100 -> beam_host:request(idle_event) end end),
    99.
worker() -> receive {From, Ref} -> From ! {Ref, android_native:value()}, worker() end.
gather(_,0,_,Bytes) -> Bytes;
gather(Socket,N,Deadline,Bytes) ->
    {ok,Part} = beam_socket:recv(Socket,N,Deadline),
    true = byte_size(Part)>0,
    gather(Socket,N-byte_size(Part),Deadline,<<Bytes/binary,Part/binary>>).
