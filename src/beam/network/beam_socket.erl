%% SPDX-License-Identifier: Apache-2.0
-module(beam_socket).
-export([start/0]).
-export([listen/2, accept/2, connect/2, send/3, recv/3, close/2, sockname/1]).

-spec listen(#{addr := {byte(), byte(), byte(), byte()}, port := non_neg_integer()},
             integer() | infinity) -> {ok, map()} | {error, map()}.
listen(#{addr := {A, B, C, D}, port := Port} = Address, Deadline)
  when is_integer(A), is_integer(B), is_integer(C), is_integer(D), A >= 0, A =< 255, B >= 0, B =< 255, C >= 0, C =< 255, D >= 0, D =< 255,
       is_integer(Port), Port >= 0, Port =< 65535, map_size(Address) =:= 2 ->
    case ready(Deadline) of
        {error, _} = Error -> Error;
        ok ->
            case socket:open(inet, stream, tcp) of
                {ok, Socket} ->
                    Sockaddr = #{family => inet, addr => {A, B, C, D}, port => Port},
                    case socket:bind(Socket, Sockaddr) of
                        ok ->
                            case socket:listen(Socket) of
                                ok -> {ok, handle(Socket)};
                                {error, Reason} ->
                                    _ = socket:close(Socket),
                                    {error, socket_error(Reason)}
                            end;
                        {error, Reason} ->
                            _ = socket:close(Socket),
                            {error, socket_error(Reason)}
                    end;
                {error, Reason} -> {error, socket_error(Reason)}
            end
    end;
listen(Address, Deadline) ->
    erlang:error(badarg, [Address, Deadline]).

-spec accept(map(), integer() | infinity) -> {ok, map()} | {error, map()}.
accept(Handle, Deadline) ->
    case owned(Handle, Deadline) of
        {ok, Socket} ->
            case socket:accept(Socket, timeout_ms(Deadline)) of
                {ok, Accepted} -> {ok, handle(Accepted)};
                {error, Reason} -> {error, socket_error(Reason)}
            end;
        {error, _} = Error -> Error
    end.

-spec connect(#{addr := {byte(), byte(), byte(), byte()}, port := non_neg_integer()},
              integer() | infinity) -> {ok, map()} | {error, map()}.
connect(#{addr := {A, B, C, D}, port := Port} = Address, Deadline)
  when is_integer(A), is_integer(B), is_integer(C), is_integer(D), A >= 0, A =< 255, B >= 0, B =< 255, C >= 0, C =< 255, D >= 0, D =< 255,
       is_integer(Port), Port >= 0, Port =< 65535, map_size(Address) =:= 2 ->
    case ready(Deadline) of
        {error, _} = Error -> Error;
        ok ->
            case socket:open(inet, stream, tcp) of
                {ok, Socket} ->
                    Sockaddr = #{family => inet, addr => {A, B, C, D}, port => Port},
                    case socket:connect(Socket, Sockaddr, timeout_ms(Deadline)) of
                        ok -> {ok, handle(Socket)};
                        {error, Reason} ->
                            _ = socket:close(Socket),
                            {error, socket_error(Reason)}
                    end;
                {error, Reason} -> {error, socket_error(Reason)}
            end
    end;
connect(Address, Deadline) ->
    erlang:error(badarg, [Address, Deadline]).

-spec send(map(), binary(), integer() | infinity) -> ok | {error, map()}.
send(Handle, Bytes, Deadline) when is_binary(Bytes) ->
    Result = case owned(Handle, Deadline, byte_size(Bytes) =:= 0) of
        {ok, _Socket} when Bytes =:= <<>> -> ok;
        {ok, Socket} -> send_all(Socket, Bytes, Deadline, 0);
        Error -> write_error(Error, 0)
    end,
    Result;
send(Handle, Bytes, Deadline) -> erlang:error(badarg, [Handle, Bytes, Deadline]).

send_all(Socket, Bytes, Deadline, Count) ->
    case expiry(Deadline, false) of
        {error, _} = Error -> write_error(Error, Count);
        ok ->
            case socket:send(Socket, Bytes, timeout_ms(Deadline)) of
                ok -> ok;
                {ok, <<>>} -> ok;
                {ok, Rest} when is_binary(Rest) ->
                    send_all(Socket, Rest, Deadline, Count + byte_size(Bytes) - byte_size(Rest));
                {error, {Reason, Rest}} when is_binary(Rest) ->
                    write_error({error, socket_error(Reason)}, Count + byte_size(Bytes) - byte_size(Rest));
                {error, Reason} -> write_error({error, socket_error(Reason)}, Count)
            end
    end.
write_error({error, Error}, Count) ->
    {error, Error#{bytes_written => Count, progress_unknown => false}}.

-spec recv(map(), non_neg_integer(), integer() | infinity) ->
          {ok, binary()} | eof | {error, map()}.
recv(Handle, Maximum, Deadline) when is_integer(Maximum), Maximum >= 0 ->
    case owned(Handle, Deadline, Maximum =:= 0) of
        {ok, _Socket} when Maximum =:= 0 -> {ok, <<>>};
        {ok, Socket} -> recv_some(Socket, min(Maximum, 65536), Deadline);
        Error -> Error
    end;
recv(Handle, Maximum, Deadline) -> erlang:error(badarg, [Handle, Maximum, Deadline]).

recv_some(Socket, Maximum, Deadline) ->
    case expiry(Deadline, false) of
        {error, _} = Error -> Error;
        ok -> recv_result(Socket, Maximum, Deadline, socket:recv(Socket, Maximum, nowait))
    end.
recv_result(_, _, _, {ok, Data}) when byte_size(Data) > 0 -> {ok, Data};
recv_result(_, _, _, {ok, <<>>}) -> eof;
recv_result(_, _, _, {error, {_, Data}}) when is_binary(Data), byte_size(Data) > 0 -> {ok, Data};
recv_result(_, _, _, {error, closed}) -> eof;
recv_result(_, _, _, {error, Reason}) -> {error, socket_error(Reason)};
recv_result(Socket, _, _, {Select, {Info, Data}})
  when (Select =:= select orelse Select =:= select_read), byte_size(Data) > 0 ->
    %% Native cancellation synchronously retires selection and drains its messages.
    _ = socket:cancel(Socket, Info),
    {ok, Data};
recv_result(Socket, Maximum, Deadline, {select, {select_info, _, Ref} = Info}) ->
    receive
        {'$socket', Socket, select, Ref} -> recv_some(Socket, Maximum, Deadline);
        {'$socket', Socket, abort, {Ref, Reason}} -> {error, socket_error(Reason)}
    after timeout_ms(Deadline) ->
        _ = socket:cancel(Socket, Info),
        {error, socket_error(timeout)}
    end.

-spec close(map(), integer() | infinity) -> ok | {error, map()}.
close(Handle, Deadline) ->
    case owned(Handle, Deadline, true) of
        {ok, Socket} ->
            case socket:close(Socket) of
                ok -> ok;
                {error, closed} -> ok;
                {error, Reason} -> {error, socket_error(Reason)}
            end;
        {error, #{kind := closed}} -> ok;
        Error -> Error
    end.

-spec sockname(map()) -> {ok, #{addr := tuple(), port := non_neg_integer()}} | {error, map()}.
sockname(Handle) ->
    case owned(Handle, infinity, true) of
        {ok, Socket} ->
            case socket:sockname(Socket) of
                {ok, #{addr := Addr, port := Port}} -> {ok, #{addr => Addr, port => Port}};
                {error, Reason} -> {error, socket_error(Reason)}
            end;
        Error -> Error
    end.

ready(Deadline) ->
    validate_deadline(Deadline),
    case selected(socket) of
        false -> {error, #{kind => unavailable, service => socket}};
        true -> expiry(Deadline, false)
    end.
expiry(_, true) -> ok;
expiry(Deadline, false) ->
    case beam_time:expired(Deadline) of
        true -> {error, #{kind => timeout, service => socket}};
        false -> ok
    end.
owned(Handle, Deadline) -> owned(Handle, Deadline, false).
owned(#{module := beam_socket, owner := Owner, socket := {'$socket', Ref} = Socket}, Deadline, IgnoreExpiry)
  when is_pid(Owner), is_reference(Ref) ->
    validate_deadline(Deadline),
    case selected(socket) of
        false -> {error, #{kind => unavailable, service => socket}};
        true ->
            case socket:getopt(Socket, otp, controlling_process) of
                {ok, Pid} when Pid =/= self() -> {error, #{kind => not_owner, service => socket}};
                {ok, _} ->
                    case expiry(Deadline, IgnoreExpiry) of ok -> {ok, Socket}; Error -> Error end;
                {error, closed} when Owner =/= self() -> {error, #{kind => not_owner, service => socket}};
                {error, Reason} -> {error, socket_error(Reason)}
            end
    end;
owned(_, Deadline, _) -> validate_deadline(Deadline), erlang:error(badarg).
validate_deadline(infinity) -> ok;
validate_deadline(D) when is_integer(D) -> ok;
validate_deadline(_) -> erlang:error(badarg).

handle(Socket) -> #{module => beam_socket, owner => self(), socket => Socket}.
timeout_ms(infinity) -> infinity;
timeout_ms(Deadline) ->
    Left = max(0, Deadline - erlang:monotonic_time(native)),
    Floor = erlang:convert_time_unit(Left, native, millisecond),
    %% Round upward without changing beam_time:remaining/1's public contract.
    case erlang:convert_time_unit(Floor, millisecond, native) < Left of
        true -> Floor + 1;
        false -> Floor
    end.
socket_error(Reason) ->
    Kind = case Reason of
        timeout -> timeout;
        closed -> closed;
        econnrefused -> io;
        eaddrinuse -> busy;
        eacces -> permission_denied;
        eperm -> permission_denied;
        einval -> invalid_data;
        ebadf -> closed;
        _ -> io
    end,
    Domain = case Reason of timeout -> socket; closed -> socket; _ -> posix end,
    #{kind => Kind, service => socket, native => #{domain => Domain, code => Reason}}.
selected(_Name) -> is_pid(whereis(socket_registry)).

%% The actual native owner is monitored by its library, independently of entry.
start() ->
    ok = prim_socket:on_load(),
    Parent = self(),
    {Watcher, Ref} = spawn_monitor(fun() ->
        Owner = whereis(socket_registry),
        Monitor = monitor(process, Owner),
        Parent ! {self(), ready},
        receive
            {'DOWN', Monitor, process, Owner, Reason} ->
                erts_internal:request_failure(socket, Reason)
        end
    end),
    receive
        {Watcher, ready} -> demonitor(Ref, [flush]), ok;
        {'DOWN', Ref, process, Watcher, Reason} -> exit(Reason)
    end.
