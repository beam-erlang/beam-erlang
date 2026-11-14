%% SPDX-License-Identifier: Apache-2.0
-module(beam_sdk_probe).
-export([load/1, add/1, symbol_visibility/0, affinity_get/0, affinity_set/1, run/5]).
-include_lib("kernel/include/eep48.hrl").

load(Path) -> erlang:load_nif(Path, 0).
add(_) -> erlang:nif_error(not_loaded).
symbol_visibility() -> erlang:nif_error(not_loaded).
affinity_get() -> erlang:nif_error(not_loaded).
affinity_set(_) -> erlang:nif_error(not_loaded).

run(Bundle, Flavor, HostTriple, Version, BuildType) ->
    Version = erlang:system_info(version),
    Flavor = erlang:system_info(emu_flavor),
    HostTriple = erlang:system_info(system_architecture),
    ExpectedDiagnostics = case BuildType of
                              "Debug" -> {debug, true, true};
                              "Release" -> {opt, false, false}
                          end,
    ExpectedDiagnostics = {erlang:system_info(build_type),
                           erlang:system_info(debug_compiled),
                           erlang:system_info(lock_checking)},
    utf8 = file:native_name_encoding(),
    Filename = filename:join(Bundle, "SDK λ.bin"),
    ok = file:write_file(Filename, <<"UTF-8 filename">>),
    {ok, <<"UTF-8 filename">>} = file:read_file(Filename),
    ok = file:delete(Filename),
    Native = filename:join(Bundle, "native"),
    ok = load(filename:join(Native, "beam_sdk_nif")),
    42 = add(41),
    ok = symbol_visibility(),
    ok = check_cpu_affinity(),
    ok = erl_ddll:load_driver(Native, "beam_sdk_driver"),
    Port = open_port({spawn_driver, "beam_sdk_driver"}, [binary]),
    true = port_command(Port, <<"native driver", 0, 255>>),
    receive {Port, {data, <<"native driver", 0, 255>>}} -> ok
    after 3000 -> error(driver_timeout)
    end,
    true = port_close(Port),
    ok = erl_ddll:unload_driver("beam_sdk_driver"),
    Ebin = filename:join([Bundle, "lib", "erts-" ++ Version, "ebin"]),
    SeedDir = filename:join(Bundle, "execution-seeds"),
    %% Match the chunk request made by upstream xref_base:abst/1. Stripped
    %% execution seeds alone cannot support xref/Dialyzer inspection.
    Files = filelib:wildcard(filename:join(Ebin, "*.beam")),
    22 = length(Files),
    InstalledNames = lists:sort([filename:basename(File) || File <- Files]),
    InstalledNames = lists:sort([filename:basename(File) || File <-
                    filelib:wildcard(filename:join(SeedDir, "*.beam"))]),
    lists:foreach(fun(File) ->
        {ok, {Module, [{abstract_code, {raw_abstract_v1, Forms}},
                        {attributes, Attributes}, {documentation, Documentation}]}} =
            beam_lib:chunks(File, [abstract_code, attributes, documentation]),
        true = is_list(Forms) andalso Forms =/= [],
        true = is_list(Attributes),
        #docs_v1{metadata = Metadata} = Documentation,
        LogicalSource = filename:join(["erts", "preloaded", "src", atom_to_list(Module) ++ ".erl"]),
        LogicalSource = maps:get(source_path, Metadata),
        relative = filename:pathtype(LogicalSource),
        true = filelib:is_regular(filename:join([Bundle, "lib", "erts-" ++ Version, "src",
                                               filename:basename(LogicalSource)])),
        Seed = filename:join(SeedDir, filename:basename(File)),
        {ok, Module, SeedChunks} = beam_lib:all_chunks(Seed),
        {ok, Module, InstalledChunks} = beam_lib:all_chunks(File),
        ExecutionChunks = fun(Chunks) -> lists:sort([{Id, Bytes} || {Id, Bytes} <- Chunks,
                            not lists:member(Id, ["Attr", "CInf", "Dbgi", "Docs"])]) end,
        Preserved = ExecutionChunks(SeedChunks),
        Preserved = ExecutionChunks(InstalledChunks)
    end, Files),
    {ok, {erts_internal, [{"Line", Line}, {"Meta", Meta}]}} =
        beam_lib:chunks(filename:join(Ebin, "erts_internal.beam"), ["Line", "Meta"]),
    true = byte_size(Line) > 0,
    true = byte_size(Meta) > 0,
    {ok, [{application, erts, Properties}]} = file:consult(filename:join(Ebin, "erts.app")),
    Version = proplists:get_value(vsn, Properties),
    ok.

check_cpu_affinity() ->
    case affinity_get() of
        not_supported ->
            erlang:display({sdk_affinity, not_supported}),
            ok;
        {ok, [Cpu, _ | _] = Original} ->
            _ = erlang:system_info(update_cpu_info),
            Available = erlang:system_info(logical_processors_available),
            case Available of
                N when is_integer(N), N >= 2 -> ok;
                _ -> error({sdk_affinity_prerequisite,
                            at_least_two_available_processors, Available})
            end,
            try
                ok = affinity_set([Cpu]),
                {ok, [Cpu]} = affinity_get(),
                changed = erlang:system_info(update_cpu_info),
                1 = erlang:system_info(logical_processors_available),
                unchanged = erlang:system_info(update_cpu_info)
            after
                %% Restore the exact OS mask even when the runtime assertion
                %% fails. The owned host process also exits after this probe.
                ok = affinity_set(Original),
                {ok, Original} = affinity_get(),
                _ = erlang:system_info(update_cpu_info)
            end,
            Available = erlang:system_info(logical_processors_available),
            erlang:display({sdk_affinity, restored, Available}),
            ok;
        {ok, Cpus} ->
            error({sdk_affinity_prerequisite, at_least_two_allowed_cpus, Cpus});
        Error ->
            error({sdk_affinity_os_error, Error})
    end.
