%% SPDX-License-Identifier: Apache-2.0
-module(process_root).
-export([start/2]).

start(<<"copied root ", 16#3bb/utf8>>, Status) ->
    [42] = erts_internal:vm_context(),
    [42] = erts_internal:vm_context(),
    10 = length(erlang:pre_loaded()),
    true = member(?MODULE, erlang:pre_loaded()),
    %% No framework assets or modern service components grant these permissions.
    Expected = case Status of 0 -> badarg; _ -> notsup end,
    Expected = failure(fun() -> erlang:open_port(invalid, []) end),
    Expected = failure(fun() -> erlang:setnode(invalid, 0) end),
    case Status of
        0 -> {error, {load_failed, _}} = erlang:load_nif("absent", 0);
        _ -> {error, {not_supported, _}} = erlang:load_nif("absent", 0)
    end,
    true = erts_internal:is_system_process(self()),
    "<0.0.0>" = pid_to_list(self()),
    true = self() =:= group_leader(),
    undefined = whereis(init),
    false = erlang:module_loaded(erl_init),
    false = erlang:module_loaded(beam_return_boot),
    false = erlang:module_loaded(prim_file),
    {ok, ok} = erts_internal:host_request(process_root_ready),
    case Status of
        250 -> halt(0);
        251 ->
            receive after 2000 -> ok end,
            {ok, ok} = erts_internal:host_request(process_root_ready),
            halt(0);
        255 -> exit(root_failed);
        _ -> halt(Status)
    end.

failure(Fun) ->
    try Fun() of _ -> unexpected_success
    catch error:Reason -> Reason end.

member(X, [X|_]) -> true;
member(X, [_|Rest]) -> member(X, Rest);
member(_, []) -> false.
