%% SPDX-License-Identifier: Apache-2.0
-module(beam_time).
-export([deadline_after/1, remaining/1, expired/1]).

-spec deadline_after(non_neg_integer() | infinity) -> integer() | infinity.
deadline_after(infinity) ->
    infinity;
deadline_after(Milliseconds)
  when is_integer(Milliseconds), Milliseconds >= 0 ->
    Native = erlang:monotonic_time(native),
    Delta = erlang:convert_time_unit(Milliseconds, millisecond, native),
    Native + Delta;
deadline_after(Milliseconds) ->
    erlang:error(badarg, [Milliseconds]).

-spec remaining(integer() | infinity) -> non_neg_integer() | infinity.
remaining(infinity) ->
    infinity;
remaining(Deadline) when is_integer(Deadline) ->
    Left = Deadline - erlang:monotonic_time(native),
    case Left > 0 of
        true -> erlang:convert_time_unit(Left, native, millisecond);
        false -> 0
    end;
remaining(Deadline) ->
    erlang:error(badarg, [Deadline]).

-spec expired(integer() | infinity) -> boolean().
expired(infinity) ->
    false;
expired(Deadline) when is_integer(Deadline) ->
    erlang:monotonic_time(native) >= Deadline;
expired(Deadline) ->
    erlang:error(badarg, [Deadline]).
