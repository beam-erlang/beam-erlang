%% SPDX-License-Identifier: Apache-2.0
%% User MFAs remain data. Only auto-imports and language operations use the
%% host's target profile; descriptor checks have already resolved at load time.
beam_native_is_guard_test(G) ->
    erl_lint:is_guard_test(beam_native_guard_ast(G)).

beam_native_guard_ast({value,_,_}=T) -> T;
beam_native_guard_ast({Tag,_,_}=T)
  when Tag =:= atom; Tag =:= var; Tag =:= integer; Tag =:= float;
       Tag =:= char; Tag =:= string -> T;
beam_native_guard_ast({beam_record_call,L,F,Args}) ->
    {call,L,{remote,L,{atom,L,erlang},{atom,L,F}},
     [beam_native_guard_ast(A) || A <- Args]};
beam_native_guard_ast({call,L,{atom,FL,F},Args}) ->
    {call,L,{atom,FL,beam_native_source_guard_name(erlang,F)},
     [beam_native_guard_ast(A) || A <- Args]};
beam_native_guard_ast({call,L,{remote,R,{atom,ML,M},{atom,FL,F}},Args}) ->
    case beam_native_guard_name(M,F,length(Args)) of
        undefined -> {call,L,{remote,R,{atom,ML,M},{atom,FL,beam_native_source_guard_name(M,F)}},
                      [beam_native_guard_ast(A) || A <- Args]};
        Semantic -> {call,L,{remote,R,{atom,ML,erlang},{atom,FL,Semantic}},
                     [beam_native_guard_ast(A) || A <- Args]}
    end;
beam_native_guard_ast(T) when is_tuple(T) ->
    list_to_tuple([beam_native_guard_ast(E) || E <- tuple_to_list(T)]);
beam_native_guard_ast(L) when is_list(L) -> [beam_native_guard_ast(E) || E <- L];
beam_native_guard_ast(T) -> T.

beam_native_source_guard_name(erlang,F) ->
    case atom_to_list(F) of
        "$beam_guard_"++_ -> '$beam_invalid_guard';
        _ -> F
    end;
beam_native_source_guard_name(_,F) -> F.

beam_native_lint({value,_,_}=T) -> T;
beam_native_lint({Tag,_,_}=T)
  when Tag =:= atom; Tag =:= var; Tag =:= integer; Tag =:= float;
       Tag =:= char; Tag =:= string -> T;
beam_native_lint({beam_record_call,L,F,Args}) ->
    {call,L,{remote,L,{atom,L,erlang},{atom,L,F}},
     [beam_native_lint(A) || A <- Args]};
beam_native_lint({clause,L,Ps,Gs,Body}) ->
    {clause,L,Ps,beam_native_guard_ast(Gs),beam_native_lint(Body)};
beam_native_lint(T) when is_tuple(T) ->
    list_to_tuple([beam_native_lint(E) || E <- tuple_to_list(T)]);
beam_native_lint(L) when is_list(L) -> [beam_native_lint(E) || E <- L];
beam_native_lint(T) -> T.

beam_native_guard_expr({value,_,_}=T) -> T;
beam_native_guard_expr({Tag,_,_}=T)
  when Tag =:= atom; Tag =:= var; Tag =:= integer; Tag =:= float;
       Tag =:= char; Tag =:= string -> T;
beam_native_guard_expr({call,L,{atom,_,F},Args}) ->
    {beam_language_call,L,type_test(F),[beam_native_guard_expr(A) || A <- Args]};
beam_native_guard_expr({call,L,{remote,_,{atom,_,M},{atom,_,F}},Args}) ->
    Function = case M of erlang -> type_test(F); _ -> F end,
    {beam_guard_call,L,M,Function,[beam_native_guard_expr(A) || A <- Args]};
beam_native_guard_expr(T) when is_tuple(T) ->
    list_to_tuple([beam_native_guard_expr(E) || E <- tuple_to_list(T)]);
beam_native_guard_expr(L) when is_list(L) -> [beam_native_guard_expr(E) || E <- L];
beam_native_guard_expr(T) -> T.

beam_native_language_call(Op,Args,_Anno,Bs,none,RBs) ->
    ret_expr(beam_native_language(Op,Args),Bs,RBs);
beam_native_language_call(Op,Args,Anno,Bs,Ef,RBs) ->
    {M,F} = beam_native_auto(Op,length(Args)),
    do_apply(M,F,Args,Anno,Bs,Ef,RBs).

bif_apply([M,F,[_,_]=Args],Anno,Bs,Ef,RBs) ->
    case beam_native_apply(M,F,2) of
        true -> bif_apply(Args,Anno,Bs,Ef,RBs);
        false -> do_apply(M,F,Args,Anno,Bs,Ef,RBs)
    end;
bif_apply([M,F,[_,_,_]=Args],Anno,Bs,Ef,RBs) ->
    case beam_native_apply(M,F,3) of
        true -> bif_apply(Args,Anno,Bs,Ef,RBs);
        false -> do_apply(M,F,Args,Anno,Bs,Ef,RBs)
    end;
bif_apply([M,F,Args],Anno,Bs,Ef,RBs) -> do_apply(M,F,Args,Anno,Bs,Ef,RBs);
bif_apply([Fun,Args],Anno,Bs,Ef,RBs) -> do_apply(Fun,Args,Anno,Bs,Ef,RBs).
