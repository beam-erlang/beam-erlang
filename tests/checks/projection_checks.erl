%% SPDX-License-Identifier: Apache-2.0
%% Compiler-time checks run in the attested canonical bootstrap VM.
-module(projection_checks).
-export([parse_transform/2]).

parse_transform(Forms, _) ->
    Source = [form(S) || S <- [
        "-module(lists).",
        "-import(binary,[part/2,compile_pattern/1]).",
        "-export([reverse/2,exercise/2]).",
        "-type item() :: unicode:chardata().",
        "reverse(_,_) -> erlang:nif_error(undef).",
        "exercise(X,M) when maps:is_key(key,M) -> "
        "{reverse(X,[]),fun reverse/2,part(X,{0,1}),fun compile_pattern/1,"
        "fun binary:matches/2,binary:part(X,0,1),maps:get(key,M),"
        "erlang:apply(binary,compile_pattern,[X]),"
        "erlang:is_builtin(binary,matches,2),erlang:function_exported(lists,reverse,2),"
        "erlang:apply(lists,[]),"
        "{unicode,binary,lists,maps,string,utf8,latin1},"
        "{call,1,{remote,1,{atom,1,lists},{atom,1,reverse}},[]}}."
    ]],
    Result=beam_runtime_projection:parse_transform(Source,[]),
    true=contains({attribute,1,module,beam_lists},Result),
    false=contains({attribute,1,import,{binary,[{part,2},{compile_pattern,1}]}},Result),
    true=has_remote(beam_lists,reverse,Result),
    true=has_remote(beam_binary,part,Result),
    true=has_remote(beam,is_map_key,Result),
    true=has_remote(beam_maps,get,Result),
    true=has_remote_fun(beam_binary,compile_pattern,1,Result),
    true=has_remote_fun(beam_binary,matches,2,Result),
    true=contains({atom,1,chardata},Result),
    true=contains({atom,1,beam_unicode},Result),
    %% Data, encodings and quoted abstract syntax remain byte-for-byte AST terms.
    [Data,Quoted]=[T || T={tuple,_,_} <- flatten(Result),
                      T=:=expression("{unicode,binary,lists,maps,string,utf8,latin1}") orelse
                      T=:=expression("{call,1,{remote,1,{atom,1,lists},{atom,1,reverse}},[]}")],
    Data=expression("{unicode,binary,lists,maps,string,utf8,latin1}"),
    Quoted=expression("{call,1,{remote,1,{atom,1,lists},{atom,1,reverse}},[]}"),
    true=contains(expression("beam:apply(beam_binary,compile_pattern,[X])"),Result),
    true=contains(expression("beam:is_builtin(beam_binary,matches,2)"),Result),
    true=contains(expression("beam:function_exported(beam_lists,reverse,2)"),Result),
    true=contains(expression("beam:apply(lists,[])"),Result),
    %% Complete binary facade projection preserves unresolved dynamic dispatch.
    Unknown=beam_runtime_projection:parse_transform([form("-module(probe)."),form("f(X)->binary:unknown(X).")],[]),
    true=has_remote(beam_binary,unknown,Unknown),
    Dynamic=beam_runtime_projection:parse_transform([form("-module(probe)."),form("f(F,X)->apply(binary,F,X).")],[]),
    true=contains(expression("beam:apply(beam_binary,F,X)"),Dynamic),
    Partial=beam_runtime_projection:parse_transform([form("-module(probe)."),form("f(X)->apply(binary,copy,[<<>>|X]).")],[]),
    true=contains(expression("beam:apply(beam_binary,copy,[<<>>|X])"),Partial),
    Core=cerl:c_call(cerl:c_atom(maps),cerl:c_atom(from_keys),[cerl:c_nil(),cerl:c_atom(value)]),
    ProjectedCore=beam_runtime_projection:core_transform(Core,[]),
    beam_maps=cerl:concrete(cerl:call_module(ProjectedCore)),
    from_keys=cerl:concrete(cerl:call_name(ProjectedCore)),
    %% Feed the expanded module macro, as EPP does before parse transforms.
    EtsExpanded=[form(S) || S <- ["-module(beam_ets).", "-export([new/2,fun2ms/1,f/2,table/1,from_dets/2,i/0,file2tab/1]).",
        "new(_,_)->erlang:nif_error(undef).",
        "fun2ms(F)->{erl_eval:fun_data(F),ms_transform:transform_from_shell(beam_ets,F,[])}.",
        "f(N,O)->{new(N,O),fun new/2,ets:lookup(N,key),ets,{'ETS-TRANSFER',N}}."]],
    Ets=beam_runtime_projection:parse_transform(EtsExpanded,[]),
    true=contains({attribute,1,module,beam_ets},Ets),
    true=has_remote(beam_ets,new,Ets),
    true=has_remote(beam_ets,lookup,Ets),
    true=has_remote_fun(beam_ets,new,2,Ets),
    true=contains(expression("ms_transform:transform_from_shell(ets,F,[])"),Ets),
    false=has_remote(erl_eval,fun_data,Ets),
    [{new,2},{fun2ms,1}] = lists:append([FAs || {attribute,_,export,FAs} <- Ets]),
    true=contains({atom,1,ets},Ets),
    TimeSource=[form(S) || S <- ["-module(clock_probe).", "-export([f/1]).",
        "f(X)->{erlang:timestamp(),fun erlang:monotonic_time/1,"
        "erlang:convert_time_unit(X,native,millisecond),"
        "erlang:apply(erlang,start_timer,[0,self(),X]),"
        "erlang:function_exported(erlang,read_timer,2),{erlang,timestamp,now}}."]],
    Time=beam_runtime_projection:parse_transform(TimeSource,[]),
    true=has_remote(beam_time,timestamp,Time),
    true=has_remote_fun(beam_time,monotonic_time,1,Time),
    true=has_remote(erts_internal,clock_convert_time_unit,Time),
    true=contains(expression("beam:apply(beam_time,start_timer,[0,self(),X])"),Time),
    true=contains(expression("beam:function_exported(beam_time,read_timer,2)"),Time),
    true=contains(expression("{erlang,timestamp,now}"),Time),
    Math=beam_runtime_projection:parse_transform(
        [form(S) || S <- ["-module(math).", "-export([sin/1,pi/0]).",
                         "sin(_)->erlang:nif_error(undef).", "pi()->3.14159."]],[]),
    true=contains({attribute,1,module,beam_math},Math),
    true=contains({attribute,0,beam_native_slots,[{sin,1}]},Math),
    true=contains({attribute,0,compile,beam_modern_map_comprehensions},Math),
    collection_projection(),
    language_lowering(),
    Forms.

collection_projection() ->
    lists:foreach(fun({Module,Private}) ->
        Name=atom_to_list(Module), PrivateName=atom_to_list(Private),
        Data="{"++Name++",probe,0}",
        Quoted="{call,1,{remote,1,{atom,1,"++Name++"},{atom,1,probe}},[]}",
        Source=[form(S) || S <- [
            "-module("++PrivateName++").",
            "-import("++Name++",[probe/0]).",
            "-type collection() :: "++Name++":collection().",
            "exercise(M,F,A)->{probe(),fun probe/0,"++Name++":probe(),"
            "fun "++Name++":probe/0,"++Name++":F(A),fun "++Name++":F/A,"
            "erlang:apply("++Name++",F,A),"
            "erlang:function_exported("++Name++",F,A),"
            "erlang:module_loaded("++Name++"),erlang:apply(M,F,A),"
            ++Data++","++Quoted++"}."
        ]],
        Result=beam_runtime_projection:parse_transform(Source,[]),
        true=contains({attribute,1,module,Private},Result),
        false=contains({attribute,1,import,{Module,[{probe,0}]}},Result),
        true=contains({remote_type,1,[{atom,1,Private},{atom,1,collection},[]]},Result),
        true=has_remote(Private,probe,Result),
        true=has_remote_fun(Private,probe,0,Result),
        true=contains(expression(PrivateName++":F(A)"),Result),
        true=contains(expression("fun "++PrivateName++":F/A"),Result),
        true=contains(expression("beam:apply("++PrivateName++",F,A)"),Result),
        true=contains(expression("beam:function_exported("++PrivateName++",F,A)"),Result),
        true=contains(expression("beam:module_loaded("++PrivateName++")"),Result),
        true=contains(expression("beam:apply(M,F,A)"),Result),
        true=contains(expression(Data),Result),
        true=contains(expression(Quoted),Result)
    end,[{queue,beam_runtime_queue},{sets,beam_runtime_sets},
          {proplists,beam_runtime_proplists},{orddict,beam_runtime_orddict},
          {math,beam_math},{erl_ddll,beam_ddll},{erl_debugger,beam_debugger}]).

language_lowering() ->
    Forms=[form(S) || S <- ["-module(language_modes).", "-export([keys/1,pairs/1]).",
        "keys(X)->#{K=>true || K<-X}.","pairs(X)->#{K=>K+1 || K<-X}."]],
    {ok,language_modes,Modern,[]}=compile:noenv_forms(Forms,[binary,deterministic,beam_modern_map_comprehensions]),
    {ok,language_modes,Canonical,[]}=compile:noenv_forms(Forms,[binary,deterministic]),
    {ok,{_,[{imports,ModernImports}]}}=beam_lib:chunks(Modern,[imports]),
    []=[M || {M,_,_}<-ModernImports, M=:=maps],
    {ok,{_,[{imports,CanonicalImports}]}}=beam_lib:chunks(Canonical,[imports]),
    true=lists:member({maps,from_keys,2},CanonicalImports),
    true=lists:member({maps,from_list,1},CanonicalImports).

form(Text) -> {ok,T,_}=erl_scan:string(Text),{ok,F}=erl_parse:parse_form(T),F.
expression(Text) -> {ok,T,_}=erl_scan:string(Text++"."),{ok,[E]}=erl_parse:parse_exprs(T),E.
flatten(T) when is_tuple(T) -> [T|flatten(tuple_to_list(T))];
flatten(L) when is_list(L) -> lists:append([flatten(E)||E<-L]);
flatten(_) -> [].
contains(E,T) -> lists:member(E,flatten(T)).
has_remote(M,F,T) -> lists:any(fun({remote,_,{atom,_,M0},{atom,_,F0}})->{M0,F0}=:={M,F};(_)->false end,flatten(T)).
has_remote_fun(M,F,N,T) -> lists:any(fun({'fun',_,{function,{atom,_,M0},{atom,_,F0},{integer,_,N0}}})->{M0,F0,N0}=:={M,F,N};(_)->false end,flatten(T)).
