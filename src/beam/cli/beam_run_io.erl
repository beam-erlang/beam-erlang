%% SPDX-License-Identifier: Apache-2.0
%% Private CLI I/O protocol. Input collection runs separately so pending terminal
%% input does not block acknowledged output from other Erlang processes.
-module(beam_run_io).
-export([start/1,start/2]).

start(Destination) ->
    Context = persistent_term:get(beam_run_context, #{}),
    start(Destination, #{mode => run, terminal => false,
                         geometry => maps:get(geometry, Context, {80, 24})}).

start(Destination, Profile) ->
    Parent = self(),
    {Pid, Ref} = spawn_monitor(fun() ->
                                      put(io_profile, Profile),
                                      Reader = spawn_link(fun() ->
                                          put(io_profile, Profile),
                                          reader([], <<>>, false)
                                      end),
                                      Parent ! {io_ready, self()},
                                      loop(Destination, Reader,
                                           #{encoding => unicode, binary => false})
                              end),
    receive
        {io_ready, Pid} ->
            erlang:demonitor(Ref, [flush]),
            Pid;
        {'DOWN', Ref, process, Pid, Reason} ->
            erlang:error({io_start_failed, Reason})
    end.

loop(Destination, Reader, Options) ->
    receive
        {io_request, From, ReplyAs, Request} ->
            case request(Request, Destination, Reader, Options, From, ReplyAs) of
                {reply, Reply, Next} ->
                    From ! {io_reply, ReplyAs, Reply},
                    loop(Destination, Reader, Next);
                pending -> loop(Destination, Reader, Options)
            end;
        _ -> loop(Destination, Reader, Options)
    end.

request({put_chars, Encoding, Characters}, Destination, _, Options, _, _) ->
    {reply, write(Destination, Encoding, Characters, Options), Options};
request({put_chars, Encoding, Module, Function, Args}, Destination, _, Options, _, _) ->
    Reply = try write(Destination, Encoding, apply(Module, Function, Args), Options)
            catch _:_ -> {error, put_chars} end,
    {reply, Reply, Options};
request({put_chars, Characters}, Destination, Reader, Options, From, Ref) ->
    request({put_chars, latin1, Characters}, Destination, Reader, Options, From, Ref);
request({put_chars, Module, Function, Args}, Destination, Reader, Options, From, Ref) ->
    request({put_chars, latin1, Module, Function, Args}, Destination, Reader, Options, From, Ref);
request({requests, Requests}, _, _, Options, From, Ref) when is_list(Requests) ->
    %% is_list/1 also accepts an improper list. Validate the entire batch before
    %% sending any output, so its worker always has a reply for the caller.
    try length(Requests) of
        _ ->
            Leader = self(),
            spawn(fun() -> reply_requests(Leader, Requests, From, Ref) end),
            pending
    catch error:badarg -> {reply, {error, enotsup}, Options} end;
request(getopts, _, _, Options, _, _) ->
    {reply, maps:to_list(Options), Options};
request({setopts, Values}, _, _, Options, _, _) ->
    try setopts(Values, Options) of
        Next -> {reply, ok, Next}
    catch _:_ -> {reply, {error, enotsup}, Options} end;
request({get_geometry, Dimension}, _, _, Options, _, _) ->
    {Columns, Rows} = maps:get(geometry, get(io_profile), {80, 24}),
    Reply = case Dimension of columns -> Columns; rows -> Rows; _ -> {error, enotsup} end,
    {reply, Reply, Options};
request({get_until, Encoding, Prompt, Module, Function, Args}, Destination,
        Reader, Options, From, Ref) when Encoding =:= unicode; Encoding =:= latin1 ->
    case maps:get(mode, get(io_profile)) =:= interactive andalso
         collector_allowed(Module, Function, Args) of
        true -> input(Reader, Destination, Encoding, Prompt,
                      {get_until, {Module, Function, Args}}, Options, From, Ref);
        false -> {reply, {error, enotsup}, Options}
    end;
request({get_until, Prompt, Module, Function, Args}, Destination, Reader, Options, From, Ref) ->
    request({get_until, latin1, Prompt, Module, Function, Args},
            Destination, Reader, Options, From, Ref);
request({fread, Prompt, Format}, Destination, Reader, Options, From, Ref) ->
    request({get_until, unicode, Prompt, io_lib, fread, [Format]},
            Destination, Reader, Options, From, Ref);
request({get_line, Encoding, Prompt}, Destination, Reader, Options, From, Ref) ->
    input(Reader, Destination, Encoding, Prompt, {collect_line, []}, Options, From, Ref);
request({get_chars, Encoding, Prompt, Count}, Destination, Reader, Options, From, Ref)
  when is_integer(Count), Count >= 0 ->
    input(Reader, Destination, Encoding, Prompt, {collect_chars, Count}, Options, From, Ref);
request({get_line, Prompt}, Destination, Reader, Options, From, Ref) ->
    request({get_line, latin1, Prompt}, Destination, Reader, Options, From, Ref);
request({get_chars, Prompt, Count}, Destination, Reader, Options, From, Ref) ->
    request({get_chars, latin1, Prompt, Count}, Destination, Reader, Options, From, Ref);
request(_, _, _, Options, _, _) -> {reply, {error, enotsup}, Options}.

collector_allowed(erl_scan, tokens, [_]) -> true;
collector_allowed(erl_scan, tokens, [_, Options]) when is_list(Options) -> true;
collector_allowed(io_lib, fread, [_]) -> true;
collector_allowed(_, _, _) -> false.

reply_requests(Leader, Requests, From, Ref) ->
    Monitor = monitor(process, Leader),
    Reply = try requests(Leader, Monitor, Requests, ok)
            catch _:_ -> {error, enotsup}
            after demonitor(Monitor, [flush]) end,
    From ! {io_reply, Ref, Reply}.

requests(_, _, [], Reply) -> Reply;
requests(Leader, Monitor, [Request | Rest], _) ->
    Ref = make_ref(),
    Leader ! {io_request, self(), Ref, Request},
    receive
        {io_reply, Ref, {error, _} = Error} -> Error;
        {io_reply, Ref, Reply} -> requests(Leader, Monitor, Rest, Reply);
        {'DOWN', Monitor, process, Leader, _} -> {error, terminated}
    end.

setopts([], Options) -> Options;
setopts([{encoding, Encoding} | Rest], Options)
  when Encoding =:= unicode; Encoding =:= latin1 ->
    setopts(Rest, Options#{encoding => Encoding});
setopts([{encoding, utf8} | Rest], Options) ->
    setopts(Rest, Options#{encoding => unicode});
setopts([{Key, Value} | Rest], Options)
  when Key =:= binary, is_boolean(Value) ->
    setopts(Rest, Options#{Key => Value});
setopts([binary | Rest], Options) -> setopts(Rest, Options#{binary => true});
setopts([list | Rest], Options) -> setopts(Rest, Options#{binary => false}).

write(Destination, Encoding, Characters, #{encoding := DeviceEncoding}) ->
    try unicode:characters_to_binary(Characters, Encoding, utf8) of
        Binary when is_binary(Binary) ->
            case unicode:characters_to_binary(Binary, utf8, DeviceEncoding) of
                Encoded when is_binary(Encoded) ->
                    %% The host transport is always UTF-8. The device encoding
                    %% still defines which characters the device can represent.
                    case maps:get(transport, get(io_profile), boot) of
                        host ->
                            case beam_host:request({output, Destination, Binary}) of
                                {ok, Reply} -> Reply;
                                _ -> {error, io}
                            end;
                        boot -> beam_output:write(Destination, Binary, infinity)
                    end;
                _ -> {error, {no_translation, Encoding, DeviceEncoding}}
            end;
        _ -> {error, put_chars}
    catch _:_ -> {error, put_chars} end.

input(Reader, Destination, Encoding, Prompt, Collector, Options, From, Ref) ->
    Reader ! {read, Destination, Encoding, Prompt, Collector, Options, From, Ref},
    pending.

reader(Buffer, Pending, Eof) ->
    receive
        {read, Destination, Encoding, Prompt, Collector, Options, From, Ref} ->
            case write(Destination, unicode, io_lib:format_prompt(Prompt, Encoding), Options) of
                ok ->
                    InputEncoding = maps:get(encoding, Options),
                    try collect(Collector, start, Buffer, Pending, Eof, Encoding, InputEncoding) of
                        {Reply0, Rest, NextPending, AtEof} ->
                            Reply = input_format(Reply0, Encoding, Options),
                            From ! {io_reply, Ref, Reply},
                            reader(Rest, NextPending, AtEof)
                    catch
                        error:badarg ->
                            From ! {io_reply, Ref, {error, invalid_unicode}},
                            reader([], <<>>, Eof);
                        throw:{collector_error, Reason} ->
                            From ! {io_reply, Ref, {error, {collector, Reason}}},
                            reader([], <<>>, Eof)
                    end;
                Error ->
                    From ! {io_reply, Ref, Error},
                    reader(Buffer, Pending, Eof)
            end
    end.

collect({collect_chars, 0}, start, Buffer, Pending, Eof, _, _) ->
    {[], Buffer, Pending, Eof};
collect(Collector, Continuation, [], Pending, Eof, Encoding, InputEncoding)
  when Pending =/= <<>>; not Eof ->
    case input_chars(Pending, Eof, InputEncoding) of
        {error, Reason, Rest, AtEof} -> {{error, Reason}, [], Rest, AtEof};
        {error, Reason, AtEof} -> {{error, Reason}, [], <<>>, AtEof};
        {Chars, NextPending, AtEof} ->
            collect(Collector, Continuation, Chars, NextPending, AtEof,
                    Encoding, InputEncoding)
    end;
collect({Function, Extra} = Collector, Continuation, Buffer, Pending, Eof,
        Encoding, InputEncoding) ->
    Data = case {Buffer, Eof} of {[], true} -> eof; _ -> Buffer end,
    case collect_call(Function, Continuation, Data, Encoding, Extra) of
        {stop, Reply, Rest} ->
            {Reply, case Rest of eof -> []; _ -> Rest end, Pending, Eof};
        Next -> collect(Collector, Next, [], Pending, Eof, Encoding, InputEncoding)
    end.

collect_call(get_until, Continuation, Data, Encoding, Extra) ->
    try io_lib:get_until(Continuation, Data, Encoding, Extra)
    catch Class:Reason -> throw({collector_error, {Class, Reason}}) end;
collect_call(Function, Continuation, Data, Encoding, Extra) ->
    apply(io_lib, Function, [Continuation, Data, Encoding, Extra]).

input_chars({invalid_unicode, Rest}, Eof, _) ->
    {error, invalid_unicode, {discard_line, Rest}, Eof};
input_chars({discard_line, Rest}, Eof, Encoding) ->
    discard_line(Rest, Eof, Encoding);
input_chars(invalid_unicode, Eof, _) ->
    {error, invalid_unicode, Eof};
input_chars(Bytes, Eof, Encoding) ->
    case unicode:characters_to_list(Bytes, Encoding) of
        [] when Eof -> {[], <<>>, true};
        [] -> read_input(<<>>, Encoding);
        Chars when is_list(Chars) -> {Chars, <<>>, Eof};
        {incomplete, [], _} when Eof -> {error, invalid_unicode, true};
        {incomplete, [], Rest} -> read_input(Rest, Encoding);
        {incomplete, Chars, Rest} -> {Chars, Rest, Eof};
        {error, [], Rest} ->
            case maps:get(recover_lines, get(io_profile), false) of
                true -> {error, invalid_unicode, {discard_line, Rest}, Eof};
                false -> {error, invalid_unicode, Eof}
            end;
        {error, Chars, Rest} ->
            Pending = case maps:get(recover_lines, get(io_profile), false) of
                          true -> {invalid_unicode, Rest};
                          false -> invalid_unicode
                      end,
            {Chars, Pending, Eof}
    end.

%% A shell decoding error invalidates its current input line, not later
%% commands already supplied in the same host chunk. Recovery stays in the
%% reader so split malformed lines cannot become executable fragments.
discard_line(Bytes, Eof, Encoding) ->
    case binary:match(Bytes, <<"\n">>) of
        {Position, 1} ->
            <<_:Position/binary, $\n, Rest/binary>> = Bytes,
            input_chars(Rest, Eof, Encoding);
        nomatch when Eof -> {[], <<>>, true};
        nomatch ->
            case beam_host:request(read_line) of
                {ok, pending} ->
                    receive after 1 -> discard_line(<<>>, false, Encoding) end;
                {ok, eof} -> {[], <<>>, true};
                {ok, {ok, Next}} -> discard_line(Next, false, Encoding);
                _ -> {error, terminated, true}
            end
    end.

read_input(Pending, Encoding) ->
    case beam_host:request(read_line) of
        {ok, pending} ->
            receive after 1 -> read_input(Pending, Encoding) end;
        {ok, eof} -> input_chars(Pending, true, Encoding);
        {ok, {ok, <<3>>}} when Pending =:= <<>> ->
            case maps:get(terminal, get(io_profile), false) of
                true -> {error, interrupted, false};
                false -> input_chars(<<3>>, false, Encoding)
            end;
        {ok, {ok, Bytes}} ->
            input_chars(<<Pending/binary, Bytes/binary>>, false, Encoding);
        _ -> {error, terminated, true}
    end.

input_format(Reply, Encoding, #{binary := Binary}) when is_list(Reply) ->
    case unicode:characters_to_binary(Reply, unicode, Encoding) of
        Bytes when is_binary(Bytes), Binary -> Bytes;
        Bytes when is_binary(Bytes) -> Reply;
        _ -> {error, {no_translation, unicode, Encoding}}
    end;
input_format(Reply, _, _) -> Reply.
