%% SPDX-License-Identifier: Apache-2.0
%% Source acquisition belongs to the supplied host; compilation returns data.
-module(beam_compiler_engine).
-export([source/3,forms/3]).

source(Source0, Options, Context) ->
    protect(Source0, fun() -> source_1(Source0,Options,Context) end).

source_1(Source0, Options, Context) ->
    Cwd = maps:get(cwd, Context),
    Source = filename:absname(Source0, Cwd),
    Identity = case lists:member(deterministic,Options) of
                   true -> filename:basename(Source0);
                   false -> filename:join(filename:dirname(Source0),filename:basename(Source0))
               end,
    Includes = [filename:absname(Dir,Cwd) || {i,Dir} <- Options],
    Macros = [{Name,Value} || {d,Name,Value} <- Options],
    case erl_features:init_parse_state(Options,fun erl_scan:f_reserved_word/1) of
        {ok,{Features,Reserved}} ->
            EppOptions=[extra,file_dependencies,{includes,[Cwd,filename:dirname(Source)|Includes]},
                        {source_name,Identity},{deterministic,lists:member(deterministic,Options)},
                        {macros,Macros},{default_encoding,utf8},{location,{1,1}},
                        {features,Features},{reserved_word_fun,Reserved}],
            case epp:parse_file(Source,EppOptions) of
                {ok,Forms,Extra} ->
                    Dependencies=proplists:get_value(file_dependencies,Extra,[]),
                    Used=proplists:get_value(features,Extra,Features),
                    Result=forms(Forms,[{features,Used}|Options],Context#{source=>Identity}),
                    case Result of
                        {ok,Output} -> {ok,Output#{dependencies=>lists:usort(Dependencies)}};
                        Error -> Error
                    end;
                {error,Reason} -> {error,[{Identity,[{none,epp,Reason}]}],[]}
            end;
        {error,{Module,Reason}} -> {error,[{Identity,[{none,Module,Reason}]}],[]}
    end.

forms(Forms,Options,Context) ->
    Identity=maps:get(source,Context,""),
    protect(Identity,fun() -> forms_1(Forms,Options,Identity) end).

forms_1(Forms,Options,Identity) ->
    case lists:member(beam_no_transforms, Options) andalso
         lists:any(fun({attribute,_,compile,V}) -> contains_transform(V);
                      (_) -> false end, Forms) of
        true -> throw({unsupported, compiler_transforms});
        false -> ok
    end,
    Policy=[binary,return_errors,return_warnings,no_spawn_compiler_process,{source,Identity}],
    case compile:noenv_forms(Forms,Policy++Options) of
        {ok,Module,Bytes,Warnings} when is_binary(Bytes) ->
            {ok,#{module=>Module,bytecode=>Bytes,warnings=>Warnings,dependencies=>[]}};
        {ok,Module,Bytes} when is_binary(Bytes) ->
            {ok,#{module=>Module,bytecode=>Bytes,warnings=>[],dependencies=>[]}};
        {error,Errors,Warnings} -> {error,Errors,Warnings};
        Other -> {error,[{Identity,[{none,compile,{compiler_result,Other}}]}],[]}
    end.

protect(Identity,Function) ->
    try Function()
    catch
        Class:Reason:Stack ->
            {error,[{Identity,[{none,compile,{compiler_exception,Class,Reason,Stack}}]}],[]}
    end.

contains_transform({parse_transform,_}) -> true;
contains_transform({core_transform,_}) -> true;
contains_transform(V) when is_list(V) -> lists:any(fun contains_transform/1,V);
contains_transform(_) -> false.
