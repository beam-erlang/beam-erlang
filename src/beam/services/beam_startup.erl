%% SPDX-License-Identifier: Apache-2.0
%% Explicit host startup policy. Libraries supply no implicit actions.
-module(beam_startup).
-export([run/3]).

run(Actions, Modules, Origins) ->
    persistent_term:put(beam_boot_origins, Origins),
    run_actions(Actions),
    case load_modules(Modules) of
        ok -> 0;
        {error, Origin, Reason} -> fail(Origin, Reason)
    end.

run_actions([]) -> ok;
run_actions([{Module, Function, Arguments} | Rest]) ->
    try apply(Module, Function, Arguments) of
        ok -> run_actions(Rest);
        Other -> fail(Module, Other)
    catch
        Class:Reason -> fail(Module, {Class, Reason})
    end.

fail(Origin, Reason) ->
    erts_internal:request_failure(Origin, Reason),
    receive after infinity -> ok end.

load_modules([]) ->
    ok;
load_modules([{Module, Bytes, Origin} | Rest])
  when is_atom(Module), is_binary(Bytes), is_binary(Origin) ->
    case load_deferred_module(Module, Bytes) of
        ok ->
            load_modules(Rest);
        {error, still_loaded} -> {error, still_loaded, still_loaded};
        {error, Reason} -> {error, Module, Reason}
    end;
load_modules([Other | _]) ->
    {error, boot, {bad_module, Other}}.

load_deferred_module(Module, Bytes) ->
    try initialize_module(Module, Bytes) of
        ok -> ok;
        {error, Reason} -> failed_initialization(Module, Reason)
    catch
        Class:Failure -> failed_initialization(Module, {Class, Failure})
    end.

initialize_module(Module, Bytes) ->
    case erlang:prepare_loading(Module, Bytes) of
        {error, Reason} ->
            {error, Reason};
        Prep ->
            case erlang:finish_loading([Prep]) of
                {on_load, [Module]} ->
                    run_on_load(Module);
                ok ->
                    manual_on_load(Module);
                {Error, _} ->
                    {error, Error}
            end
    end.

run_on_load(Module) ->
    case invoke_on_load(Module) of
        ok ->
            erlang:finish_after_on_load(Module, true),
            ok;
        {error, badarg} ->
            %% JIT call_on_load_function/1 raises badarg. The on_load record
            %% is still pending; discard it instead of committing the module.
            reject_on_load(Module, badarg);
        {error, Reason} ->
            reject_on_load(Module, Reason)
    end.

invoke_on_load(Module) ->
    try erlang:call_on_load_function(Module) of
        ok -> ok;
        Other -> {error, Other}
    catch
        error:badarg -> {error, badarg};
        Class:Failure -> {error, {Class, Failure}}
    end.

manual_on_load(Module) ->
    case erlang:function_exported(Module, on_load, 0) of
        false ->
            ok;
        true ->
            case Module:on_load() of
                ok ->
                    ok;
                Other ->
                    reject_on_load(Module, Other)
            end
    end.

%% finish_after_on_load(Module, false) first. A module that finish_loading
%% already made current is deleted afterwards. still_loaded means that failed.
reject_on_load(Module, Reason) ->
    _ = rollback_on_load(Module),
    case erlang:module_loaded(Module) of
        false ->
            {error, Reason};
        true ->
            try erlang:delete_module(Module) catch _:_ -> ok end,
            try erlang:purge_module(Module) catch _:_ -> ok end,
            case erlang:module_loaded(Module) of
                true -> {error, still_loaded};
                false -> {error, Reason}
            end
    end.

failed_initialization(Module, Reason) ->
    reject_on_load(Module, Reason).

rollback_on_load(Module) ->
    try erlang:finish_after_on_load(Module, false) of
        _ -> ok
    catch
        _:_ -> ok
    end.

