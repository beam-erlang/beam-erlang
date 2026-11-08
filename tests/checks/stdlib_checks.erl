%% SPDX-License-Identifier: Apache-2.0
%% Inspect exported type documentation using the independent build compiler.
-module(stdlib_checks).
-export([parse_transform/2]).

parse_transform(Forms, _Options) ->
    [Cases] = [Cases || {attribute, _, stdlib_types, Cases} <- Forms],
    lists:foreach(fun({File, Expected}) ->
        {ok, {_, [{"Docs", Binary}]}} = beam_lib:chunks(File, ["Docs"]),
        {docs_v1, _, erlang, _, _, _, Docs} = binary_to_term(Binary),
        Actual = lists:sort([{Name,Arity} ||
            {{type,Name,Arity},_,_,_,#{exported := true}} <- Docs]),
        case Actual =:= lists:sort(Expected) of
            true -> ok;
            false -> error({stdlib_exported_types, File, Expected, Actual})
        end
    end, Cases),
    Forms.
