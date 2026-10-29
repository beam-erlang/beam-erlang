%% SPDX-License-Identifier: Apache-2.0
%% Ordinary entry-scoped application policy above activated VM facilities.
-module(beam_return_boot).
-export([start/3]).

start({Module, Function}, Arguments, OriginSeed)
  when is_atom(Module), is_atom(Function), is_list(Arguments) ->
    seed_origins(OriginSeed),
    persistent_term:put(beam_returning_boot, true),
    case persistent_term:get(beam_boot_services, undefined) of
        undefined -> persistent_term:put(beam_boot_services, []);
        _ -> ok
    end,
    Leader = spawn(fun leader/0),
    erlang:group_leader(Leader, self()),
    apply(Module, Function, Arguments);
start(Entry, Arguments, OriginSeed) -> erlang:error(badarg, [Entry, Arguments, OriginSeed]).

%% Activation already published origins when it ran; preserve its updates.
seed_origins(undefined) -> ok;
seed_origins(Origins) when is_list(Origins) ->
    case persistent_term:get(beam_boot_origins, undefined) of
        undefined -> persistent_term:put(beam_boot_origins, Origins);
        _ -> ok
    end.

leader() ->
    receive
        {io_request, From, ReplyAs, Request} ->
            _ = From ! {io_reply, ReplyAs, io_request(Request)},
            leader();
        _ ->
            leader()
    end.

io_request({put_chars, _Encoding, Characters}) ->
    write_chars(Characters);
io_request({put_chars, _Encoding, Module, Function, Args}) ->
    write_chars(apply(Module, Function, Args));
io_request({requests, Requests}) ->
    io_requests(Requests);
io_request(_Request) ->
    {error, enodev}.

io_requests([]) ->
    ok;
io_requests([Request | Rest]) ->
    case io_request(Request) of
        ok -> io_requests(Rest);
        Other -> Other
    end.

write_chars(Characters) ->
    case whereis(beam_output_server) of
        undefined -> {error, enodev};
        _ ->
            case beam_output:write(standard_output, characters(Characters), infinity) of
                ok -> ok;
                {error, _} -> {error, terminated}
            end
    end.

characters(Binary) when is_binary(Binary) ->
    Binary;
characters(List) when is_list(List) ->
    characters(List, <<>>);
characters(Other) ->
    erlang:error(badarg, [Other]).

characters([], Acc) ->
    Acc;
characters([Byte | Rest], Acc) when is_integer(Byte), Byte >= 0, Byte =< 255 ->
    characters(Rest, <<Acc/binary, Byte>>);
characters([Binary | Rest], Acc) when is_binary(Binary) ->
    characters(Rest, <<Acc/binary, Binary/binary>>);
characters([List | Rest], Acc) when is_list(List) ->
    characters(Rest, <<Acc/binary, (characters(List))/binary>>);
characters([Code | Rest], Acc) when is_integer(Code), Code > 255, Code =< 16#10FFFF ->
    characters(Rest, <<Acc/binary, (utf8(Code))/binary>>).

utf8(Code) when Code < 16#80 ->
    <<Code>>;
utf8(Code) when Code < 16#800 ->
    <<(16#C0 bor (Code bsr 6)), (16#80 bor (Code band 16#3F))>>;
utf8(Code) when Code < 16#10000 ->
    <<(16#E0 bor (Code bsr 12)),
      (16#80 bor ((Code bsr 6) band 16#3F)),
      (16#80 bor (Code band 16#3F))>>;
utf8(Code) ->
    <<(16#F0 bor (Code bsr 18)),
      (16#80 bor ((Code bsr 12) band 16#3F)),
      (16#80 bor ((Code bsr 6) band 16#3F)),
      (16#80 bor (Code band 16#3F))>>.
