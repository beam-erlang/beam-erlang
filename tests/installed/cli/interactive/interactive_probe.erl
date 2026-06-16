%% SPDX-License-Identifier: Apache-2.0
-module(interactive_probe).
-export([value/0, clean/0, retain/1]).

value() -> 42.

clean() ->
    absent = persistent_term:get(interactive_retained_state, absent),
    undefined = whereis(interactive_retained_worker),
    undefined = ets:whereis(interactive_retained_table),
    <<"copied interactive result">>.

retain(Path) ->
    clean(),
    Parent = self(),
    Worker = spawn(fun() ->
        {ok, File} = beam_file:open(Path, infinity, #{mode => write}),
        ok = beam_file:write(File, <<"held">>, infinity),
        {ok, Socket} = beam_socket:listen(#{addr => {127,0,0,1}, port => 0}, infinity),
        Stream = zlib:open(),
        ok = zlib:deflateInit(Stream),
        Port = erlang:open_port({spawn_executable,"/bin/sleep"},
            [binary,use_stdio,{args,["60"]}]),
        {os_pid, Pid} = erlang:port_info(Port, os_pid),
        Table = ets:new(interactive_retained_table, [named_table,public]),
        true = ets:insert(Table, {owner,self()}),
        persistent_term:put(interactive_retained_state, {File,Socket,Stream,Port,Table}),
        Parent ! {ready,Pid},
        receive stop -> ok end
    end),
    true = register(interactive_retained_worker, Worker),
    receive {ready,Pid} -> io:format("child ~p~ninteractive resources held~n", [Pid])
    after 3000 -> error(resource_timeout)
    end,
    <<"copied interactive result">>.
