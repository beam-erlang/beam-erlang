%% SPDX-License-Identifier: Apache-2.0
-module(beam_file).
-export([start/0]).
-export([info/2, list_directory/2, make_directory/2, remove_file/2, remove_directory/2, rename/3]).
-export([open/3, read/3, write/3, close/2, read_file/2, write_file/3]).

%% Handles are opaque single-owner native resources. The owner in this map is
%% descriptive; native state is authoritative even if a caller edits the map.
-spec open(binary(), integer() | infinity, #{mode := read | write}) -> {ok, map()} | {error, map()}.
open(Path, Deadline, #{mode := Mode} = Options)
  when is_binary(Path), (Mode =:= read orelse Mode =:= write), map_size(Options) =:= 1 ->
    case path_ready(Path, Deadline) of
        {ok, Name} ->
            case operation(open, {Name, Mode}, Deadline) of
                {ok, Fd} -> {ok, #{module => beam_file, owner => self(), fd => Fd}};
                {error, Error} -> {error, Error#{effects => possible}}
            end;
        Error -> Error
    end;
open(Path, Deadline, Options) -> erlang:error(badarg, [Path, Deadline, Options]).

-spec read(map(), non_neg_integer(), integer() | infinity) -> {ok, binary()} | eof | {error, map()}.
read(Handle, Maximum, Deadline) when is_integer(Maximum), Maximum >= 0 ->
    case owned(Handle, Deadline, Maximum =:= 0) of
        {ok, _Fd} when Maximum =:= 0 -> {ok, <<>>};
        {ok, Fd} -> operation(read, {Fd, min(Maximum, 65536)}, Deadline);
        Error -> Error
    end;
read(Handle, Maximum, Deadline) -> erlang:error(badarg, [Handle, Maximum, Deadline]).

-spec write(map(), binary(), integer() | infinity) -> ok | {error, map()}.
write(Handle, Bytes, Deadline) when is_binary(Bytes) ->
    Result = case owned(Handle, Deadline, byte_size(Bytes) =:= 0) of
                 {ok, _Fd} when Bytes =:= <<>> -> ok;
                 {ok, Fd} -> operation(write, {Fd, Bytes}, Deadline);
                 Error -> Error
             end,
    write_result(Result, 0);
write(Handle, Bytes, Deadline) -> erlang:error(badarg, [Handle, Bytes, Deadline]).

-spec close(map(), integer() | infinity) -> ok | {error, map()}.
close(Handle, Deadline) ->
    case owned(Handle, Deadline, true) of
        {ok, Fd} -> operation(close, {Fd}, infinity);
        {error, #{kind := closed}} -> ok;
        Error -> Error
    end.

-spec read_file(binary(), integer() | infinity) -> {ok, binary()} | {error, map()}.
read_file(Path, Deadline) when is_binary(Path) ->
    case open(Path, Deadline, #{mode => read}) of
        {ok, Handle} ->
            Result = read_all(Handle, Deadline, []),
            finish_file(Result, close(Handle, infinity));
        Error -> Error
    end;
read_file(Path, Deadline) -> erlang:error(badarg, [Path, Deadline]).

read_all(Handle, Deadline, Acc) ->
    case read(Handle, 65536, Deadline) of
        {ok, Bytes} -> read_all(Handle, Deadline, [Bytes | Acc]);
        eof -> {ok, erlang:iolist_to_binary(reverse(Acc, []))};
        Error -> Error
    end.
reverse([], Acc) -> Acc;
reverse([H | T], Acc) -> reverse(T, [H | Acc]).

-spec write_file(binary(), binary(), integer() | infinity) -> ok | {error, map()}.
write_file(Path, Bytes, Deadline) when is_binary(Path), is_binary(Bytes) ->
    case open(Path, Deadline, #{mode => write}) of
        {ok, Handle} ->
            Result = write(Handle, Bytes, Deadline),
            %% Opening for write may already have created or truncated the file.
            case finish_file(Result, close(Handle, infinity)) of
                {error, Error} -> write_result({error, Error#{effects => possible}},
                                               case Result of ok -> byte_size(Bytes); _ -> 0 end);
                ok -> ok
            end;
        Error -> write_result(Error, 0)
    end;
write_file(Path, Bytes, Deadline) -> erlang:error(badarg, [Path, Bytes, Deadline]).

finish_file({error, Primary}, {error, Cleanup}) -> {error, Primary#{cleanup => Cleanup}};
finish_file({error, _} = Primary, _) -> Primary;
finish_file(_, {error, _} = Cleanup) -> Cleanup;
finish_file(Result, ok) -> Result.

-spec info(binary(), integer() | infinity) -> {ok, map()} | {error, map()}.
info(Path, Deadline) -> path_operation(info, Path, Deadline).
-spec list_directory(binary(), integer() | infinity) -> {ok, [binary()]} | {error, map()}.
list_directory(Path, Deadline) ->
    case path_operation(list_directory, Path, Deadline) of
        {ok, Names} -> validate_names(Names, Names);
        Error -> Error
    end.
validate_names([], Names) -> {ok, Names};
validate_names([Name | Rest], Names) ->
    case valid_path(Name) of
        true -> validate_names(Rest, Names);
        false -> {error, #{kind => invalid_data, service => file, reason => unrepresentable_name}}
    end.
-spec make_directory(binary(), integer() | infinity) -> ok | {error, map()}.
make_directory(Path, Deadline) -> path_operation(make_directory, Path, Deadline).
-spec remove_file(binary(), integer() | infinity) -> ok | {error, map()}.
remove_file(Path, Deadline) -> path_operation(remove_file, Path, Deadline).
-spec remove_directory(binary(), integer() | infinity) -> ok | {error, map()}.
remove_directory(Path, Deadline) -> path_operation(remove_directory, Path, Deadline).
-spec rename(binary(), binary(), integer() | infinity) -> ok | {error, map()}.
rename(Source, Destination, Deadline) when is_binary(Source), is_binary(Destination) ->
    case path_ready(Source, Deadline) of
        {ok, Name} ->
            case path_name(Destination) of
                {ok, Target} -> operation(rename, {Name, Target}, Deadline);
                Error -> Error
            end;
        Error -> Error
    end;
rename(Source, Destination, Deadline) -> erlang:error(badarg, [Source, Destination, Deadline]).
path_operation(Action, Path, Deadline) when is_binary(Path) ->
    case path_ready(Path, Deadline) of
        {ok, Name} -> operation(Action, {Name}, Deadline);
        Error -> Error
    end;
path_operation(_, Path, Deadline) -> erlang:error(badarg, [Path, Deadline]).

path_ready(Path, Deadline) ->
    case ready(Deadline, false) of
        ok -> path_name(Path);
        Error -> Error
    end.
ready(Deadline, IgnoreExpiry) ->
    validate_deadline(Deadline),
    case is_pid(whereis(beam_file_owner)) of
        false -> {error, #{kind => unavailable, service => file}};
        true -> expiry(Deadline, IgnoreExpiry)
    end.
expiry(_, true) -> ok;
expiry(Deadline, false) ->
    case beam_time:expired(Deadline) of
        true -> {error, #{kind => timeout, service => file}};
        false -> ok
    end.
owned(#{module := beam_file, owner := Owner, fd := Fd}, Deadline, IgnoreExpiry) when is_pid(Owner) ->
    %% Validate the native reference before checking availability, but check its
    %% ownership/state before the deadline. No caller-supplied owner grants access.
    validate_deadline(Deadline),
    State = prim_file:beam_handle_state(Fd),
    case is_pid(whereis(beam_file_owner)) of
        false -> {error, #{kind => unavailable, service => file}};
        true ->
            case State of
                ok -> case expiry(Deadline, IgnoreExpiry) of ok -> {ok, Fd}; Error -> Error end;
                Kind -> {error, #{kind => Kind, service => file}}
            end
    end;
owned(_, Deadline, _) -> validate_deadline(Deadline), erlang:error(badarg).
validate_deadline(infinity) -> ok;
validate_deadline(D) when is_integer(D) -> ok;
validate_deadline(_) -> erlang:error(badarg).

operation(Action, Args, Deadline) ->
    NativeDeadline = case Deadline of
        infinity -> infinity;
        _ -> max(-9223372036854775808, min(9223372036854775807,
                 erlang:convert_time_unit(Deadline, native, nanosecond)))
    end,
    case prim_file:beam_operation(Action, Args, NativeDeadline) of
        {error, Reason, Count} -> {error, (file_error(Reason))#{bytes_written => Count, progress_unknown => false}};
        {error, Reason} -> {error, file_error(Reason)};
        Result -> Result
    end.

path_name(Path) when is_binary(Path) ->
    Absolute = case erlang:system_info(os_type) of
        {win32, _} -> windows_absolute(Path);
        _ -> case Path of <<"/", _/binary>> -> true; _ -> false end
    end,
    case Absolute andalso valid_path(Path) of
        true -> {ok, <<Path/binary, 0>>};
        false -> {error, #{kind => invalid_data, service => file}}
    end;
path_name(_) -> {error, #{kind => invalid_data, service => file}}.
windows_absolute(<<Drive, $:, Slash, _/binary>>)
  when (Drive >= $A andalso Drive =< $Z) orelse (Drive >= $a andalso Drive =< $z),
       Slash =:= $/ orelse Slash =:= $\\ -> true;
windows_absolute(<<"//", Rest/binary>>) -> byte_size(Rest) > 0;
windows_absolute(<<"\\\\", Rest/binary>>) -> byte_size(Rest) > 0;
windows_absolute(_) -> false.
valid_path(<<>>) -> true;
valid_path(<<0, _/binary>>) -> false;
valid_path(<<_/utf8, Rest/binary>>) -> valid_path(Rest);
valid_path(_) -> false.

write_result({error, #{bytes_written := _, progress_unknown := _}} = Error, _) -> Error;
write_result({error, Error}, Count) ->
    {error, Error#{bytes_written => Count, progress_unknown => false}};
write_result(ok, _) -> ok.

file_error(Reason) ->
    Kind = case Reason of
        enoent -> not_found;
        eacces -> permission_denied;
        eperm -> permission_denied;
        einval -> invalid_data;
        ebadf -> closed;
        ealready -> not_owner;
        etimedout -> timeout;
        ebusy -> busy;
        eexist -> busy;
        _ -> io
    end,
    #{kind => Kind, service => file, reason => Reason,
      native => #{domain => posix, code => Reason}}.

%% The actual native owner is monitored by its library, independently of entry.
start() ->
    ok = prim_buffer:on_load(),
    ok = prim_file:on_load(),
    Parent = self(),
    {Watcher, Ref} = spawn_monitor(fun() ->
        Owner = whereis(beam_file_owner),
        Monitor = monitor(process, Owner),
        Parent ! {self(), ready},
        receive
            {'DOWN', Monitor, process, Owner, Reason} ->
                erts_internal:request_failure(file, Reason)
        end
    end),
    receive
        {Watcher, ready} -> demonitor(Ref, [flush]), ok;
        {'DOWN', Ref, process, Watcher, Reason} -> exit(Reason)
    end.
