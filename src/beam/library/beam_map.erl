%% SPDX-License-Identifier: Apache-2.0
-module(beam_map).
-export([find/2, put/3]).

-spec find(map(), term()) -> {ok, term()} | not_found.
find(Map, Key) when is_map(Map) ->
    case Map of
        #{Key := Value} -> {ok, Value};
        #{} -> not_found
    end;
find(Map, Key) ->
    erlang:error(badarg, [Map, Key]).

-spec put(map(), term(), term()) -> map().
put(Map, Key, Value) when is_map(Map) ->
    Map#{Key => Value};
put(Map, Key, Value) ->
    erlang:error(badarg, [Map, Key, Value]).
