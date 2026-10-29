%% SPDX-License-Identifier: Apache-2.0
-module(beam_code).
-export([start/0, fetch/3, load/3]).

start() -> persistent_term:put(beam_code_enabled, true), ok.

-spec fetch(atom() | binary(), atom(), integer() | infinity) ->
          {ok, binary()} | {error, #{kind := atom()}}.
fetch(Source, Module, Deadline) when is_atom(Module) ->
    validate_deadline(Deadline),
    case valid_source(Source) of
        true -> fetch_checked(Source, Module, Deadline);
        false -> erlang:error(badarg, [Source, Module, Deadline])
    end;
fetch(Source, Module, Deadline) ->
    erlang:error(badarg, [Source, Module, Deadline]).

fetch_checked(Source, Module, Deadline) ->
    case selected(code) of
        false -> {error, #{kind => unavailable, service => code}};
        true ->
            case beam_time:expired(Deadline) of
                true -> {error, #{kind => timeout, service => code}};
                false ->
                    case beam_host:request({code, Source, Module}) of
                        {ok, {ok, Bytes}} when is_binary(Bytes) -> {ok, Bytes};
                        {ok, {error, Kind}} when Kind =:= unavailable; Kind =:= not_found; Kind =:= invalid_data; Kind =:= io ->
                            {error, #{kind => Kind, service => code, native => #{domain => host, code => Kind}}};
                        {error, _} -> {error, #{kind => io, service => code, native => #{domain => host, code => request_failed}}};
                        _ -> {error, #{kind => invalid_data, service => code, native => #{domain => host, code => invalid_reply}}}
                    end
            end
    end.

%% Publishes bytes after the caller has already returned from the host
%% callback. on_load is rejected and does not replace current code.
-spec load(atom(), binary(), atom() | binary()) -> ok | {error, #{kind := atom()}}.
load(Module, Bytes, Origin) when is_atom(Module), is_binary(Bytes) ->
    case valid_source(Origin) of
        false -> erlang:error(badarg, [Module, Bytes, Origin]);
        true ->
            case publish(Module, Bytes, Origin) of
                {module, Module} ->
                    Origins = persistent_term:get(beam_boot_origins, []),
                    Label = case is_atom(Origin) of
                                true -> atom_to_binary(Origin, utf8);
                                false -> Origin
                            end,
                    persistent_term:put(beam_boot_origins,
                                        [{Module, Label} | without_origin(Module, Origins)]),
                    ok;
                {error, badfile} -> publication_error(invalid_data, badfile);
                {error, on_load} -> publication_error(invalid_data, on_load);
                {error, not_purged} -> publication_error(busy, not_purged);
                {error, Reason} -> publication_error(invalid_data, Reason)
            end
    end;
load(Module, Bytes, Origin) ->
    erlang:error(badarg, [Module, Bytes, Origin]).

%% Inspect before asking the code server to publish: its ordinary load_binary
%% operation may run on_load, whereas this interface deliberately rejects it.
publish(Module, Bytes, Origin) ->
    case erlang:prepare_loading(Module, Bytes) of
        {error, _} = Error -> Error;
        Prepared ->
            case erlang:has_prepared_code_on_load(Prepared) of
                true -> {error, on_load};
                false ->
                    case whereis(code_server) of
                        Pid when is_pid(Pid) ->
                            code:load_binary(Module, origin_string(Origin), Bytes);
                        undefined ->
                            case erlang:finish_loading([Prepared]) of
                                ok -> {module, Module};
                                {Reason, _} -> {error, Reason}
                            end
                    end
            end
    end.

origin_string(Origin) when is_atom(Origin) -> atom_to_list(Origin);
origin_string(Origin) when is_binary(Origin) -> unicode:characters_to_list(Origin).

without_origin(_Module, []) -> [];
without_origin(Module, [{Module, _} | Rest]) -> without_origin(Module, Rest);
without_origin(Module, [Entry | Rest]) -> [Entry | without_origin(Module, Rest)].

selected(_Name) -> persistent_term:get(beam_code_enabled, false).

valid_source(Name) when is_atom(Name), Name =/= '' -> true;
valid_source(Name) when is_binary(Name), byte_size(Name) > 0 -> true;
valid_source(_) -> false.

validate_deadline(infinity) -> ok;
validate_deadline(D) when is_integer(D) -> ok;
validate_deadline(_) -> erlang:error(badarg).

publication_error(Kind, Reason) ->
    {error, #{kind => Kind, service => code, reason => Reason,
              native => #{domain => loader, code => Reason}}}.
