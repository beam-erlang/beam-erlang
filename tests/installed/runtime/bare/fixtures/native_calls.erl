%% SPDX-License-Identifier: Apache-2.0
-module(native_calls).
-export([run/1,exports/2,echo/1]).
-compile({beam_native_bindings,[{vm_is_float_1,chosen,float,1}]}).
-import(chosen,[float/1]).
-compile({no_auto_import,[float/1]}).
-compile({beam_private_native_functions,[{invoke,3},{control,2},{fail,1}]}).

run(Profile) ->
    ok = native_modern:run(Profile),
    %% Bare guards and language operators need no public native bindings.
    yes = language(1.0), no = language(atom),
    [2,4] = [X*2 || X <- [1,2], is_integer(X)],
    [a,b] = [a] ++ [b],
    5 = 2+3,
    yes = lazy_guard(1.0),
    Canonical = Profile =:= canonical,
    Modern = Profile =:= modern,
    Custom = Profile =:= custom,
    Canonical = canonical_guard(1.0),
    Custom = custom_guard(1.0),
    Custom = custom_guard(1),
    Custom = imported_guard(1.0),
    Custom = imported_guard(1),
    false = identity_guard(1),
    false = imported_identity_guard(1),
    expected(Canonical,true,fun() -> erlang:is_float(1.0) end),
    expected(Modern,true,fun() -> beam:is_float(1.0) end),
    expected_custom(Profile,fun() -> chosen:float(1.0) end),
    expected_custom(Profile,fun() -> imported_body(1.0) end),
    expected(Canonical,ok,fun() -> erlang:is_float(1.0), ok end),
    expected(Canonical,true,fun() -> F=fun erlang:is_float/1, F(1.0) end),
    expected(Modern,true,fun() -> F=fun beam:is_float/1, F(1.0) end),
    expected_custom(Profile,fun() -> F=fun chosen:float/1, F(1.0) end),
    expected(Canonical,true,fun() -> invoke(erlang,is_float,[1.0]) end),
    expected(Modern,true,fun() -> invoke(beam,is_float,[1.0]) end),
    Canonical = control(exported,[erlang,is_float,1]),
    Modern = control(exported,[beam,is_float,1]),
    case Profile of
        canonical -> selected(erlang,is_float,erts_debug,apply,apply,yield,hibernate);
        modern -> selected(beam,is_float,erts_debug,apply,apply,yield,hibernate);
        custom -> selected(chosen,float,chosen,invoke_fun,invoke,relax,sleep);
        _ ->
            expected(false,unused,fun() -> erlang:yield() end),
            expected(false,unused,fun() -> beam:yield() end),
            expected(false,unused,fun() -> erlang:apply(fun() -> ok end,[]) end),
            expected(false,unused,fun() -> beam:apply(fun() -> ok end,[]) end),
            private_execution()
    end,
    ok.

%% Keep selection observations separate from the calls being tested.
exports(Entries,Selected) ->
    [begin
         Selected = control(exported,[M,F,A]),
         Selected = member({F,A},invoke(M,module_info,[exports]))
     end || {M,F,A} <- Entries, control(loaded,[M])],
    [Selected = control(exported,[M,F,A]) || {M,F,A} <- Entries],
    ok.

language(X) when is_float(X) -> yes;
language(_) -> no.
canonical_guard(X) when erlang:is_float(X) -> true;
canonical_guard(_) -> false.
custom_guard(X) when is_boolean(chosen:float(X)), chosen:float(1.0) -> true;
custom_guard(_) -> false.
identity_guard(X) when chosen:float(X) -> true;
identity_guard(_) -> false.
imported_guard(X) when is_boolean(float(X)), float(1.0) -> true;
imported_guard(_) -> false.
imported_identity_guard(X) when float(X) -> true;
imported_identity_guard(_) -> false.
imported_body(X) -> float(X).
lazy_guard(X) when is_float(X) orelse chosen:float(X) -> yes;
lazy_guard(_) -> no.

expected(true,Value,Fun) -> Value=Fun(), ok;
expected(false,_,Fun) ->
    try Fun() of Value -> fail({expected_undef,Value})
    catch error:undef -> ok
    end.
expected_custom(custom,Fun) -> true=Fun(), ok;
expected_custom(wrong,Fun) -> false=Fun(), ok;
expected_custom(ordinary,Fun) -> ordinary=Fun(), ok;
expected_custom(_,Fun) -> expected(false,unused,Fun).

selected(M,Float,Dirty,Apply2,Apply3,Yield,Hibernate) ->
    undefined = invoke(M,whereis,[native_unregistered]),
    ok = fill(M,2000), ok = erase(M,2000),
    undefined = invoke(M,erase,[1]),
    dirty_cpu = invoke(Dirty,dirty_cpu,[scheduler,type]),
    dirty_io = invoke(Dirty,dirty_io,[scheduler,type]),
    ok = invoke(Dirty,dirty_cpu,[reschedule,1000]),
    try invoke(Dirty,dirty_cpu,[error,notsup]) of unexpected -> ok
    catch error:notsup -> ok
    end,
    true = invoke(M,Yield,[]),
    ok = invoke(M,Apply2,[fun() -> ok end,[]]),
    ok = invoke(M,Apply3,[?MODULE,echo,[ok]]),
    expect_badarg(M,Apply2,[fun() -> ok end,bad]),
    expect_badarg(M,Apply3,[42,echo,[]]),
    true = traced(M,Float,[1.0]),
    undefined = traced(M,erase,[native_key]),
    dirty_cpu = traced(Dirty,dirty_cpu,[scheduler,type]),
    true = traced(M,Yield,[]),
    ok = private_trace(M),
    ok = exception_traits(M),
    ok = private_execution(),
    Parent = control(self,[]),
    {P,R} = control(spawn_monitor,[fun() ->
        F = fun M:Hibernate/0,
        F(),
        receive resume -> Parent ! resumed end
    end]),
    waiting = wait(P,10000),
    {current_function,{M,Hibernate,0}} = control(process_info,[P,current_function]),
    P ! resume,
    receive resumed -> ok after 3000 -> fail(missing_resume) end,
    receive {'DOWN',R,process,P,normal} -> ok after 3000 -> fail(missing_down) end,
    %% Termination must reclaim a child still sleeping in a selected entry.
    {Abandoned,_} = control(spawn_monitor,[fun() -> invoke(M,Hibernate,[]), receive never -> ok end end]),
    waiting = wait(Abandoned,10000),
    ok.

fill(_,0) -> ok;
fill(M,N) -> undefined=invoke(M,put,[N,{N,N+1,[N,N+2]}]), fill(M,N-1).
erase(_,0) -> ok;
erase(M,N) ->
    {N,Next,[N,Next2]}=invoke(M,erase,[N]),
    Next=N+1, Next2=N+2, erase(M,N-1).

expect_badarg(M,F,Args) ->
    try invoke(M,F,Args) of _ -> fail(expected_badarg)
    catch error:badarg:Stack -> [{M,F,Args,_}|_]=Stack, ok
    end.

private_trace(M) ->
    Parent=control(self,[]),
    1=control(trace_pattern,[{M,self,0},true,[]]),
    1=control(trace,[Parent,true,[call,{tracer,builtin_tracer,Parent}]]),
    Parent=control(self,[]),
    1=control(trace,[Parent,false,[call]]),
    1=control(trace_pattern,[{M,self,0},false,[]]),
    receive {trace,Parent,call,Call} -> fail({private_public_trace,Call})
    after 0 -> ok
    end.

exception_traits(M) ->
    {Error,Throw,Exit,Math}=case M of
        chosen -> {fail,raise,stop,chosen};
        beam -> {error,throw,exit,beam_math};
        _ -> {error,throw,exit,math}
    end,
    [try invoke(M,F,[native_reason]) of _ -> fail(expected_exception)
     catch Class:native_reason:Stack ->
         false=stack_member(M,F,Stack), ok
     end || {Class,F} <- [{error,Error},{throw,Throw},{exit,Exit}]],
    try invoke(Math,log,[bad]) of _ -> fail(expected_badarg)
    catch error:badarg:MathStack ->
        [{Math,log,[bad],Info}|_]=MathStack,
        {error_info,#{module:=erl_stdlib_errors}}=find(error_info,Info)
    end,
    ok.

stack_member(M,F,[{M,F,_,_}|_]) -> true;
stack_member(M,F,[_|T]) -> stack_member(M,F,T);
stack_member(_,_,[]) -> false.
find(K,[{K,_}=Pair|_]) -> Pair;
find(K,[_|T]) -> find(K,T).

traced(M,F,Args) ->
    Parent=control(self,[]),
    1=control(trace_pattern,[{M,F,count(Args)},true,[]]),
    1=control(trace,[Parent,true,[call,{tracer,builtin_tracer,Parent}]]),
    Value=invoke(M,F,Args),
    1=control(trace,[Parent,false,[call]]),
    1=control(trace_pattern,[{M,F,count(Args)},false,[]]),
    receive {trace,Parent,call,{M,F,Args}} -> Value
    after 3000 -> fail(missing_native_trace)
    end.

private_execution() ->
    Parent=control(self,[]),
    {P,R}=control(spawn_monitor,[fun() -> Parent ! private_child end]),
    receive private_child -> ok after 3000 -> fail(missing_child) end,
    receive {'DOWN',R,process,P,normal} -> ok after 3000 -> fail(missing_down) end,
    ok.
wait(_,0) -> timeout;
wait(P,N) ->
    case control(process_info,[P,status]) of
        {status,waiting} -> waiting;
        _ -> control(yield,[]), wait(P,N-1)
    end.
echo(X) -> X.
count([]) -> 0;
count([_|T]) -> 1+count(T).
member(X,[X|_]) -> true;
member(X,[_|T]) -> member(X,T);
member(_,[]) -> false.

invoke(M,F,Args) -> erlang:apply(M,F,Args).
control(self,[]) -> erlang:self();
control(exported,[M,F,A]) -> erlang:function_exported(M,F,A);
control(loaded,[M]) -> erlang:module_loaded(M);
control(spawn_monitor,[Fun]) -> erlang:spawn_monitor(Fun);
control(process_info,[P,K]) -> erlang:process_info(P,K);
control(yield,[]) -> erlang:yield();
control(trace_pattern,[MFA,Flag,Opts]) ->
    case erlang:function_exported(erlang,trace_pattern,3) of
        true -> erlang:trace_pattern(MFA,Flag,Opts);
        false -> erts_internal:trace_pattern(MFA,Flag,Opts)
    end;
control(trace,[Pid,Flag,Opts]) ->
    case erlang:function_exported(erlang,trace,3) of
        true -> erlang:trace(Pid,Flag,Opts);
        false -> erts_internal:trace(Pid,Flag,Opts)
    end.

fail(Reason) -> erlang:error(Reason).
