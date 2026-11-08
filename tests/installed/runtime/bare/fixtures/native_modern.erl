%% SPDX-License-Identifier: Apache-2.0
-module(native_modern).
-export([run/1]).
-compile({beam_private_native_functions,[{fail,1}]}).

run(Profile) ->
    Expected=Profile=:=modern,
    Expected=qualified(1.0),
    case Expected of
        true -> true=body(1.0), true=auto(1.0);
        false ->
            try body(1.0) of V1 -> fail({expected_undef,V1}) catch error:undef -> ok end,
            try auto(1.0) of V2 -> fail({expected_undef,V2}) catch error:undef -> ok end
    end,
    ok.

qualified(X) when beam:is_float(X) -> true;
qualified(_) -> false.
body(X) -> beam:is_float(X).
auto(X) -> is_float(X).
fail(Reason) -> erlang:error(Reason).
