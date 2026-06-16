%% SPDX-License-Identifier: Apache-2.0
-module(beam_output).
-export([write/3]).

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
    Server = whereis(beam_service_output),
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

selected(_Name) -> erts_internal:component_ready(output).

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
