%% SPDX-License-Identifier: Apache-2.0
-module(beam_time).
-beam_native_slots([{monotonic_time,0},{monotonic_time,1},{system_time,0},{system_time,1},{time_offset,0},{time_offset,1},{timestamp,0},{start_timer,3},{start_timer,4},{send_after,3},{send_after,4},{cancel_timer,1},{cancel_timer,2},{read_timer,1},{read_timer,2}]).
-compile({beam_private_native_mfas,[{beam,error,2},{erts_internal,time_unit,0},{erts_internal,perf_counter_unit,0}]}).
-export([deadline_after/1, remaining/1, expired/1, convert_time_unit/3,
         monotonic_time/0, monotonic_time/1, system_time/0, system_time/1,
         time_offset/0, time_offset/1, timestamp/0,
         start_timer/3, start_timer/4, send_after/3, send_after/4,
         cancel_timer/1, cancel_timer/2, read_timer/1, read_timer/2]).
-export_type([time_unit/0, timestamp/0]).

-type time_unit() :: pos_integer() | second | millisecond | microsecond | nanosecond |
                     native | perf_counter | seconds | milli_seconds | micro_seconds | nano_seconds.
-type timestamp() :: {non_neg_integer(), non_neg_integer(), non_neg_integer()}.
-type timer_option() :: {abs, boolean()}.
-type timer_result_option() :: {async, boolean()} | {info, boolean()}.
-type timer_destination() :: pid() | atom().

-spec deadline_after(non_neg_integer() | infinity) -> integer() | infinity.
deadline_after(infinity) ->
    infinity;
deadline_after(Milliseconds)
  when is_integer(Milliseconds), Milliseconds >= 0 ->
    Native = monotonic_time(native),
    Delta = convert_time_unit(Milliseconds, millisecond, native),
    Native + Delta;
deadline_after(Milliseconds) ->
    beam:error(badarg, [Milliseconds]).

-spec remaining(integer() | infinity) -> non_neg_integer() | infinity.
remaining(infinity) ->
    infinity;
remaining(Deadline) when is_integer(Deadline) ->
    Left = Deadline - monotonic_time(native),
    case Left > 0 of
        true -> convert_time_unit(Left, native, millisecond);
        false -> 0
    end;
remaining(Deadline) ->
    beam:error(badarg, [Deadline]).

-spec expired(integer() | infinity) -> boolean().
expired(infinity) ->
    false;
expired(Deadline) when is_integer(Deadline) ->
    monotonic_time(native) >= Deadline;
expired(Deadline) ->
    beam:error(badarg, [Deadline]).

-spec convert_time_unit(integer(), time_unit(), time_unit()) -> integer().
-define(BEAM_TIME_CONVERT(Time, From, To), convert_time_unit(Time, From, To)).
-define(BEAM_TIME_CONVERT_ERROR(Args), beam:error(badarg, Args)).
-include("../support/include/beam_time_conversion.hrl").

-spec monotonic_time() -> integer().
monotonic_time() -> beam:nif_error(undefined).
-spec monotonic_time(time_unit()) -> integer().
monotonic_time(_Unit) -> beam:nif_error(undefined).

-spec system_time() -> integer().
system_time() -> beam:nif_error(undefined).
-spec system_time(time_unit()) -> integer().
system_time(_Unit) -> beam:nif_error(undefined).

-spec time_offset() -> integer().
time_offset() -> beam:nif_error(undefined).
-spec time_offset(time_unit()) -> integer().
time_offset(_Unit) -> beam:nif_error(undefined).

-spec timestamp() -> timestamp().
timestamp() -> beam:nif_error(undefined).

-spec start_timer(integer(), timer_destination(), term()) -> reference().
start_timer(_Time, _Dest, _Message) -> beam:nif_error(undefined).
-spec start_timer(integer(), timer_destination(), term(), [timer_option()]) -> reference().
start_timer(_Time, _Dest, _Message, _Options) -> beam:nif_error(undefined).

-spec send_after(integer(), timer_destination(), term()) -> reference().
send_after(_Time, _Dest, _Message) -> beam:nif_error(undefined).
-spec send_after(integer(), timer_destination(), term(), [timer_option()]) -> reference().
send_after(_Time, _Dest, _Message, _Options) -> beam:nif_error(undefined).

-spec cancel_timer(reference()) -> non_neg_integer() | false.
cancel_timer(_Ref) -> beam:nif_error(undefined).
-spec cancel_timer(reference(), [timer_result_option()]) -> non_neg_integer() | false | ok.
cancel_timer(_Ref, _Options) -> beam:nif_error(undefined).

-spec read_timer(reference()) -> non_neg_integer() | false.
read_timer(_Ref) -> beam:nif_error(undefined).
-spec read_timer(reference(), [{async, boolean()}]) -> non_neg_integer() | false | ok.
read_timer(_Ref, _Options) -> beam:nif_error(undefined).
