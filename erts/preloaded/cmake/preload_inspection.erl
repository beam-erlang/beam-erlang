%% SPDX-License-Identifier: Apache-2.0
%% Publish metadata for retained execution images; never replace their code.
-module(preload_inspection).
-export([main/1]).
-include_lib("kernel/include/eep48.hrl").

main([SourceRoot, Destination | Modules]) ->
    Canonical = lists:sort([atom_to_list(Module) || Module <- erlang:pre_loaded()]),
    Canonical = lists:sort(Modules),
    ok = file:set_cwd(SourceRoot),
    ok = filelib:ensure_dir(filename:join(Destination, "placeholder")),
    lists:foreach(fun(Name) -> inspection(Name, Destination) end, Modules),
    io:format("Published inspection metadata for ~p canonical preloads~n", [length(Modules)]).

inspection(Name, Destination) ->
    Source = filename:join("erts/preloaded/src", Name ++ ".erl"),
    Seed = filename:join("erts/preloaded/ebin", Name ++ ".beam"),
    Options = [binary, debug_info, deterministic, return_errors, return_warnings,
               nowarn_deprecated_catch, {i, "lib/kernel/src"},
               {i, "bootstrap/lib/kernel/include"}, {i, "bootstrap/lib/stdlib/include"}],
    {Module, Compiled} = case compile:noenv_file(Source, Options) of
        {ok, M, Bytes} -> {M, Bytes};
        {ok, M, Bytes, _Warnings} -> {M, Bytes};
        Error -> error({preload_inspection_compile_failed, Source, Error})
    end,
    {ok, {Module, [{exports, Exports}]}} = beam_lib:chunks(Seed, [exports]),
    {ok, {Module, [{exports, Exports}]}} = beam_lib:chunks(Compiled, [exports]),
    {ok, Module, Original} = beam_lib:all_chunks(Seed),
    {ok, Module, Full} = beam_lib:all_chunks(Compiled),
    Metadata = ["Attr", "CInf", "Dbgi", "Docs"],
    Extra = [{Id, inspection_metadata(Id, Data, Name)} ||
             {Id, Data} <- Full, lists:member(Id, Metadata)],
    true = lists:sort(Metadata) =:= lists:sort([Id || {Id, _} <- Extra]),
    Chunks = [{Id, Data} || {Id, Data} <- Original, not lists:member(Id, Metadata)] ++ Extra,
    {ok, Result} = beam_lib:build_module(Chunks),
    {ok, Module, Checked} = beam_lib:all_chunks(Result),
    %% This includes prim_eval's assembly execution body. Its abstract code
    %% comes from the upstream source stub, as in upstream add_abstract_code.
    true = lists:all(fun({Id, Data}) ->
        lists:member(Id, Metadata) orelse lists:keyfind(Id, 1, Checked) =:= {Id, Data}
    end, Original),
    {ok, {Module, [{abstract_code, {raw_abstract_v1, _}},
                   {attributes, _}, {documentation, _}]}} =
        beam_lib:chunks(Result, [abstract_code, attributes, documentation]),
    ok = file:write_file(filename:join(Destination, Name ++ ".beam"), Result).

inspection_metadata("Docs", Data, Name) ->
    #docs_v1{metadata = Metadata} = Docs = binary_to_term(Data),
    SourcePath = filename:join(["erts", "preloaded", "src", Name ++ ".erl"]),
    term_to_binary(Docs#docs_v1{metadata = Metadata#{source_path => SourcePath}},
                   [deterministic]);
inspection_metadata(_Id, Data, _Name) ->
    Data.
