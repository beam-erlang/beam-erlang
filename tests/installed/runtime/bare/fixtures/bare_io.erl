%% SPDX-License-Identifier: Apache-2.0
-module(bare_io).
-export([hello/1, failed_output/0, bad_format/0]).

hello(Name) ->
    Leader = spawn(fun io_loop/0),
    true = group_leader(Leader, self()),
    ok = io:format("Hello, ~ts!~n", [Name]),
    Leader ! stop,
    ok.

failed_output() ->
    Leader = spawn(fun io_loop/0),
    true = group_leader(Leader, self()),
    Result = try io:put_chars("failed output") of
        _ -> unexpected
    catch error:_ -> ok end,
    Leader ! stop,
    Result.

bad_format() ->
    Leader = spawn(fun io_loop/0),
    true = group_leader(Leader, self()),
    Result = try io:format("~s", [{not_a_string}]) of
        _ -> unexpected
    catch error:_ -> ok end,
    Leader ! stop,
    Result.

io_loop() ->
    receive
        {io_request, From, ReplyAs, Request} ->
            Reply = try write_request(Request) catch _:Reason -> {error, Reason} end,
            From ! {io_reply, ReplyAs, Reply},
            io_loop();
        stop -> ok
    end.

write_request({put_chars, Encoding, Module, Function, Args}) ->
    write_request({put_chars, Encoding, apply(Module, Function, Args)});
write_request({put_chars, Encoding, Chars}) ->
    Bytes = unicode:characters_to_binary(Chars, Encoding, utf8),
    case beam_host:request({write, Bytes}) of
        {ok, ok} -> ok;
        {error, Reason} -> {error, Reason}
    end;
write_request({put_chars, Chars}) ->
    write_request({put_chars, latin1, Chars});
write_request({put_chars, Module, Function, Args}) ->
    write_request({put_chars, latin1, Module, Function, Args});
write_request({requests, Requests}) -> write_requests(Requests);
write_request(_) -> {error, request}.

write_requests([]) -> ok;
write_requests([Request | Rest]) ->
    case write_request(Request) of
        ok -> write_requests(Rest);
        Error -> Error
    end.
