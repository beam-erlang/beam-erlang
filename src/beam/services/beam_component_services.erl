%% SPDX-License-Identifier: Apache-2.0
%% Composition activation and facility ownership, independent of entry policy.
-module(beam_component_services).
-export([activate/3]).

activate(Services, Modules, Origins) ->
    persistent_term:put(beam_boot_origins, Origins),
    case native_services(Services) of
        {ok, Owners} ->
            case activate_modules(Modules) of
                ok -> ready(Owners);
                {error, Service, Reason} -> fail(Service, Reason)
            end;
        {error, Service, Reason} -> fail(Service, Reason)
    end.

native_services(Services) ->
    try
        Zlib = case Services band 4 of
                   0 -> [];
                   _ -> ok = activate_native(zlib, fun() -> zlib:on_load("beam_component") end),
                        [{zlib, start_owner(beam_service_zlib, fun owner_loop/0)}]
               end,
        File = case Services band 8 of
                   0 -> [];
                   _ -> ok = activate_native(file, fun() -> prim_buffer:on_load(), prim_file:on_load() end),
                        [{file, whereis(beam_file_owner)}]
               end,
        Network = case Services band 16 of
                      0 -> [];
                      _ -> ok = activate_native(socket, fun prim_socket:on_load/0),
                           [{socket, whereis(socket_registry)}]
                  end,
        Output = case Services band 1 of
                     0 -> [];
                     _ -> [{output, start_owner(beam_service_output, fun output_loop/0)}]
                 end,
        Program = case Services band 32 of
                      0 -> [];
                      _ -> [{program, start_owner(beam_service_program, fun owner_loop/0)}]
                  end,
        {ok, Zlib ++ File ++ Network ++ Output ++ Program}
    catch
        throw:{activation_failure, Service, Reason} -> {error, Service, Reason}
    end.

activate_native(Service, Fun) ->
    try Fun() of
        ok -> ok;
        Other -> throw({activation_failure, Service, Other})
    catch
        Class:Reason -> throw({activation_failure, Service, {Class, Reason}})
    end.

fail(Service, Reason) ->
    erts_internal:component_failed(Service, Reason),
    receive after infinity -> ok end.

ready(Owners) ->
    Parent = self(),
    {Watcher, Monitor} = spawn_monitor(fun() -> watch(Parent, Owners) end),
    receive
        {Watcher, ready} -> demonitor(Monitor, [flush]), 0;
        {'DOWN', Monitor, process, Watcher, Reason} -> fail(services, Reason)
    after 5000 -> fail(services, component_readiness_timeout)
    end.

watch(Parent, Owners) ->
    Refs = monitor_owners(Owners),
    Parent ! {self(), ready},
    receive
        {'DOWN', Ref, process, _Pid, Reason} -> fail(service(Ref, Refs), Reason)
    end.

monitor_owners([]) -> [];
monitor_owners([{Name, Pid} | Rest]) when is_pid(Pid) ->
    [{monitor(process, Pid), Name} | monitor_owners(Rest)].
service(Ref, [{Ref, Name} | _]) -> Name;
service(Ref, [_ | Rest]) -> service(Ref, Rest).

start_owner(Name, Loop) ->
    Parent = self(),
    {Pid, Monitor} = spawn_monitor(fun() ->
        register(Name, self()),
        Parent ! {ready, self()},
        Loop()
    end),
    receive
        {ready, Pid} -> demonitor(Monitor, [flush]), Pid;
        {'DOWN', Monitor, process, Pid, Reason} -> fail(Name, Reason)
    end.

owner_loop() -> receive _ -> owner_loop() end.

output_loop() ->
    receive
        {write, From, Ref, Destination, Bytes} ->
            Reply = case beam_host:request({output, Destination, Bytes}) of
                        {ok, ok} -> ok;
                        {ok, {error, Kind}} -> {error, Kind};
                        {error, _} -> {error, io}
                    end,
            From ! {Ref, Reply},
            output_loop()
    end.

activate_modules([]) ->
    ok;
activate_modules([{Module, Bytes, Origin} | Rest])
  when is_atom(Module), is_binary(Bytes), is_binary(Origin) ->
    case load_service(Module, Bytes) of
        ok ->
            activate_modules(Rest);
        {error, still_loaded} -> {error, still_loaded, still_loaded};
        {error, Reason} -> {error, service_name(Module), Reason}
    end;
activate_modules([Other | _]) ->
    {error, boot, {bad_service, Other}}.

load_service(Module, Bytes) ->
    try activate_service(Module, Bytes) of
        ok -> ok;
        {error, Reason} -> failed_activation(Module, Reason)
    catch
        Class:Failure -> failed_activation(Module, {Class, Failure})
    end.

activate_service(Module, Bytes) ->
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

failed_activation(Module, Reason) ->
    reject_on_load(Module, Reason).

rollback_on_load(Module) ->
    try erlang:finish_after_on_load(Module, false) of
        _ -> ok
    catch
        _:_ -> ok
    end.

service_name(zlib) -> zlib;
service_name(prim_file) -> file;
service_name(prim_socket) -> socket;
service_name(Name) -> Name.
