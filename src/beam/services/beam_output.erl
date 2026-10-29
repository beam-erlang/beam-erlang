%% SPDX-License-Identifier: Apache-2.0
-module(beam_output).
-export([start/0, write/3]).

-spec write(atom() | binary(), binary(), integer() | infinity) ->
          ok | {error, #{kind := atom()}}.
write(Destination, Bytes, Deadline) ->
    validate_deadline(Deadline),
    case valid_destination(Destination) andalso is_binary(Bytes) of
        true -> write_checked(Destination, Bytes, Deadline);
        false -> erlang:error(badarg, [Destination, Bytes, Deadline])
    end.

write_checked(Destination, Bytes, Deadline) ->
    case selected(output) of
        false ->
            failure(unavailable, false);
        true when Bytes =:= <<>> -> ok;
        true ->
            case beam_time:expired(Deadline) of
                true -> failure(timeout, false);
                false -> deliver(Destination, Bytes)
            end
    end.

deliver(Destination, Bytes) ->
    Server = whereis(beam_output_server),
    case is_pid(Server) of
        false -> failure(unavailable, false);
        true ->
            Ref = erlang:monitor(process, Server),
            Server ! {write, self(), Ref, Destination, Bytes},
            receive
                {Ref, ok} ->
                    erlang:demonitor(Ref, [flush]),
                    ok;
                {Ref, {error, Kind}} ->
                    erlang:demonitor(Ref, [flush]),
                    failure(Kind, true);
                {'DOWN', Ref, process, _, _} ->
                    failure(closed, true)
            end
    end.

selected(_Name) -> is_pid(whereis(beam_output_server)).

valid_destination(Name) when is_atom(Name), Name =/= '' -> true;
valid_destination(Name) when is_binary(Name), byte_size(Name) > 0 -> true;
valid_destination(_) -> false.

validate_deadline(infinity) -> ok;
validate_deadline(D) when is_integer(D) -> ok;
validate_deadline(_) -> erlang:error(badarg).
failure(Reason, Unknown) ->
    Kind = case Reason of
        unavailable -> unavailable;
        timeout -> timeout;
        not_found -> not_found;
        busy -> busy;
        closed -> closed;
        _ -> io
    end,
    {error, #{kind => Kind, service => output, bytes_written => 0,
              progress_unknown => Unknown, native => #{domain => host, code => Reason}}}.

start() ->
    Parent = self(),
    {Pid, Ref} = spawn_monitor(fun() ->
        register(beam_output_server, self()),
        Parent ! {self(), ready},
        output_loop()
    end),
    receive
        {Pid, ready} ->
            demonitor(Ref, [flush]),
            Watcher = spawn(fun() ->
                Monitor = monitor(process, Pid),
                Parent ! {self(), watching},
                receive {'DOWN', Monitor, process, Pid, Reason} ->
                    erts_internal:request_failure(output, Reason)
                end
            end),
            receive {Watcher, watching} -> ok end;
        {'DOWN', Ref, process, Pid, Reason} -> exit(Reason)
    end.

output_loop() ->
    receive
        {write, From, Ref, Destination, Bytes} ->
            Reply = case beam_host:request({output, Destination, Bytes}) of
                        {ok, ok} -> ok;
                        {ok, {error, Kind}} -> {error, Kind};
                        {error, _} -> {error, io}
                    end,
            From ! {Ref, Reply},
            output_loop()
    end.
