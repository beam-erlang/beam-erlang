%% SPDX-License-Identifier: Apache-2.0
%% Evaluators receive the same explicit target profile as compiled programs.
%% Selected-descriptor tests become load-time constants, never VM lookups.
native_evaluator_forms(Forms,Opts) ->
    AllOpts = native_options(Forms,Opts),
    case lists:member(beam_native_evaluator,AllOpts) of
        false -> Forms;
        true ->
            B = proplists:get_value(beam_native_bindings,AllOpts,[]),
            Autos = proplists:get_value(beam_auto_imports,AllOpts,[]),
            NativeAutos = [{{F,A},M,N} || {ID,M,N,A} <- B,
                                         {erlang,F,A0,_} <- [native_contract(ID)],A0 =:= A],
            AutoTargets = maps:to_list(maps:from_list([{FA,{M,F}} || {FA,M,F} <- NativeAutos++Autos])),
            Guards0 = [{ID,erlang,F,A,F} || {ID,F,A} <- native_guard_contracts()],
            Guards1 = [{ID,M,F,A,Semantic} || {ID,M,F,A} <- B,
                        {erlang,Semantic,A0,true} <- [native_contract(ID)],A0 =:= A],
            Guards = lists:usort(Guards0++Guards1),
            Ops = [{'++',2},{'--',2},{'!',2}],
            Language = lists:usort([{F,A} || {_,F,A} <- native_guard_contracts()]++Ops),
            CanonicalApply = [{vm_apply_2,erlang,apply,2},{vm_apply_3,erlang,apply,3}],
            Apply = lists:usort(CanonicalApply++[T || T={ID,_,_,_} <- B,
                                       ID =:= vm_apply_2 orelse ID =:= vm_apply_3]),
            OpBindings = [{ID,M,F,A} || {ID,M,F,A} <- B,
                             {erlang,Op,A0,_} <- [native_contract(ID)],A0 =:= A,
                             lists:member({Op,A},Ops)],
            LiteralIDs = [vm_list_to_pid_1,vm_list_to_port_1,vm_list_to_ref_1],
            Literals = [{ID,erlang,F,A} || ID <- LiteralIDs,
                                             {erlang,F,A,_} <- [native_contract(ID)]] ++
                       [T || T={ID,_,_,_} <- B, lists:member(ID,LiteralIDs)],
            Selections = lists:usort([{ID,M,F,A} || {ID,M,F,A,_} <- Guards]++Apply++OpBindings++Literals),
            Helpers = [native_eval_auto(AutoTargets),native_eval_language(Language),
                       native_eval_selections(Selections),native_eval_guard_names(Guards),
                       native_eval_apply(Apply)],
            %% Generated helpers precede eof so normal listings retain a valid module.
            [F || F <- Forms, element(1,F) =/= eof] ++ Helpers ++
                [F || F={eof,_} <- Forms]
    end.

native_eval_atom(A) -> {atom,0,A}.
native_eval_integer(N) -> {integer,0,N}.
native_eval_vars(A) -> [{var,0,list_to_atom("NativeArg"++integer_to_list(N))} || N <- lists:seq(1,A)].
native_eval_list(Es) -> lists:foldr(fun(E,T) -> {cons,0,E,T} end,{nil,0},Es).
native_eval_tuple(Es) -> {tuple,0,Es}.
native_eval_call(M,F,Args) -> {call,0,{remote,0,native_eval_atom(M),native_eval_atom(F)},Args}.
native_eval_local(F,Args) -> {call,0,native_eval_atom(F),Args}.
native_eval_clause(Patterns,Body) -> {clause,0,Patterns,[],[Body]}.

native_eval_auto(Targets) ->
    Clauses = [native_eval_clause([native_eval_atom(F),native_eval_integer(A)],
                                  native_eval_tuple([native_eval_atom(M),native_eval_atom(N)])) ||
                  {{F,A},{M,N}} <- Targets],
    F = {var,0,'NativeFunction'},
    Default = native_eval_clause([F,{var,0,'_'}],native_eval_tuple([native_eval_atom(erlang),F])),
    {function,0,beam_native_auto,2,Clauses++[Default]}.

native_eval_language(Operations) ->
    Clauses = [begin
                   Args = native_eval_vars(A),
                   native_eval_clause([native_eval_atom(F),native_eval_list(Args)],
                                      native_eval_call(erlang,F,Args))
               end || {F,A} <- Operations],
    {function,0,beam_native_language,2,Clauses}.

native_eval_selections(Selections) ->
    Clauses = [begin
                   Args = [native_eval_atom(M),native_eval_atom(F),native_eval_integer(A)],
                   {call,_,Remote,Args} = native_eval_call(erlang,is_builtin,Args),
                   Anno = [{beam_native_selection,ID},{location,0},{generated,true}],
                   native_eval_clause(Args,{call,Anno,Remote,Args})
               end || {ID,M,F,A} <- Selections],
    Default = native_eval_clause([{var,0,'_'} || _ <- lists:seq(1,3)],native_eval_atom(false)),
    {function,0,beam_native_selected,3,Clauses++[Default]}.

native_eval_guard_names(Guards) ->
    Clauses = [native_eval_clause([native_eval_atom(M),native_eval_atom(F),native_eval_integer(A)],
                                 native_eval_atom(Semantic)) || {_,M,F,A,Semantic} <- Guards],
    Default = native_eval_clause([{var,0,'_'} || _ <- lists:seq(1,3)],native_eval_atom(undefined)),
    {function,0,beam_native_guard_name,3,Clauses++[Default]}.

native_eval_apply(Apply) ->
    Clauses = [begin
                   Args = [native_eval_atom(M),native_eval_atom(F),native_eval_integer(A)],
                   native_eval_clause(Args,native_eval_local(beam_native_selected,Args))
               end || {_,M,F,A} <- Apply],
    Default = native_eval_clause([{var,0,'_'} || _ <- lists:seq(1,3)],native_eval_atom(false)),
    {function,0,beam_native_apply,3,Clauses++[Default]}.
