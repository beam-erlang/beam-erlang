%% SPDX-License-Identifier: Apache-2.0
-module(beam_list).
-export([map/2, fold/3]).

-spec map([term()], fun((term()) -> term())) -> [term()].
map(List, Fun) when is_list(List), is_function(Fun, 1) ->
    map_loop(List, Fun);
map(List, Fun) ->
    erlang:error(badarg, [List, Fun]).

map_loop([], _Fun) ->
    [];
map_loop([Item | Rest], Fun) ->
    [Fun(Item) | map_loop(Rest, Fun)].

-spec fold([term()], term(), fun((term(), term()) -> term())) -> term().
fold(List, Initial, Fun) when is_list(List), is_function(Fun, 2) ->
    fold_loop(List, Initial, Fun);
fold(List, Initial, Fun) ->
    erlang:error(badarg, [List, Initial, Fun]).

fold_loop([], Accumulator, _Fun) ->
    Accumulator;
fold_loop([Item | Rest], Accumulator, Fun) ->
    fold_loop(Rest, Fun(Item, Accumulator), Fun).
