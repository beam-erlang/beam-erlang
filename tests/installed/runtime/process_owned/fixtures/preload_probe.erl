%% SPDX-License-Identifier: Apache-2.0
-module(preload_probe).
-export([probe/0]).
probe() ->
    Preloads = beam:pre_loaded(),
    Count = case member(?MODULE, Preloads) of true -> 6; false -> 5 end,
    Count = length(Preloads),
    {error, {not_supported, _}} = beam:load_nif("absent", 0),
    try erlang:open_port(invalid, []) of
        _ -> error(unexpected_port_admission)
    catch error:notsup -> ok end,
    Count.
member(X, [X|_]) -> true;
member(X, [_|Rest]) -> member(X, Rest);
member(_, []) -> false.
