%% SPDX-License-Identifier: Apache-2.0
-module(sdk_returning).
-export([main/1, on_load/0]).
on_load() ->
    persistent_term:put(beam_boot_origins, [{?MODULE, <<"activation:origin">>}]),
    ok.
main(Value) ->
    undefined = whereis(code_server),
    undefined = whereis(beam_service_output),
    undefined = whereis(beam_file_owner),
    undefined = whereis(socket_registry),
    false = erlang:module_loaded(code_server),
    false = erlang:module_loaded(beam_cli),
    false = erlang:module_loaded(init),
    ExpectedOrigin = case Value of
        42 -> <<"memory:sdk-origin">>;
        43 -> <<"memory">>;
        44 -> <<"activation:origin">>
    end,
    ExpectedOrigin = origin(?MODULE, persistent_term:get(beam_boot_origins)),
    Value.

origin(Module, [{Module, Origin} | _]) -> Origin;
origin(Module, [_ | Rest]) -> origin(Module, Rest);
origin(_, []) -> undefined.
