%% SPDX-License-Identifier: Apache-2.0
%% Public source calls stay opaque until assembly. Language operations retain
%% their semantics independently of the target host's callable exports.
native_options(Forms, Opts) ->
    lists:append([case V of L when is_list(L) -> L; _ -> [V] end ||
                     {attribute,_,compile,V} <- Forms]) ++ Opts.

native_lint_forms(Forms0, Opts) ->
    Forms = native_selection_forms(native_evaluator_forms(Forms0,Opts),Opts),
    Bindings = proplists:get_value(beam_native_bindings,native_options(Forms,Opts),[]),
    Imports = maps:from_list([{{F,A},M} || {attribute,_,import,{M,FAs}} <- Forms,
                                          {F,A} <- FAs]),
    Context = #{bindings=>Bindings,imports=>Imports},
    [native_lint_form(F,Context) || F <- Forms].

native_lint_form({function,L,N,A,Cs}, B) ->
    {function,L,N,A,[native_lint_clause(C,B) || C <- Cs]};
native_lint_form(F, _) -> F.

native_lint_clause({clause,L,Ps,Gs,Body}, B) ->
    {clause,L,Ps,[[native_lint_guard(G,B) || G <- Seq] || Seq <- Gs],
     native_lint_body(Body,B)}.

native_lint_body({clause,_,_,_,_}=C, B) -> native_lint_clause(C,B);
native_lint_body({call,L,{remote,R,{atom,ML,M},{atom,FL,F}},Args}, B) ->
    Args1 = native_lint_body(Args,B),
    %% NIF declaration diagnostics follow the target contract. The executable
    %% source call remains public and opaque through optimization.
    case lists:member({vm_load_nif_2,M,F,length(Args)},maps:get(bindings,B)) of
        true -> {call,L,{remote,R,{atom,ML,erlang},{atom,FL,load_nif}},Args1};
        false -> {call,L,{remote,R,{atom,ML,M},{atom,FL,F}},Args1}
    end;
native_lint_body(T, B) when is_tuple(T) ->
    list_to_tuple([native_lint_body(E,B) || E <- tuple_to_list(T)]);
native_lint_body(L, B) when is_list(L) -> [native_lint_body(E,B) || E <- L];
native_lint_body(T, _) -> T.

native_lint_guard({call,L,{atom,FL,F},Args}, B) ->
    case maps:find({F,length(Args)},maps:get(imports,B)) of
        {ok,M} -> native_lint_guard({call,L,{remote,L,{atom,FL,M},{atom,FL,F}},Args},B);
        error -> {call,L,{atom,FL,native_source_guard_name(erlang,F)},
                  [native_lint_guard(A,B) || A <- Args]}
    end;
native_lint_guard({call,L,{remote,R,{atom,ML,M},{atom,FL,F}},Args}, B) ->
    {ID,Semantic} = native_guard_target(M,F,length(Args),maps:get(bindings,B)),
    case ID of
        undefined -> {call,L,{remote,R,{atom,ML,M},{atom,FL,native_source_guard_name(M,F)}},
                      [native_lint_guard(A,B) || A <- Args]};
        _ -> {call,L,{remote,R,{atom,ML,erlang},{atom,FL,Semantic}},
              [native_lint_guard(A,B) || A <- Args]}
    end;
native_lint_guard(T, B) when is_tuple(T) ->
    list_to_tuple([native_lint_guard(E,B) || E <- tuple_to_list(T)]);
native_lint_guard(L, B) when is_list(L) -> [native_lint_guard(E,B) || E <- L];
native_lint_guard(T, _) -> T.

%% Synthetic guard names are introduced only after source validation. They
%% must never make an unrelated user function a language guard.
native_source_guard_name(erlang,F) ->
    case lists:prefix("$beam_guard_",atom_to_list(F)) of
        true -> '$beam_invalid_guard';
        false -> F
    end;
native_source_guard_name(_,F) -> F.

native_calls(Forms0, #compile{options=Opts}=St) ->
    Forms = native_selection_forms(native_evaluator_forms(Forms0,Opts),Opts),
    AllOpts = native_options(Forms,Opts),
    Bindings = proplists:get_value(beam_native_bindings,AllOpts,[]),
    ok = native_validate_bindings(Bindings),
    [{attribute,_,module,Module}] = [F || F={attribute,_,module,_} <- Forms],
    Locals = [{F,A} || {function,_,F,A,_} <- Forms],
    Imports = maps:from_list([{{F,A},M} || {attribute,_,import,{M,FAs}} <- Forms,
                                          {F,A} <- FAs]),
    Disabled = lists:append([FAs || {no_auto_import,FAs} <- AllOpts]),
    S0 = #{next=>0,aliases=>[],bindings=>Bindings,locals=>Locals,module=>Module,
           imports=>Imports,disabled=>Disabled,
           no_auto_import=>lists:member(no_auto_import,AllOpts),
           auto_imports=>proplists:get_value(beam_auto_imports,AllOpts,[]),
           private=>lists:member(beam_private_native_calls,AllOpts),
           private_mfas=>proplists:get_value(beam_private_native_mfas,AllOpts,[]),
           private_functions=>proplists:get_value(beam_private_native_functions,AllOpts,[]) ++
               [{beam_native_language,2} || lists:member(beam_native_evaluator,AllOpts)]},
    {Code,S} = lists:mapfoldl(fun native_form/2,S0,Forms),
    Attr = {attribute,0,beam_native_calls,maps:get(aliases,S)},
    {ok,[Attr|Code],St}.

native_form({function,L,N,A,Cs}, S0) ->
    Private = maps:get(private,S0) orelse lists:member({N,A},maps:get(private_functions,S0)),
    {Cs1,S} = lists:mapfoldl(fun native_clause/2,S0#{private=>Private},Cs),
    {{function,L,N,A,Cs1},S#{private=>maps:get(private,S0)}};
native_form(F,S) -> {F,S}.

native_clause({clause,L,Ps,Gs,B},S0) ->
    {Ps1,S1} = native_walk(Ps,guard,S0),
    {Gs1,S2} = native_walk(Gs,guard,S1),
    {B1,S} = native_walk(B,body,S2),
    {{clause,L,Ps1,Gs1,B1},S}.

native_walk({clause,_,_,_,_}=C,_,S) -> native_clause(C,S);
native_walk({Kind,L,E,Qs},body,S0) when Kind =:= lc; Kind =:= bc; Kind =:= mc ->
    {E1,S1} = native_walk(E,body,S0),
    {Qs1,S} = lists:mapfoldl(fun native_qualifier/2,S1,Qs),
    {{Kind,L,E1,Qs1},S};
native_walk({call,[{beam_native_selection,ID}|L],
             {remote,_,{atom,_,erlang},{atom,_,is_builtin}},
             [{atom,_,M},{atom,_,F},{integer,_,A}]},body,S0) ->
    {Alias,S} = native_alias(selection,M,F,A,ID,S0),
    {{call,L,{remote,L,{atom,L,'$beam_public'},{atom,L,Alias}},[]},S};
native_walk({call,L,{remote,R,{atom,_,M},{atom,_,F}},Args},Mode,S0) ->
    {Args1,S1} = native_walk(Args,Mode,S0),
    case Mode of
        guard ->
            {ID,_Semantic} = native_guard_target(M,F,length(Args),maps:get(bindings,S1)),
            Private = maps:get(private,S1) orelse
                lists:member({M,F,length(Args)},maps:get(private_mfas,S1)),
            Kind = case Private of true -> private_guard; false -> guard end,
            {Alias,S} = native_alias(Kind,M,F,length(Args),ID,S1),
            {{call,L,{remote,R,{atom,L,erlang},{atom,L,Alias}},Args1},S};
        body -> native_public_call(L,R,M,F,Args1,S1)
    end;
native_walk({call,L,{remote,R,M,F},Args},Mode,S0) ->
    {[M1,F1|Args1],S} = native_walk([M,F|Args],Mode,S0),
    {{call,L,{remote,R,native_opaque(L,M1),F1},Args1},S};
native_walk({call,L,{atom,_,F}=C,Args},Mode,S0) ->
    A = length(Args),
    case maps:find({F,A},maps:get(imports,S0)) of
        {ok,M} -> native_walk({call,L,{remote,L,{atom,L,M},C},Args},Mode,S0);
        error when Mode =:= guard ->
            {Args1,S} = native_walk(Args,guard,S0),
            {{call,L,C,Args1},S};
        error ->
            {Args1,S1} = native_walk(Args,body,S0),
            Local = lists:member({F,A},maps:get(locals,S1)),
            Disabled = maps:get(no_auto_import,S1) orelse lists:member({F,A},maps:get(disabled,S1)),
            case {Local,native_local_binding(F,A,S1),Disabled orelse not erl_internal:bif(erlang,F,A)} of
                {true,true,_} -> native_public_call(L,L,maps:get(module,S1),F,Args1,S1);
                {true,_,_} -> {{call,L,C,Args1},S1};
                {false,_,true} -> {{call,L,C,Args1},S1};
                {false,_,false} ->
                    {M,N} = native_auto_target(F,A,S1),
                    native_public_call(L,L,M,N,Args1,S1)
            end
    end;
native_walk({call,L,C,Args},body,S0) ->
    {[C1|Args1],S} = native_walk([C|Args],body,S0),
    {{call,L,native_opaque(L,C1),Args1},S};
native_walk({'fun',L,{function,F,A}}=Fun,body,S) ->
    case lists:member({F,A},maps:get(locals,S)) of
        true -> {Fun,S};
        false ->
            case maps:find({F,A},maps:get(imports,S)) of
                {ok,M} -> {{'fun',L,{function,{atom,L,M},{atom,L,F},{integer,L,A}}},S};
                error ->
                    case erl_internal:bif(erlang,F,A) of
                        true ->
                            {M,N} = native_auto_target(F,A,S),
                            {{'fun',L,{function,{atom,L,M},{atom,L,N},{integer,L,A}}},S};
                        false -> {Fun,S}
                    end
            end
    end;
native_walk(T,Mode,S0) when is_tuple(T) ->
    {L,S} = native_walk(tuple_to_list(T),Mode,S0),
    {list_to_tuple(L),S};
native_walk([H|T],Mode,S0) ->
    {H1,S1} = native_walk(H,Mode,S0),
    {T1,S} = native_walk(T,Mode,S1),
    {[H1|T1],S};
native_walk(T,_,S) -> {T,S}.

native_public_call(L,R,M,F,Args,S0) ->
    A = length(Args),
    Private = maps:get(private,S0) orelse lists:member({M,F,A},maps:get(private_mfas,S0)),
    ID = case Private of
             true -> native_private_id(M,F,A,S0);
             false -> undefined
         end,
    Kind = case ID of undefined -> public; _ -> private end,
    {Alias,S} = native_alias(Kind,M,F,A,ID,S0),
    {{call,L,{remote,R,{atom,L,'$beam_public'},{atom,L,Alias}},Args},S}.

native_opaque(L,E) ->
    {call,L,{remote,L,{atom,L,'$beam_public'},{atom,L,identity}},[E]}.

native_alias(Kind,M,F,A,ID,S0) ->
    Key = {Kind,M,F,A,ID},
    Aliases = maps:get(aliases,S0),
    case lists:keyfind(Key,2,Aliases) of
        {Alias,Key} -> {Alias,S0};
        false ->
            N = maps:get(next,S0),
            Prefix = case Kind of
                         guard -> "$beam_guard_";
                         private_guard -> "$beam_guard_";
                         _ -> "$beam_public_"
                     end,
            Alias = list_to_atom(Prefix++integer_to_list(N)),
            {Alias,S0#{next=>N+1,aliases=>[{Alias,Key}|Aliases]}}
    end.

native_local_binding(F,A,#{module:=M,bindings:=Bindings}) ->
    lists:any(fun({_,M0,F0,A0}) -> {M0,F0,A0} =:= {M,F,A} end,Bindings).

native_private_id(M,F,A,#{bindings:=Bindings}) ->
    case native_canonical_id(M,F,A) of
        undefined ->
            case [ID || {ID,M0,F0,A0} <- Bindings,{M0,F0,A0} =:= {M,F,A}] of
                [ID] -> ID;
                [] -> undefined
            end;
        ID -> ID
    end.

native_auto_target(F,A,#{private:=true}=S) ->
    case native_canonical_id(erlang,F,A) of
        undefined -> native_auto_target(F,A,S#{private=>false});
        _ -> {erlang,F}
    end;
native_auto_target(F,A,#{auto_imports:=Autos,bindings:=Bindings}) ->
    case lists:keyfind({F,A},1,Autos) of
        {{F,A},M,N} -> {M,N};
        false ->
            case [{M,N} || {ID,M,N,A0} <- Bindings,A0 =:= A,
                           {erlang,F0,A1,_} <- [native_contract(ID)],{F0,A1} =:= {F,A}] of
                [{M,N}|_] -> {M,N};
                [] -> {erlang,F}
            end
    end.

native_guard_target(M,F,A,Bindings) ->
    case [{ID,SF} || {ID,M0,F0,A0} <- Bindings,{M0,F0,A0} =:= {M,F,A},
                    {erlang,SF,SA,true} <- [native_contract(ID)],SA =:= A] of
        [Target|_] -> Target;
        [] when M =:= erlang -> native_canonical_guard(F,A);
        [] -> {undefined,undefined}
    end.

native_validate_bindings(Bindings) ->
    {_,_} = lists:foldl(
      fun({ID,M,F,A},{IDs,MFAs}) when is_atom(ID),is_atom(M),is_atom(F),is_integer(A),A >= 0 ->
              {_,_,A,_} = native_contract(ID),
              false = maps:is_key(ID,IDs),
              false = maps:is_key({M,F,A},MFAs),
              {IDs#{ID=>true},MFAs#{{M,F,A}=>true}}
      end,{#{},#{}},Bindings),
    ok.

native_qualifier({Kind,L,Pattern,E},S0)
  when Kind =:= generate; Kind =:= generate_strict;
       Kind =:= b_generate; Kind =:= b_generate_strict;
       Kind =:= m_generate; Kind =:= m_generate_strict ->
    {P1,S1} = native_walk(Pattern,guard,S0),
    {E1,S} = native_walk(E,body,S1),
    {{Kind,L,P1,E1},S};
native_qualifier({zip,L,Qs},S0) ->
    {Qs1,S} = lists:mapfoldl(fun native_qualifier/2,S0,Qs),
    {{zip,L,Qs1},S};
native_qualifier(Q,S) ->
    Test = native_lint_guard(Q,S),
    Overridden = fun(FA) -> lists:member(FA,maps:get(locals,S)) end,
    Mode = case erl_lint:is_guard_test(Test,[],Overridden) of true -> guard; false -> body end,
    native_walk(Q,Mode,S).

%% Private core mechanisms can test a public entry at load time, using the same
%% descriptor requirements as evaluator specialization. No export is added.
native_selection_forms(Forms,Opts) ->
    Probes = proplists:get_value(beam_native_selection_functions,
                                native_options(Forms,Opts),[]),
    Helpers = [begin
                   {_,_,A,_} = native_contract(ID),
                   Args = [{atom,0,M},{atom,0,F},{integer,0,A}],
                   Remote = {remote,0,{atom,0,erlang},{atom,0,is_builtin}},
                   Anno = [{beam_native_selection,ID},{location,0},{generated,true}],
                   {function,0,Name,0,[{clause,0,[],[],[{call,Anno,Remote,Args}]}]}
               end || {Name,ID,M,F,A} <- Probes],
    [Form || Form <- Forms, element(1,Form) =/= eof] ++ Helpers ++
        [Form || Form={eof,_} <- Forms].

