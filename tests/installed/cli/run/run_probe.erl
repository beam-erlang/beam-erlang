%% SPDX-License-Identifier: Apache-2.0
-module(run_probe).
-export([main/1]).

main(["probe", Root, Source]) ->
    absence(),
    {ok, Services} = beam_boot:services(),
    true = same_members(Services, [output,code,file,socket,zlib,program]),
    Origins = persistent_term:get(beam_boot_origins),
    SourceBytes = utf8(Source),
    SourceBytes = origin(?MODULE, Origins),
    RootBytes = utf8(Root),
    Runner = <<RootBytes/binary, "/lib/beam/runtime/beam/run/ebin/">>,
    RunnerEntry = <<Runner/binary, "beam_source_run.beam">>,
    RunnerEntry = origin(beam_source_run, Origins),
    RunnerIO = <<RootBytes/binary, "/lib/beam/runtime/beam/cli/ebin/io.beam">>,
    RunnerIO = origin(io, Origins),
    lists:foreach(fun(Module) ->
        case member(Module, erlang:pre_loaded()) of
            true -> ok;
            false -> io:format("origin ~s ~ts~n", [Module, origin(Module, Origins)])
        end
    end, erlang:loaded()),
    io:format("probe ok~n");
main(["io"]) ->
    ok = io:format("Unicode ~ts ~p ~p~n", [<<"日本語 λ"/utf8>>, 42, {ok,[1,2]}]),
    ok = io:format(standard_error, "error ~ts~n", [<<"é"/utf8>>]),
    Parent = self(),
    {Child, Ref} = spawn_monitor(fun() ->
        ok = io:put_chars("child output\n"), Parent ! child_done
    end),
    receive child_done -> ok after 3000 -> error(child_output_timeout) end,
    receive {'DOWN',Ref,process,Child,normal} -> ok after 3000 -> error(child_exit_timeout) end;
main(["input"]) ->
    spawn(fun() ->
        receive after 30 -> ok end,
        ok = io:put_chars("reader live\n")
    end),
    "日本語\n" = io:get_line("prompt>"),
    "ab" = io:get_chars("", 2),
    "c\n" = io:get_line(""),
    eof = io:get_line(""),
    io:put_chars("input ok\n");
main(["io_options"]) ->
    {ok,80} = io:columns(),
    {ok,24} = io:rows(),
    #{encoding := unicode,binary := false} = maps:from_list(io:getopts()),
    {error,enotsup} = io:request({user,node()}, getopts),
    {error,enotsup} = io:setopts([{unsupported,true}]),
    {error,enotsup} = io:read(""),
    {error,enotsup} = io:fread("", "~d"),
    {error,enotsup} = io:scan_erl_exprs(""),
    false = erlang:module_loaded(erl_parse),
    false = erlang:module_loaded(erl_eval),
    {error,enotsup} = io:request({requests,[{put_chars,unicode,"must not print"}|invalid_tail]}),
    badarg = try io:format(#{}, "must not print", []) catch error:badarg -> badarg end,
    badarg = try io:format(invalid_format(), [one]) catch error:badarg -> badarg end,
    ok = io:setopts([binary,{encoding,latin1}]),
    <<>> = io:get_chars("", 0),
    <<"é\n"/utf8>> = io:get_line(""),
    ok = io:put_chars("é\n"),
    no_translation = try io:put_chars("λ") catch error:no_translation -> no_translation end,
    ok = io:setopts([list,{encoding,utf8}]),
    [] = io:get_chars("", 0),
    eof = io:get_line(""),
    io:put_chars("options ok\n");
main(["input_chunks"]) ->
    "A" = io:get_chars("", 1),
    io:put_chars("ascii\n"),
    [3] = io:get_chars("", 1),
    io:put_chars("control\n"),
    "λ" = io:get_chars("", 1),
    io:put_chars("unicode\n"),
    "X" = io:get_chars("", 1),
    "Y" = io:get_chars("", 1),
    io:put_chars("leftover\n"),
    "line\n" = io:get_line(""),
    "crlf\n" = io:get_line(""),
    io:put_chars("lines\n"),
    "Z" = io:get_chars("", 1),
    io:put_chars("prefix\n"),
    {error,invalid_unicode} = io:get_chars("", 1),
    io:put_chars("invalid\n"),
    "Q" = io:get_chars("", 1),
    io:put_chars("tail prefix\n"),
    {error,invalid_unicode} = io:get_chars("", 1),
    eof = io:get_chars("", 1),
    io:put_chars("chunks ok\n");
main(["literal", "-eval", "erlang:halt(42).", "--", "", "two words", "a'b", "+S", "日本語"]) ->
    io:put_chars("literal ok\n");
main(["services", Path]) ->
    absence(),
    file_roundtrip(utf8(Path)),
    socket_roundtrip(),
    Bytes = <<"compress 日本語"/utf8>>,
    Bytes = zlib:uncompress(zlib:compress(Bytes)),
    program_roundtrip(),
    io:put_chars("services ok\n");
main(["output_only"]) ->
    absence(),
    {ok,[output]} = beam_boot:services(),
    lists:foreach(fun(Module) -> false = erlang:module_loaded(Module) end,
                  [beam_file,prim_file,prim_buffer,beam_socket,prim_socket,zlib]),
    [] = erlang:ports(),
    #{answer := 42} = maps:from_list([{answer,42}]),
    [1,2] = lists:reverse([2,1]),
    io:put_chars("output only\n"),
    <<"copied runner result">>;
main(["clean"]) ->
    absent = persistent_term:get(run_retained_state, absent),
    undefined = whereis(run_retained_worker),
    undefined = ets:whereis(run_retained_table),
    <<"copied runner result">>;
main(["retain", Path, Ending]) ->
    retain(Path, Ending, true);
main(["retain_sleep", Path, Ending]) ->
    retain(Path, Ending, sleep);
main(["retain_unused_program", Path, Ending]) ->
    retain(Path, Ending, false);
main(["kill_output"]) ->
    exit(whereis(beam_service_output), kill),
    receive after 3000 -> error(owner_death_not_observed) end;
main(["kill_io_leader"]) ->
    exit(group_leader(), kill),
    receive after 3000 -> error(io_owner_death_not_observed) end;
main(["kill_io_reader"]) ->
    {links,[Reader]} = process_info(group_leader(), links),
    exit(Reader, kill),
    receive after 3000 -> error(reader_death_not_observed) end;
main(["exception"]) ->
    error(source_run_exception);
main(["halt"]) ->
    erlang:halt(7);
main(["finish"]) ->
    beam_boot:finish(6).

retain(Path, Ending, UseProgram) ->
    absent = persistent_term:get(run_retained_state, absent),
    undefined = whereis(run_retained_worker),
    undefined = ets:whereis(run_retained_table),
    Parent = self(),
    Worker = spawn(fun() -> retain_resources(utf8(Path), Parent, UseProgram) end),
    true = register(run_retained_worker, Worker),
    ProgramPid = receive {resources_ready, Pid} -> Pid
                 after 3000 -> error(resource_timeout) end,
    case ProgramPid of 0 -> ok; _ -> io:format("child ~p~n", [ProgramPid]) end,
    io:put_chars("resources held\n"),
    finish(Ending).

absence() ->
    true = same_members(erlang:pre_loaded(),
        [erlang,erts_internal,erts_code_purger,erts_literal_area_collector,
         erts_trace_cleaner,erts_dirty_process_signal_handler,atomics,counters,persistent_term]),
    lists:foreach(fun(Name) -> undefined = whereis(Name) end,
        [code_server,file_server_2,application_controller,inet_db,erl_prim_loader]),
    undefined = ets:whereis(ac_tab),
    lists:foreach(fun(Module) -> false = erlang:module_loaded(Module) end,
        [application,application_controller,code,code_server,file,file_server,
         gen,gen_server,supervisor,inet,inet_db,inet_gethost_native,os,
         compile,epp,beam_compiler_cli,beam_compiler_engine,beam_cli,beam_cli_io,
         beam_run,beam_compile,shell,erl_eval,erl_parse,init,erl_prim_loader]),
    ok.

file_roundtrip(Path) ->
    Payload = <<"file 日本語\n"/utf8>>,
    ok = beam_file:write_file(Path, Payload, infinity),
    {ok, Payload} = beam_file:read_file(Path, infinity),
    {ok, Handle} = beam_file:open(Path, infinity, #{mode => read}),
    {ok, Payload} = beam_file:read(Handle, byte_size(Payload), infinity),
    Parent = self(),
    spawn(fun() -> Parent ! {not_owner,beam_file:read(Handle,1,infinity)} end),
    receive {not_owner,{error,#{kind := not_owner}}} -> ok
    after 3000 -> error(file_ownership_timeout) end,
    ok = beam_file:close(Handle, infinity),
    {error,#{kind := not_found}} = beam_file:read_file(<<Path/binary,".missing">>, infinity),
    {error,#{kind := invalid_data}} = beam_file:read_file(<<Path/binary,0>>, infinity),
    {error,#{kind := invalid_data}} = beam_file:open(<<Path/binary,16#ff>>, infinity, #{mode => read}).

socket_roundtrip() ->
    Deadline = beam_time:deadline_after(3000),
    {ok, Listen} = beam_socket:listen(#{addr => {127,0,0,1},port => 0}, Deadline),
    {ok, #{port := Port}} = beam_socket:sockname(Listen),
    Parent = self(),
    {Child, Ref} = spawn_monitor(fun() ->
        {ok, Socket} = beam_socket:connect(#{addr => {127,0,0,1},port => Port}, Deadline),
        ok = beam_socket:send(Socket, <<"ping">>, Deadline),
        <<"pong">> = recv_bytes(Socket, 4, <<>>, Deadline),
        ok = beam_socket:close(Socket, Deadline),
        Parent ! socket_done
    end),
    {ok, Accepted} = beam_socket:accept(Listen, Deadline),
    <<"ping">> = recv_bytes(Accepted, 4, <<>>, Deadline),
    ok = beam_socket:send(Accepted, <<"pong">>, Deadline),
    ok = beam_socket:close(Accepted, Deadline),
    ok = beam_socket:close(Listen, Deadline),
    receive socket_done -> ok after 3000 -> error(socket_timeout) end,
    receive {'DOWN',Ref,process,Child,normal} -> ok after 3000 -> error(socket_exit_timeout) end.

recv_bytes(_Socket, 0, Bytes, _Deadline) -> Bytes;
recv_bytes(Socket, Remaining, Bytes, Deadline) ->
    {ok, Part} = beam_socket:recv(Socket, Remaining, Deadline),
    recv_bytes(Socket, Remaining-byte_size(Part), <<Bytes/binary,Part/binary>>, Deadline).

program_roundtrip() ->
    Port = erlang:open_port({spawn_executable,"/bin/sh"},
        [binary,use_stdio,exit_status,{args,["-c","printf program-ok"]}]),
    <<"program-ok">> = collect_program(Port, <<>>).

collect_program(Port, Bytes) ->
    receive
        {Port,{data,Part}} -> collect_program(Port, <<Bytes/binary,Part/binary>>);
        {Port,{exit_status,0}} -> Bytes
    after 3000 -> error(program_timeout)
    end.

retain_resources(Path, Parent, UseProgram) ->
    {ok, File} = beam_file:open(Path, infinity, #{mode => write}),
    ok = beam_file:write(File, <<"held">>, infinity),
    {ok, Socket} = beam_socket:listen(#{addr => {127,0,0,1},port => 0}, infinity),
    Stream = zlib:open(),
    ok = zlib:deflateInit(Stream),
    Port = retain_program(UseProgram),
    Table = ets:new(run_retained_table, [named_table,public]),
    true = ets:insert(Table, {compiler_state,retained}),
    persistent_term:put(run_retained_state, {File,Socket,Stream,Table,Port}),
    ProgramPid = case Port of unused -> 0; _ -> {os_pid,Pid} = erlang:port_info(Port,os_pid), Pid end,
    Parent ! {resources_ready,ProgramPid},
    receive release -> ok end.

retain_program(false) -> unused;
retain_program(sleep) ->
    erlang:open_port({spawn_executable,"/bin/sleep"},
                     [binary,use_stdio,{args,["60"]}]);
retain_program(true) ->
    Port = erlang:open_port({spawn_executable,"/bin/cat"}, [binary,use_stdio,exit_status]),
    true = erlang:port_command(Port, <<"ready\n">>),
    <<"ready\n">> = program_ready(Port, <<>>),
    Port.

program_ready(_Port, Bytes) when byte_size(Bytes) >= 6 -> Bytes;
program_ready(Port, Bytes) ->
    receive {Port,{data,Part}} -> program_ready(Port, <<Bytes/binary,Part/binary>>)
    after 3000 -> error(program_readiness_timeout) end.

finish("return") -> <<"copied runner result">>;
finish("halt") -> erlang:halt(7);
finish("finish") -> beam_boot:finish(6);
finish("exception") -> error(source_run_exception).

origin(Module, [{Module,Origin}|_]) -> Origin;
origin(Module, [_|Rest]) -> origin(Module, Rest);
origin(Module, []) -> error({missing_origin,Module}).

member(_, []) -> false;
member(Value, [Value|_]) -> true;
member(Value, [_|Rest]) -> member(Value, Rest).
same_members(Left, Right) ->
    lists:sort(Left) =:= lists:sort(Right).
utf8(Characters) -> << <<Codepoint/utf8>> || Codepoint <- Characters >>.
invalid_format() -> "~p ~p".
