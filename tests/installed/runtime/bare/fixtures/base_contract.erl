%% SPDX-License-Identifier: Apache-2.0
-module(base_contract).
-export([check/0, filenames_utf8/0, filenames_latin1/0]).

check() ->
    5 = count(beam:pre_loaded()),
    false = beam:module_loaded(beam_return_boot),
    false = beam:module_loaded(beam_boot),
    false = beam:module_loaded(beam_file),
    false = beam:module_loaded(beam_socket),
    false = beam:module_loaded(init),
    undefined = whereis(beam_output_server),
    undefined = whereis(beam_service_file),
    undefined = whereis(beam_service_socket),
    undefined = whereis(code_server),
    false = beam:is_builtin(persistent_term, get, 2),
    case beam:module_loaded(beam) of
        true ->
            undefined = beam_persistent_term:get(beam_boot_services, undefined),
            <<"42">> = beam_format:integer(42),
            [2, 4] = beam_lists:map(fun(N) -> N * 2 end, [1, 2]);
        false -> ok
    end,
    ok.

count([]) -> 0;
count([_ | Rest]) -> 1 + count(Rest).

filenames_utf8() ->
    false = beam:module_loaded(prim_file),
    <<195,169,0>> = erts_internal:filename_to_native([233]),
    [233] = erts_internal:filename_from_native(<<195,169>>),
    false = erts_internal:filename_is_translatable(<<255>>),
    filenames_common().

filenames_latin1() ->
    false = beam:module_loaded(prim_file),
    <<233,0>> = erts_internal:filename_to_native([233]),
    [233] = erts_internal:filename_from_native(<<233>>),
    true = erts_internal:filename_is_translatable(<<255>>),
    filenames_common().

filenames_common() ->
    [97] = erts_internal:filename_normalize_utf8(<<97>>),
    true = erts_internal:filename_is_translatable(<<>>),
    badarg = invalid(fun() -> erts_internal:filename_to_native(an_atom) end),
    badarg = invalid(fun() -> erts_internal:filename_to_native([0]) end),
    badarg = invalid(fun() -> erts_internal:filename_normalize_utf8(<<255>>) end),
    badarg = invalid(fun() -> erts_internal:filename_is_translatable([]) end),
    <<111,107,0>> = erts_internal:filename_to_native("ok"),
    ok.

invalid(Fun) ->
    try Fun() of _ -> unexpectedly_accepted catch error:badarg -> badarg end.
