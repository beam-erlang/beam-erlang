%% SPDX-License-Identifier: Apache-2.0
-module(core_probe).
-export([main/1]).
main([Directory]) ->
    Files = filelib:wildcard(filename:join(Directory, "*.beam")),
    9 = length(Files),
    lists:foreach(fun(File) ->
        %% make_preload retains these chunks, following the upstream generator.
        %% Tooling gets the original file, including documentation/debug metadata.
        {ok, Module, Chunks} = beam_lib:all_chunks(File),
        {ok, {Module, [{abstract_code, {raw_abstract_v1, Forms}},
                       {attributes, Attributes}, {compile_info, CompileInfo}]}} =
            beam_lib:chunks(File, [abstract_code, attributes, compile_info]),
        true = is_list(Forms) andalso Forms =/= [],
        true = is_list(Attributes),
        true = is_list(CompileInfo),
        Embedded = [Chunk || {Name, _} = Chunk <- Chunks,
                    lists:member(Name, ["Code", "AtU8", "ImpT", "ExpT", "StrT",
                                        "FunT", "LitT", "Type"])],
        {ok, Binary} = beam_lib:build_module(Embedded),
        {ok, {Module, Digest}} = beam_lib:md5(Binary),
        preloaded = code:which(Module),
        Digest = Module:module_info(md5)
    end, Files).
