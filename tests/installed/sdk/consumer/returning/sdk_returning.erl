%% SPDX-License-Identifier: Apache-2.0
-module(sdk_returning).
-export([main/1, on_load/0]).
on_load() ->
    beam_persistent_term:put(beam_boot_origins, [{?MODULE, <<"activation:origin">>}]),
    ok.
main(Value) ->
    undefined = whereis(code_server),
    undefined = whereis(beam_output_server),
    undefined = whereis(beam_file_owner),
    undefined = whereis(socket_registry),
    false = beam:module_loaded(code_server),
    false = beam:module_loaded(beam_cli),
    false = beam:module_loaded(init),
    ExpectedOrigin = case Value of
        42 -> <<"memory:sdk-origin">>;
        43 -> <<"memory">>;
        44 -> <<"activation:origin">>
    end,
    ExpectedOrigin = origin(?MODULE, beam_persistent_term:get(beam_boot_origins)),
    Value.

origin(Module, [{Module, Origin} | _]) -> Origin;
origin(Module, [_ | Rest]) -> origin(Module, Rest);
origin(_, []) -> undefined.
