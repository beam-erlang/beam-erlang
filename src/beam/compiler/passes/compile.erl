%%
%% %CopyrightBegin%
%%
%% SPDX-License-Identifier: Apache-2.0
%%
%% Copyright Ericsson AB 1996-2026. All Rights Reserved.
%%
%% Licensed under the Apache License, Version 2.0 (the "License");
%% you may not use this file except in compliance with the License.
%% You may obtain a copy of the License at
%%
%%     http://www.apache.org/licenses/LICENSE-2.0
%%
%% Unless required by applicable law or agreed to in writing, software
%% distributed under the License is distributed on an "AS IS" BASIS,
%% WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
%% See the License for the specific language governing permissions and
%% limitations under the License.
%%
%% %CopyrightEnd%
%%
%% Purpose: Run the Erlang compiler.

-module(compile).
-moduledoc """
Private pass driver for the BEAM compiler engine.

Options are supplied explicitly. The supported surface is abstract-form input,
bytecode or intermediate-form results, diagnostics, and compiler subpasses.
Source acquisition and output publication belong to the tooling host. File APIs,
environment defaults, profiling, listings and encrypted debug info are absent.
This matching-build module is not a public library API.
""".

-compile([{nowarn_possibly_unsafe_function, {file, consult, 1}},
          {nowarn_possibly_unsafe_function, {erlang, binary_to_term, 1}}]).

%% Private entry points; the tooling engine owns the bytecode result contract.
-export([forms/1,forms/2,noenv_forms/2,format_error/1]).

%% Utility functions for compiler passes.
-export([run_sub_passes/2]).

-export_type([option/0]).
-export_type([forms/0]).
-export_type([comp_ret/0]).

-include("core_parse.hrl").

-import(lists, [member/2,reverse/1,reverse/2,keyfind/3,last/1,
		map/2,flatmap/2,flatten/1,foreach/2,foldr/3,any/2]).


%%----------------------------------------------------------------------

-type abstract_code() :: [erl_parse:abstract_form()].

-doc """
List of Erlang abstract forms, as used by
`forms/2`.
""".
-type forms() :: abstract_code().

-doc "Private pass options; host-side utility modes are rejected.".
-type option() :: atom() | {atom(), term()} | {'d', atom(), term()}.

-type error_description() :: erl_lint:error_description().
-type error_info() :: erl_lint:error_info().
-type errors()   :: [{file:filename(), [error_info()]}].
-type warnings() :: [{file:filename(), [error_info()]}].
-type mod_ret()  :: {'ok', module()}
                  | {'ok', module(), cerl:c_module()} %% with option 'to_core'
                  | {'ok',                            %% with option 'to_pp'
                     module() | [],                   %% module() if 'to_exp'
                     abstract_code()}
                  | {'ok', module(), warnings()}.
-type bin_ret()  :: {'ok', module(), binary()}
                  | {'ok', module(), binary(), warnings()}.
-type err_ret()  :: 'error' | {'error', errors(), warnings()}.
-type comp_ret() :: mod_ret() | bin_ret() | err_ret().


%%----------------------------------------------------------------------

%%
%%  Exported functions
%%


%% Every entry uses explicit options and returns data. Intermediate Core results
%% are retained for the private debug-info backend; source and file APIs are absent.
-spec forms(forms()) -> comp_ret().
forms(Forms) -> forms(Forms, []).

-spec forms(forms(), [option()] | option()) -> comp_ret().
forms(Forms, Options) -> noenv_forms(Forms, Options).

-spec noenv_forms(forms(), [option()] | option()) -> comp_ret().
noenv_forms(Forms, Options) when is_list(Options) ->
    do_compile({forms,Forms}, [binary|Options]);
noenv_forms(Forms, Option) when is_atom(Option) ->
    noenv_forms(Forms, [Option]).

%%%
%%% Run sub passes from a compiler pass.
%%%

-doc false.
-spec run_sub_passes([term()], term()) -> term().

run_sub_passes(Ps, St) ->
    Runner = fun(_Name, Run, State) -> Run(State) end,
    run_sub_passes_1(Ps, Runner, St).

%%
%%  Local functions
%%

-define(pass(P), {P,fun P/2}).
-define(pass(P,T), {P,fun T/1,fun P/2}).

do_compile(Input, Opts0) ->
    validate_tooling_options(Input, Opts0),
    Expanded = expand_opts(Opts0),
    validate_tooling_options(Input, Expanded),
    Opts = [binary,return_errors,return_warnings|Expanded],
    IntFun = internal_fun(Input, Opts),

    %% Some tools, like Dialyzer, has already spawned workers
    %% and spawning extra workers actually slow the compilation
    %% down instead of speeding it up, so we provide a mechanism
    %% to bypass the compiler process.
    case lists:member(no_spawn_compiler_process, Opts) of
        true ->
            IntFun();
        false ->
            {Pid,Ref} =
                spawn_monitor(fun() ->
                                      exit(IntFun())
                              end),
            receive
                {'DOWN',Ref,process,Pid,Rep} -> Rep
            end
    end.

internal_fun(Input, Opts) ->
    fun() ->
            try
                internal(Input, Opts)
            catch
                Class:Reason:Stk ->
                    internal_error(Class, Reason, Stk)
            end
    end.

internal_error(Class, Reason, Stk) ->
    Error = ["\n*** Internal compiler error ***\n",
             format_error_reason(Class, Reason, Stk),
             "\n"],
    {error,[{"",[{none,?MODULE,{internal_compiler_error,Error}}]}],[]}.

expand_opts(Opts) ->
    foldr(fun expand_opt/2, [], Opts).

expand_opt(basic_validation, Os) ->
    [no_code_generation,to_pp,binary|Os];
expand_opt(strong_validation, Os) ->
    [no_code_generation,to_kernel,binary|Os];
expand_opt(report, Os) ->
    [report_errors,report_warnings|Os];
expand_opt(return, Os) ->
    [return_errors,return_warnings|Os];
expand_opt(r26, Os) ->
    [no_bsm_opt | expand_opt(r27, Os)];
expand_opt(r27, Os) ->
    [no_long_atoms, compressed_literals | Os];
expand_opt(beam_debug_info, Os) ->
    [beam_debug_info, no_copt, no_bsm_opt, no_bool_opt,
     no_share_opt, no_recv_opt, no_ssa_opt, no_throw_opt | Os];
expand_opt(no_type_opt=O, Os) ->
    %% Be sure to keep the no_type_opt option so that it will
    %% be recorded in the BEAM file, allowing the test suites
    %% to recompile the file with this option.
    [O,no_ssa_opt_type_start,
     no_ssa_opt_type_continue,
     no_ssa_opt_type_finish | Os];
expand_opt(no_module_opt=O, Os) ->
    [O,no_recv_opt | Os];
expand_opt({check_ssa,Tag}, Os) ->
    [check_ssa, Tag | Os];
expand_opt(O, Os) -> [O|Os].

-doc """
Uses an `ErrorDescriptor` and returns a deep list of characters that describes
the error.

This function is usually called implicitly when an `ErrorInfo`
structure is processed.
""".
-spec format_error(ErrorDescription :: error_description()) -> string().

format_error({unsupported_compiler_option,Option}) ->
    io_lib:format("unsupported BEAM compiler option: ~tp",[Option]);
format_error({compiler_exception,Class,Reason,Stack}) ->
    format_error_reason(Class,Reason,Stack);
format_error({compiler_result,Other}) ->
    io_lib:format("compiler did not produce bytecode: ~tp",[Other]);
format_error({internal_compiler_error,Text}) ->
    Text;
format_error({obsolete_option,Ver}) ->
    io_lib:fwrite("the ~p option is no longer supported", [Ver]);
format_error(no_crypto) ->
    "this system is not configured with crypto support.";
format_error(bad_crypto_key) ->
    "invalid crypto key.";
format_error(no_crypto_key) ->
    "no crypto key supplied.";
format_error({open,E}) ->
    io_lib:format("open error '~ts'", [file:format_error(E)]);
format_error({epp,E}) ->
    epp:format_error(E);
format_error(write_error) ->
    "error writing file";
format_error({write_error, Error}) ->
    io_lib:format("error writing file: ~ts", [file:format_error(Error)]);
format_error({rename,From,To,Error}) ->
    io_lib:format("failed to rename ~ts to ~ts: ~ts",
		  [From,To,file:format_error(Error)]);
format_error({parse_transform,M,{C,R,Stk}}) ->
    E = format_error_reason(C, R, Stk),
    io_lib:format("error in parse transform '~ts':\n~ts", [M, E]);
format_error({undef_parse_transform,M}) ->
    io_lib:format("undefined parse transform '~ts'", [M]);
format_error({core_transform,M,{C,R,Stk}}) ->
    E = format_error_reason(C, R, Stk),
    io_lib:format("error in core transform '~s':\n~ts", [M, E]);
format_error({crash,Pass,Reason,Stk}) ->
    io_lib:format("internal error in pass ~p:\n~ts", [Pass,format_error_reason({Reason, Stk})]);
format_error({bad_return,Pass,Reason}) ->
    io_lib:format("internal error in pass ~p: bad return value:\n~tP", [Pass,Reason,20]);
format_error({module_name,Mod,Filename}) ->
    io_lib:format("Module name '~s' does not match file name '~ts'", [Mod,Filename]).

format_error_reason({Reason, Stack}) when is_list(Stack) ->
    format_error_reason(error, Reason, Stack).

format_error_reason(Class, Reason, Stack) ->
    StackFun = fun
	(escript, run,      2) -> true;
	(escript, start,    1) -> true;
	(init,    start_it, 1) -> true;
	(init,    start_em, 1) -> true;
	(_Mod, _Fun, _Arity)   -> false
    end,
    FormatFun = fun (Term, _) -> io_lib:format("~tp", [Term]) end,
    Opts = #{stack_trim_fun => StackFun,
             format_fun => FormatFun},
    erl_error:format_exception(Class, Reason, Stack, Opts).

%% The compile state record.
-record(compile, {filename=""      :: file:filename(),
                  dir=""           :: file:filename(),
                  base=""          :: file:filename(),
                  ifile=""         :: file:filename(),
                  module=[]        :: module() | [],
                  abstract_code=[] :: abstract_code(), %Abstract code for debugger.
                  options=[]       :: [option()],  %Options for compilation
                  mod_options=[]   :: [option()], %Options for module_info
                  errors=[]        :: errors(),
                  warnings=[]      :: warnings(),
                  extra_chunks=[]  :: [{binary(), binary()}]}).

internal({forms,Forms}, Opts0) ->
    Ps = select_passes(standard_passes(), Opts0),
    Source = proplists:get_value(source, Opts0, ""),
    Opts1 = proplists:delete(source, Opts0),
    Compile = build_compile(Opts1),
    NewForms = case with_columns(Opts0) of
                   true ->
                       Forms;
                   false ->
                       strip_columns(Forms)
               end,
    internal_comp(Ps, NewForms, Source, Compile).

build_compile(Opts0) ->
    ExtraChunks = proplists:get_value(extra_chunks, Opts0, []),
    Opts1 = proplists:delete(extra_chunks, Opts0),
    State = #compile{options=Opts1,mod_options=Opts1,extra_chunks=ExtraChunks},
    metadata_add_features(proplists:get_value(features,Opts1,[]),State).

internal_comp(Passes, Code0, File, St0) ->
    St1 = St0#compile{filename=File,dir=filename:dirname(File),
                      base=filename:basename(File,".erl"),ifile=File},
    case fold_comp(Passes, runner(St1), Code0, St1) of
        {ok,Code,St2} -> comp_ret_ok(Code, St2);
        {error,St2} -> comp_ret_err(St2)
    end.

fold_comp([{delay,Ps0}|Passes], Run, Code, #compile{options=Opts}=St) ->
    Ps = select_passes(Ps0, Opts) ++ Passes,
    fold_comp(Ps, Run, Code, St);
fold_comp([{Name,Test,Pass}|Ps], Run, Code, St) ->
    case Test(St) of
	false ->				%Pass is not needed.
	    fold_comp(Ps, Run, Code, St);
	true ->					%Run pass in the usual way.
	    fold_comp([{Name,Pass}|Ps], Run, Code, St)
    end;
fold_comp([{Name,Pass}|Ps], Run, Code0, St0) ->
    try Run({Name,Pass}, Code0, St0) of
	{ok,Code,St1} ->
            fold_comp(Ps, Run, Code, St1);
	{error,_St1}=Error ->
            Error
    catch
        error:Reason:Stk ->
	    Es = [{St0#compile.ifile,[{none,?MODULE,{crash,Name,Reason,Stk}}]}],
	    {error,St0#compile{errors=St0#compile.errors ++ Es}}
    end;
fold_comp([], _Run, Code, St) -> {ok,Code,St}.

run_sub_passes_1([{Name,Run}|Ps], Runner, St0)
  when is_atom(Name), is_function(Run, 1) ->
    try Runner(Name, Run, St0) of
        St ->
            run_sub_passes_1(Ps, Runner, St)
    catch
        C:E:Stk ->
            erlang:raise(C, E, Stk)
    end;
run_sub_passes_1([], _, St) -> St.

runner(_State) ->
    fun({_Name,Function}, Code, State) -> Function(Code, State) end.

comp_ret_ok(Code, #compile{warnings=Warn0,module=Mod,options=Opts}=St) ->
    Warn1 = filter_warnings(Warn0, Opts),
    case werror(St) of
        true ->
            comp_ret_err(St);
        false ->
            Warn = messages_per_file(Warn1),
            report_warnings(St#compile{warnings = Warn}),
            Ret1 = case member(binary, Opts) andalso
		       not member(no_code_generation, Opts) of
                       true -> [Code];
                       false -> []
                   end,
            Ret2 = case member(return_warnings, Opts) of
                       true -> Ret1 ++ [Warn];
                       false -> Ret1
                   end,
            list_to_tuple([ok,Mod|Ret2])
    end.

comp_ret_err(#compile{warnings=Warn0,errors=Err0,options=Opts}=St) ->
    Warn = messages_per_file(Warn0),
    Err = messages_per_file(Err0),
    report_errors(St#compile{errors=Err}),
    report_warnings(St#compile{warnings=Warn}),
    case member(return_errors, Opts) of
	true -> {error,Err,Warn};
	false -> error
    end.

werror(#compile{options=Opts,warnings=Ws}) ->
    Ws =/= [] andalso member(warnings_as_errors, Opts).

%% messages_per_file([{File,[Message]}]) -> [{File,[Message]}]
messages_per_file(Ms) ->
    T = lists:sort([{File,M} || {File,Messages} <:- Ms, M <- Messages]),
    PrioMs = [erl_scan, epp, erl_parse],
    {Prio0, Rest} =
        lists:mapfoldl(fun(M, A) ->
                               lists:partition(fun({_,{_,Mod,_}}) -> Mod =:= M;
                                                  (_) -> false
                                               end, A)
                       end, T, PrioMs),
    Prio = lists:sort(fun({_,{L1,_,_}}, {_,{L2,_,_}}) -> L1 =< L2 end,
                      lists:append(Prio0)),
    flatmap(fun mpf/1, [Prio, Rest]).

mpf(Ms) ->
    [{File,[M || {F,M} <- Ms, F =:= File]} ||
	File <- lists:usort([F || {F,_} <:- Ms])].

%% select_passes([Command], Opts) -> [{Name,Function}]
%%  Interpret the lists of commands to return a pure list of passes.
%%
%%  Command can be one of:
%%
%%    {pass,Mod}	Will be expanded to a call to the external
%%			function Mod:module(Code, Options).  This
%%			function must transform the code and return
%%			{ok,NewCode} or {ok,NewCode,Warnings}.
%%			Example: {pass,beam_ssa_codegen}
%%
%%    {Name,Fun}	Name is an atom giving the name of the pass.
%%    			Fun is an 'fun' taking one argument: a #compile{} record.
%%			The fun should return {ok,NewCompileRecord} or
%%			{error,NewCompileRecord}.
%%			Note: ?pass(Name) is equvivalent to {Name,fun Name/1}.
%%			Example: ?pass(lint_module)
%%
%%    {Name,Test,Fun}	Like {Name,Fun} above, but the pass will be run
%%			(and listed by the `time' option) only if Test(St)
%%			returns true.
%%
%%    {src_listing,Ext}	Produces an Erlang source listing with the
%%			the file extension Ext.  (Ext should not contain
%%			a period.)  No more passes will be run.
%%
%%    {listing,Ext}	Produce an listing of the terms in the internal
%%			representation.  The extension of the listing
%%			file will be Ext.  (Ext should not contain
%%			a period.)   No more passes will be run.
%%
%%    done              End compilation at this point.
%%
%%    {done,Ext}        End compilation at this point. Produce a listing
%%                      as with {listing,Ext}, unless 'binary' is
%%                      specified, in which case the current
%%                      representation of the code is returned without
%%                      creating an output file.
%%
%%    {iff,Flag,Cmd}	If the given Flag is given in the option list,
%%			Cmd will be interpreted as a command.
%%			Otherwise, Cmd will be ignored.
%%			Example: {iff,dcg,{listing,"codegen}}
%%
%%    {unless,Flag,Cmd}	If the given Flag is NOT given in the option list,
%%			Cmd will be interpreted as a command.
%%			Otherwise, Cmd will be ignored.
%%			Example: {unless,no_kernopt,{pass,sys_kernopt}}
%%

select_passes([{pass,Mod}|Ps], Opts) ->
    F = fun(Code0, St) ->
		case Mod:module(Code0, St#compile.options) of
		    {ok,Code} ->
			{ok,Code,St};
		    {ok,Code,Ws} ->
			{ok,Code,St#compile{warnings=St#compile.warnings++Ws}};
                    {error,Es} ->
                        {error,St#compile{errors=St#compile.errors ++ Es}};
                    Other ->
                        Es = [{St#compile.ifile,[{none,?MODULE,{bad_return,Mod,Other}}]}],
                        {error,St#compile{errors=St#compile.errors ++ Es}}
		end
	end,
    [{Mod,F}|select_passes(Ps, Opts)];
select_passes([{_,Fun}=P|Ps], Opts) when is_function(Fun) ->
    [P|select_passes(Ps, Opts)];
select_passes([{_,Test,Fun}=P|Ps], Opts) when is_function(Test),
					      is_function(Fun) ->
    [P|select_passes(Ps, Opts)];
select_passes([{src_listing,Ext}|_], _Opts) ->
    erlang:error({unsupported_compiler_listing,Ext});
select_passes([{listing,Ext}|_], _Opts) ->
    erlang:error({unsupported_compiler_listing,Ext});
select_passes([done|_], _Opts) ->
    [];
select_passes([{done,Ext}|_], Opts) ->
    select_passes([{unless,binary,{listing,Ext}}], Opts);
select_passes([{iff,Flag,Pass}|Ps], Opts) ->
    select_cond(Flag, true, Pass, Ps, Opts);
select_passes([{unless,Flag,Pass}|Ps], Opts) ->
    select_cond(Flag, false, Pass, Ps, Opts);
select_passes([{delay,Passes0}|Ps], Opts) when is_list(Passes0) ->
    %% Delay evaluation of compiler options and which compiler passes to run.
    %% Since we must know beforehand whether a listing will be produced, we
    %% will go through the list of passes and evaluate all conditions that
    %% select a list pass.
    case select_list_passes(Passes0, Opts) of
	{done,Passes} ->
	    [{delay,Passes}];
	{not_done,Passes} ->
	    [{delay,Passes}|select_passes(Ps, Opts)]
    end;
select_passes([], _Opts) ->
    [];
select_passes([List|Ps], Opts) when is_list(List) ->
    case select_passes(List, Opts) of
	[] -> select_passes(Ps, Opts);
	Nested ->
	    case last(Nested) of
		{listing,_Fun} -> Nested;
		_Other         -> Nested ++ select_passes(Ps, Opts)
	    end
    end.

select_cond(Flag, ShouldBe, Pass, Ps, Opts) ->
    ShouldNotBe = not ShouldBe,
    case member(Flag, Opts) of
	ShouldBe    -> select_passes([Pass|Ps], Opts);
	ShouldNotBe -> select_passes(Ps, Opts)
    end.

%% select_list_passes([Pass], Opts) -> {done,[Pass]} | {not_done,[Pass]}
%%  Evaluate all conditions having to do with listings in the list of
%%  passes.

select_list_passes(Ps, Opts) ->
    select_list_passes_1(Ps, Opts, []).

select_list_passes_1([{iff,Flag,{listing,Ext}}|Ps], Opts, Acc) ->
    case member(Flag, Opts) of
	true -> erlang:error({unsupported_compiler_listing,Ext});
	false -> select_list_passes_1(Ps, Opts, Acc)
    end;
select_list_passes_1([{iff,Flag,{done,Ext}}|Ps], Opts, Acc) ->
    case member(Flag, Opts) of
	false ->
	    select_list_passes_1(Ps, Opts, Acc);
	true ->
	    {done,case member(binary, Opts) of
		      false -> reverse(Acc, [{listing,Ext}]);
		      true -> reverse(Acc)
		  end}
    end;
select_list_passes_1([{iff=Op,Flag,List0}|Ps], Opts, Acc) when is_list(List0) ->
    case select_list_passes(List0, Opts) of
	{done,List} -> {done,reverse(Acc) ++ List};
	{not_done,List} -> select_list_passes_1(Ps, Opts, [{Op,Flag,List}|Acc])
    end;
select_list_passes_1([{unless=Op,Flag,List0}|Ps], Opts, Acc) when is_list(List0) ->
    case select_list_passes(List0, Opts) of
	{done,List} -> {done,reverse(Acc) ++ List};
	{not_done,List} -> select_list_passes_1(Ps, Opts, [{Op,Flag,List}|Acc])
    end;
select_list_passes_1([P|Ps], Opts, Acc) ->
    select_list_passes_1(Ps, Opts, [P|Acc]);
select_list_passes_1([], _, Acc) ->
    {not_done,reverse(Acc)}.

make_ssa_check_pass(PassFlag) ->
    F = fun (Code, St) ->
                case beam_ssa_check:module(Code, PassFlag) of
                    ok -> {ok, Code, St};
                    {error, Errors} ->
                        {error, St#compile{errors=St#compile.errors++Errors}}
                end
        end,
    {iff, PassFlag, {PassFlag, F}}.

%% The standard passes (almost) always run.

standard_passes() ->
    [?pass(transform_module),

     {iff,'dpp',{listing,"pp"}},
     ?pass(lint_module),

     {iff,'P',{src_listing,"P"}},
     {iff,'to_pp',{done,"P"}},

     {iff,'dabstr',{listing,"abstr"}},
     {iff,'to_abstr',{done,"abstr"}}
     | abstr_passes(verified_abstr)].

abstr_passes(AbstrStatus) ->
    case AbstrStatus of
        non_verified_abstr ->
            [{unless, no_lint, ?pass(lint_module)}];
        verified_abstr ->
            []
    end ++
        [
         {unless,no_docs,?pass(beam_docs)},
         ?pass(remove_doc_attributes),
         ?pass(compile_directives),

         {delay,[{iff,debug_info,?pass(save_abstract_code)}]},

         {delay,[{iff,line_coverage,{pass,sys_coverage}},
                 {iff,beam_debug_info,?pass(beam_debug_info)}]},
         {iff,'dcover',{src_listing,"cover"}},

         ?pass(expand_records),
         {iff,'dexp',{listing,"expand"}},
         {iff,'E',?pass(legalize_vars)},
         {iff,'E',{src_listing,"E"}},
         {iff,'to_exp',{done,"abstr"}},

         %% Conversion to Core Erlang.
         ?pass(core),
         {iff,'dcore',{listing,"core"}},
         {iff,'to_core0',{done,"core"}}
         | core_passes(verified_core)].

core_passes(CoreStatus) ->
    %% Optimization and transforms of Core Erlang code.
    case CoreStatus of
        non_verified_core ->
            [?pass(core_lint_module),
             ?pass(core_compile_directives),
             {unless,no_core_prepare,{pass,sys_core_prepare}},
             {iff,dprep,{listing,"prepare"}}];
        verified_core ->
            [{iff,clint0,?pass(core_lint_module)}]
    end ++
        [
         {delay,
          [{unless,no_copt,
            [{core_old_inliner,fun test_old_inliner/1,fun core_old_inliner/2},
             {iff,doldinline,{listing,"oldinline"}},
             {unless,no_fold,{pass,sys_core_fold}},
             {iff,dcorefold,{listing,"corefold"}},
             {core_inline_module,fun test_core_inliner/1,fun core_inline_module/2},
             {iff,dinline,{listing,"inline"}},
             {core_fold_after_inlining,fun test_any_inliner/1,
              fun core_fold_module_after_inlining/2},
             {iff,dcopt,{listing,"copt"}},
             {unless,no_alias,{pass,sys_core_alias}},
             {iff,dalias,{listing,"core_alias"}},
             ?pass(core_transforms)]},
           {iff,'to_core',{done,"core"}}]}
         | kernel_passes()].

kernel_passes() ->
    %% Optimizations that must be done after all other optimizations.
    [{pass,sys_core_bsm},
     {iff,dcbsm,{listing,"core_bsm"}},

     {iff,clint,?pass(core_lint_module)},

     %% Kernel Erlang and code generation.
     ?pass(core_to_ssa),
     {iff,dssa,{listing,"ssa"}},
     {iff,ssalint,{pass,beam_ssa_lint}},
     {delay,
      [make_ssa_check_pass(pre_ssa_opt),
       {unless,no_bool_opt,{pass,beam_ssa_bool}},
       {iff,dbool,{listing,"bool"}},
       {unless,no_bool_opt,{iff,ssalint,{pass,beam_ssa_lint}}},

       {unless,no_share_opt,{pass,beam_ssa_share}},
       {iff,dssashare,{listing,"ssashare"}},
       {unless,no_share_opt,{iff,ssalint,{pass,beam_ssa_lint}}},

       {unless,no_recv_opt,{pass,beam_ssa_recv}},
       {iff,drecv,{listing,"recv"}},
       {unless,no_recv_opt,{iff,ssalint,{pass,beam_ssa_lint}}},

       {unless,no_bsm_opt,{pass,beam_ssa_bsm}},
       {iff,dssabsm,{listing,"ssabsm"}},
       {unless,no_bsm_opt,{iff,ssalint,{pass,beam_ssa_lint}}},

       {unless,no_ssa_opt,{pass,beam_ssa_opt}},
       make_ssa_check_pass(post_ssa_opt),
       {iff,dssaopt,{listing,"ssaopt"}},
       {unless,no_ssa_opt,{iff,ssalint,{pass,beam_ssa_lint}}},

       {unless,no_throw_opt,{pass,beam_ssa_throw}},
       {iff,dthrow,{listing,"throw"}},
       {unless,no_throw_opt,{iff,ssalint,{pass,beam_ssa_lint}}}]},

     {pass,beam_ssa_pre_codegen},
     {iff,dprecg,{listing,"precodegen"}},
     {iff,ssalint,{pass,beam_ssa_lint}},
     {pass,beam_ssa_codegen},
     {iff,dcg,{listing,"codegen"}},
     {iff,doldcg,{listing,"codegen"}},
     ?pass(beam_validator_strong)
     | asm_passes()].

asm_passes() ->
    %% Assembly level optimisations.
    [{delay,
      [{pass,beam_a},
       {iff,da,{listing,"a"}},
       {unless,no_postopt,
	[{pass,beam_block},
	 {iff,dblk,{listing,"block"}},
	 {unless,no_jopt,{pass,beam_jump}},
	 {iff,djmp,{listing,"jump"}},
	 {pass,beam_clean},
	 {iff,dclean,{listing,"clean"}},
	 {unless,no_stack_trimming,{pass,beam_trim}},
	 {iff,dtrim,{listing,"trim"}},
	 {pass,beam_flatten}]},

       %% If post optimizations are turned off, we still
       %% need to do a few clean-ups to code.
       {iff,no_postopt,[{pass,beam_clean}]},

       {iff,diffable,?pass(diffable)},
       {pass,beam_z},
       {iff,diffable,{listing,"S"}},
       {iff,dz,{listing,"z"}},
       {iff,dopt,{listing,"optimize"}},
       {iff,'S',{listing,"S"}},
       {iff,'to_asm',{done,"S"}}]},
     ?pass(beam_validator_weak),
     ?pass(beam_asm),
     {iff,strip_types,?pass(beam_strip_types)}
     ].

deterministic_filename(#compile{ifile=File,options=Options}) ->
    case member(deterministic,Options) of
        true -> filename:basename(File);
        false -> File
    end.

%% The atom to be used in the proplist of the meta chunk indicating
%% the features used when compiling the module.
-define(META_USED_FEATURES, enabled_features).
-define(META_CHUNK_NAME, <<"Meta">>).

metadata_add_features([], St) ->
    St;
metadata_add_features(Ftrs, #compile{options = CompOpts,
                                     extra_chunks = Extra} = St) ->
    MetaData =
        case proplists:get_value(?META_CHUNK_NAME, Extra) of
            undefined ->
                [];
            Bin ->
                erlang:binary_to_term(Bin)
        end,
    OldFtrs = proplists:get_value(?META_USED_FEATURES, MetaData, []),
    NewFtrs = (Ftrs -- OldFtrs) ++ OldFtrs,
    MetaData1 =
        proplists:from_map(maps:put(?META_USED_FEATURES, NewFtrs,
                                    proplists:to_map(MetaData))),
    Extra1 = proplists:from_map(
               maps:put(?META_CHUNK_NAME,
                        term_to_binary(MetaData1,
                                       ensure_deterministic(CompOpts, [])),
                        proplists:to_map(Extra))),
    St#compile{extra_chunks = Extra1}.

ensure_deterministic(CompOpts, Opts) ->
    case member(deterministic, CompOpts) of
        true ->
            [deterministic | Opts];
        false ->
            Opts
    end.

with_columns(Opts) ->
    case proplists:get_value(error_location, Opts, column) of
        column -> true;
        line -> false
    end.

compile_options([{attribute,_L,compile,C}|Fs]) when is_list(C) ->
    C ++ compile_options(Fs);
compile_options([{attribute,_L,compile,C}|Fs]) ->
    [C|compile_options(Fs)];
compile_options([_F|Fs]) -> compile_options(Fs);
compile_options([]) -> [].

clean_parse_transforms(Fs) ->
    clean_parse_transforms_1(Fs, []).

clean_parse_transforms_1([{attribute,L,compile,C0}|Fs], Acc) when is_list(C0) ->
    C = lists:filter(fun({parse_transform,_}) -> false;
			(_) -> true
		     end, C0),
    clean_parse_transforms_1(Fs, [{attribute,L,compile,C}|Acc]);
clean_parse_transforms_1([{attribute,_,compile,{parse_transform,_}}|Fs], Acc) ->
    clean_parse_transforms_1(Fs, Acc);
clean_parse_transforms_1([F|Fs], Acc) ->
    clean_parse_transforms_1(Fs, [F|Acc]);
clean_parse_transforms_1([], Acc) -> reverse(Acc).

transforms(Os) -> [ M || {parse_transform,M} <- Os ].

transform_module(Code0, #compile{options=Opt}=St) ->
    %% Extract compile options from code into options field.
    case transforms(Opt ++ compile_options(Code0)) of
	[] ->
            %% No parse transforms.
            {ok,Code0,St};
	Ts ->
	    %% Remove parse_transform attributes from the abstract code to
	    %% prevent parse transforms to be run more than once.
	    Code = clean_parse_transforms(Code0),
	    foldl_transform(Ts, Code, St)
    end.

foldl_transform([T|Ts], Code0, St) ->
    Name = "transform " ++ atom_to_list(T),
    case code:ensure_loaded(T) =:= {module,T} andalso
        erlang:function_exported(T, parse_transform, 2) of
        true ->
            Fun = fun(Code, S) ->
                          T:parse_transform(Code, S#compile.options)
                  end,
            Run = runner(St),
            StrippedCode = maybe_strip_columns(Code0, T, St),
            try Run({Name, Fun}, StrippedCode, St) of
                {error,Es,Ws} ->
                    {error,St#compile{warnings=St#compile.warnings ++ Ws,
                                      errors=St#compile.errors ++ Es}};
                {warning, Forms, Ws} ->
                    foldl_transform(Ts, Forms,
                                    St#compile{warnings=St#compile.warnings ++ Ws});
                Forms ->
                    foldl_transform(Ts, Forms, St)
            catch
                Class:Reason:Stk ->
                    Es = [{St#compile.ifile,[{none,compile,
                                              {parse_transform,T,{Class,Reason,Stk}}}]}],
                    {error,St#compile{errors=St#compile.errors ++ Es}}
            end;
        false ->
            Es = [{St#compile.ifile,[{none,compile,
                                      {undef_parse_transform,T}}]}],
            {error,St#compile{errors=St#compile.errors ++ Es}}
    end;
foldl_transform([], Code, St) ->
    %% We may need to strip columns added by parse transforms before returning
    %% them back to the compiler. We pass ?MODULE as a bit of a hack to get the
    %% correct default.
    {ok, maybe_strip_columns(Code, ?MODULE, St), St}.

%%% If a parse transform does not support column numbers it can say so using
%%% the parse_transform_info callback. The error_location is taken from both
%%% compiler options and from the parse transform and if either of them want
%%% to only use lines, we strip columns.
maybe_strip_columns(Code, T, St) ->
    PTErrorLocation =
        case erlang:function_exported(T, parse_transform_info, 0) of
            true ->
                maps:get(error_location, T:parse_transform_info(), column);
            false ->
                column
        end,
    ConfigErrorLocation = proplists:get_value(error_location, St#compile.options, column),
    if 
        PTErrorLocation =:= line; ConfigErrorLocation =:= line ->
            strip_columns(Code);
        true -> Code
    end.

strip_columns(Code) ->
    F = fun(A) -> erl_anno:set_location(erl_anno:line(A), A) end,
    [case Form of
         {eof,{Line,_Col}} ->
             {eof,Line};
         {ErrorOrWarning,{{Line,_Col},Module,Reason}}
           when ErrorOrWarning =:= error;
                ErrorOrWarning =:= warning ->
             {ErrorOrWarning,{Line,Module,Reason}};
         Form ->
             erl_parse:map_anno(F, Form)
     end || Form <- Code].

get_core_transforms(Opts) -> [M || {core_transform,M} <- Opts].

core_transforms(Code, St) ->
    %% The options field holds the complete list of options at this
    Ts = get_core_transforms(St#compile.options),
    foldl_core_transforms(Ts, Code, St).

foldl_core_transforms([T|Ts], Code0, St) ->
    Name = "core transform " ++ atom_to_list(T),
    Fun = fun(Code, S) ->
                  case code:ensure_loaded(T) of
                      {module,T} -> T:core_transform(Code, S#compile.options);
                      {error,Reason} -> erlang:error({core_transform_unavailable,T,Reason})
                  end
          end,
    Run = runner(St),
    try Run({Name, Fun}, Code0, St) of
	Forms ->
	    foldl_core_transforms(Ts, Forms, St)
    catch
        Class:Reason:Stk ->
            Es = [{St#compile.ifile,[{none,compile,
                                      {core_transform,T,{Class,Reason,Stk}}}]}],
            {error,St#compile{errors=St#compile.errors ++ Es}}
    end;
foldl_core_transforms([], Code, St) -> {ok,Code,St}.

%%% Fetches the module name from a list of forms. The module attribute must
%%% be present.
get_module([{attribute,_,module,M} | _]) -> M;
get_module([_ | Rest]) ->
    get_module(Rest).

%%% A #compile state is returned, where St.base has been filled in
%%% with the module name from Forms, as a string, in case it wasn't
%%% set in St (i.e., it was "").
add_default_base(St, Forms) ->
    F = St#compile.filename,
    case F of
	"" ->
 	    M = get_module(Forms),
	    St#compile{base=atom_to_list(M)};
	_ ->
	    St
    end.

lint_module(Code, St) ->
    case erl_lint:module(Code, St#compile.ifile, St#compile.options) of
	{ok,Ws} ->
	    %% Insert name of module as base name, if needed. This is
	    %% for compile:forms to work with listing files.
	    St1 = add_default_base(St, Code),
	    {ok,Code,St1#compile{warnings=St1#compile.warnings ++ Ws}};
	{error,Es,Ws} ->
	    {error,St#compile{warnings=St#compile.warnings ++ Ws,
			      errors=St#compile.errors ++ Es}}
    end.

core_lint_module(Code, St) ->
    case core_lint:module(Code, St#compile.options) of
	{ok,Ws} ->
	    {ok,Code,St#compile{warnings=St#compile.warnings ++ Ws}};
	{error,Es,Ws} ->
	    {error,St#compile{warnings=St#compile.warnings ++ Ws,
			      errors=St#compile.errors ++ Es}}
    end.

expand_records(Code0, #compile{options=Opts}=St) ->
    Code = erl_expand_records:module(Code0, Opts),
    {ok,Code,St}.

legalize_vars(Code0, St) ->
    Code = map(fun(F={function,_,_,_,_}) ->
                       erl_pp:legalize_vars(F);
                  (F) ->
                       F
               end, Code0),
    {ok,Code,St}.

%% Add all -compile() directives to #compile.options
compile_directives(Forms, St) ->
    Opts = [C || {attribute,_,compile,C} <- Forms],
    compile_directives_1(Opts, Forms, St).

core_compile_directives(Core, St) ->
    Attrs = [{cerl:concrete(Name),cerl:concrete(Value)} ||
                {Name,Value} <:- cerl:module_attrs(Core)],
    Opts = [C || {compile,C} <- Attrs],
    compile_directives_1(Opts, Core, St).

compile_directives_1(Opts1, Forms, #compile{options=Opts0}=St0) ->
    Opts = expand_opts(flatten(Opts1)) ++ Opts0,
    validate_tooling_options({forms,[]},Opts),
    St1 = St0#compile{options=Opts},
    case any_obsolete_option(Opts) of
        {yes,Opt} ->
            Error = {St1#compile.ifile,[{none,?MODULE,{obsolete_option,Opt}}]},
            St = St1#compile{errors=[Error|St1#compile.errors]},
            {error,St};
        no ->
            {ok,Forms,St1}
    end.

any_obsolete_option([Opt|Opts]) ->
    case is_obsolete(Opt) of
        true -> {yes,Opt};
        false -> any_obsolete_option(Opts)
    end;
any_obsolete_option([]) -> no.

is_obsolete(r18) -> true;
is_obsolete(r19) -> true;
is_obsolete(r20) -> true;
is_obsolete(r21) -> true;
is_obsolete(r22) -> true;
is_obsolete(r23) -> true;
is_obsolete(r24) -> true;
is_obsolete(r25) -> true;
is_obsolete(no_badrecord) -> true;
is_obsolete(no_bs_create_bin) -> true;
is_obsolete(no_bs_match) -> true;
is_obsolete(no_bsm3) -> true;
is_obsolete(no_get_hd_tl) -> true;
is_obsolete(no_init_yregs) -> true;
is_obsolete(no_make_fun3) -> true;
is_obsolete(no_min_max_bifs) -> true;
is_obsolete(no_put_tuple2) -> true;
is_obsolete(no_shared_fun_wrappers) -> true;
is_obsolete(no_swap) -> true;
is_obsolete(no_utf8_atoms) -> true;
is_obsolete(_) -> false.

core(Forms, #compile{options=Opts}=St) ->
    {ok,Core,Ws} = v3_core:module(Forms, Opts),
    Mod = cerl:concrete(cerl:module_name(Core)),
    {ok,Core,St#compile{module=Mod,options=Opts,
                        warnings=St#compile.warnings++Ws}}.

core_fold_module_after_inlining(Code0, #compile{options=Opts}=St) ->
    %% Inlining may produce code that generates spurious warnings.
    %% Ignore all warnings.
    {ok,Code,_Ws} = sys_core_fold:module(Code0, Opts),
    {ok,Code,St}.

core_to_ssa(Code0, #compile{options=Opts,warnings=Ws0}=St) ->
    {ok,Code,Ws} = beam_core_to_ssa:module(Code0, Opts),
    case Ws =:= [] orelse test_core_inliner(St) of
	false ->
	    {ok,Code,St#compile{warnings=Ws0++Ws}};
	true ->
	    %% cerl_inline may produce code that generates spurious
	    %% warnings. Ignore any such warnings.
	    {ok,Code,St}
    end.

test_old_inliner(#compile{options=Opts}) ->
    %% The point of this test is to avoid loading the old inliner
    %% if we know that it will not be used.
    any(fun({inline,_}) -> true;
	   (_) -> false
	end, Opts).

test_core_inliner(#compile{options=Opts}) ->
    case any(fun(no_inline) -> true;
		(_) -> false
	     end, Opts) of
	true -> false;
	false ->
	    any(fun(inline) -> true;
		   (_) -> false
		end, Opts)
    end.

test_any_inliner(St) ->
    test_old_inliner(St) orelse test_core_inliner(St).

core_old_inliner(Code0, #compile{options=Opts}=St) ->
    {ok,Code} = sys_core_inline:module(Code0, Opts),
    {ok,Code,St}.

core_inline_module(Code0, #compile{options=Opts}=St) ->
    Code = cerl_inline:core_transform(Code0, Opts),
    {ok,Code,St}.

save_abstract_code(Code, St) ->
    {ok,Code,St#compile{abstract_code=erl_parse:anno_to_term(Code)}}.

-define(META_DOC_CHUNK, <<"Docs">>).


%% Adds documentation attributes to extra_chunks (beam file)
beam_docs(Code, #compile{dir = Dir, options = Options,
                         extra_chunks = ExtraChunks }=St) ->
    SourceName = deterministic_filename(St),
    case beam_doc:main(Dir, SourceName, Code, Options) of
        {ok, Docs, Ws} ->
            Binary = term_to_binary(Docs, [deterministic, compressed]),
            MetaDocs = [{?META_DOC_CHUNK, Binary} | ExtraChunks],
            {ok, Code, St#compile{extra_chunks = MetaDocs,
                                  warnings = St#compile.warnings ++ Ws}};
        {error, no_docs} ->
            {ok, Code, St}
    end.

%% Strips documentation attributes from the code
remove_doc_attributes(Code, St) ->
    {ok, [Attr || Attr <- Code, not is_doc_attribute(Attr)], St}.


is_doc_attribute(Attr) ->
    case Attr of
        {attribute, _Anno, DocAttr, _Meta}
          when DocAttr =:= doc; DocAttr =:= moduledoc; DocAttr =:= docformat ->
            true;
        _ -> false
    end.

debug_info(St) ->
    {DebugInfo,Options}=debug_info_chunk(St),
    case member(encrypt_debug_info,Options) of
        true -> {error,[{none,?MODULE,{unsupported_compiler_option,encrypt_debug_info}}]};
        false -> {ok,DebugInfo,Options}
    end.

beam_debug_info(Code0, #compile{}=St) ->
    {ok,Code} = sys_coverage:beam_debug_info(Code0),
    {ok,Code,St}.

debug_info_chunk(#compile{mod_options=ModOpts0,
                          options=CompOpts,
                          abstract_code=Abst}) ->
    AbstOpts = cleanup_compile_options(ModOpts0),
    {Backend,Metadata,ModOpts} =
        case proplists:get_value(debug_info, CompOpts, false) of
            {OptBackend,OptMetadata} when is_atom(OptBackend) ->
                ModOpts1 = proplists:delete(debug_info, ModOpts0),
                {OptBackend,OptMetadata,ModOpts1};
            true ->
                ModOpts1 = proplists:delete(debug_info, ModOpts0),
                {erl_abstract_code,{Abst,AbstOpts},[debug_info | ModOpts1]};
            false ->
                {erl_abstract_code,{none,AbstOpts},ModOpts0}
        end,
    DebugInfo = term_to_binary({debug_info_v1,Backend,Metadata},
                               ensure_deterministic(CompOpts, [compressed])),
    {DebugInfo, ModOpts}.

cleanup_compile_options(Opts) ->
    IsDeterministic = lists:member(deterministic, Opts),
    lists:filter(fun(Opt) ->
                         keep_compile_option(Opt, IsDeterministic)
                 end, Opts).

%% Include paths and current directory don't affect compilation, but they might
%% be helpful so we include them unless we're doing a deterministic build.
keep_compile_option({i, _}, Deterministic) ->
    not Deterministic;
keep_compile_option({cwd, _}, Deterministic) ->
    not Deterministic;
%% We are storing abstract, not asm or core.
keep_compile_option(from_asm, _Deterministic) ->
    false;
keep_compile_option(from_core, _Deterministic) ->
    false;
keep_compile_option(from_abstr, _Deterministic) ->
    false;
%% Parse transform and macros have already been applied.
keep_compile_option({parse_transform, _}, _Deterministic) ->
    false;
keep_compile_option({d, _, _}, _Deterministic) ->
    false;
%% Do not affect compilation result on future calls.
keep_compile_option(Option, _Deterministic) ->
    effects_code_generation(Option).

beam_validator_strong(Code, St) ->
    beam_validator_1(Code, St, strong).

beam_validator_weak(Code, St) ->
    beam_validator_1(Code, St, weak).

beam_validator_1(Code, #compile{errors=Errors0}=St, Level) ->
    case beam_validator:validate(Code, Level) of
        ok ->
            {ok, Code, St};
        {error, Es} ->
            {error, St#compile{errors=Errors0 ++ Es}}
    end.

beam_asm(Code0, #compile{ifile=File,extra_chunks=ExtraChunks,options=CompilerOpts}=St) ->
    case debug_info(St) of
	{ok,DebugInfo,Opts0} ->
	    Opts1 = [O || O <- Opts0, effects_code_generation(O)],
	    Chunks = [{<<"Dbgi">>, DebugInfo} | ExtraChunks],
	    CompileInfo = compile_info(File, CompilerOpts, Opts1),
	    {ok,Code} = beam_asm:module(Code0, Chunks, CompileInfo, CompilerOpts),
	    {ok,Code,St#compile{abstract_code=[]}};
	{error,Es} ->
	    {error,St#compile{errors=St#compile.errors ++ [{File,Es}]}}
    end.

beam_strip_types(Beam0, #compile{}=St) ->
    {ok,_Module,Chunks0} = beam_lib:all_chunks(Beam0),
    Chunks = [{Tag,Contents} || {Tag,Contents} <:- Chunks0,
                                Tag =/= "Type"],
    {ok,Beam} = beam_lib:build_module(Chunks),
    {ok,Beam,St}.

compile_info(File, CompilerOpts, Opts) ->
    IsSlim = member(slim, CompilerOpts),
    IsDeterministic = member(deterministic, CompilerOpts),
    Info0 = proplists:get_value(compile_info, Opts, []),
    Info1 =
	case File of
	    [_|_] = Source when not IsSlim, not IsDeterministic ->
		[{source,Source} | Info0];
	    _ ->
		Info0
	end,
    Info2 =
	case IsDeterministic of
	    false -> [{options,proplists:delete(compile_info, Opts)} | Info1];
	    true -> Info1
	end,
    Info2.

%% effects_code_generation(Option) -> true|false.
%%  Determine whether the option could have any effect on the
%%  generated code in the BEAM file (as opposed to how
%%  errors will be reported).

effects_code_generation(Option) ->
    case Option of
	beam -> false;
	report_warnings -> false;
	report_errors -> false;
	return_errors-> false;
	return_warnings-> false;
	warnings_as_errors -> false;
	binary -> false;
	verbose -> false;
	{cwd,_} -> false;
	{outdir, _} -> false;
	_ -> true
    end.

%% report_errors(State) -> ok
%% report_warnings(State) -> ok

report_errors(_State) -> ok.

report_warnings(_State) -> ok.

%%%
%%% Filter warnings.
%%%

filter_warnings(Ws, Opts) ->
    Ignore = ignore_tags(Opts, sets:new([{version,2}])),
    filter_warnings_1(Ws, Ignore).

filter_warnings_1([{Source,Ws0}|T], Ignore) ->
    Ws = [W || W <- Ws0, not ignore_warning(W, Ignore)],
    [{Source,Ws}|filter_warnings_1(T, Ignore)];
filter_warnings_1([], _Ignore) -> [].

ignore_warning({_Location,Pass,{Category,_}}, Ignore) ->
    IgnoreMod = case Pass of
                    v3_core -> true;
                    sys_core_fold -> true;
                    beam_core_to_ssa -> true;
                    _ -> false
                end,
    IgnoreMod andalso sets:is_element(Category, Ignore);
ignore_warning(_, _) -> false.

ignore_tags([nowarn_opportunistic|_], _Ignore) ->
    sets:from_list([failed,ignored,nomatch], [{version,2}]);
ignore_tags([nowarn_failed|Opts], Ignore) ->
    ignore_tags(Opts, sets:add_element(failed, Ignore));
ignore_tags([nowarn_ignored|Opts], Ignore) ->
    ignore_tags(Opts, sets:add_element(ignored, Ignore));
ignore_tags([nowarn_nomatch|Opts], Ignore) ->
    ignore_tags(Opts, sets:add_element(nomatch, Ignore));
ignore_tags([_|Opts], Ignore) ->
    ignore_tags(Opts, Ignore);
ignore_tags([], Ignore) -> Ignore.

%%%
%%% Transform the BEAM code to make it more friendly for
%%% diffing: using function names instead of labels for
%%% local calls and number labels relative to each function.
%%%

diffable(Code0, St) ->
    {Mod,Exp,Attr,Anno,Fs0,NumLabels} = Code0,
    EntryLabels = #{Entry => {Name,Arity} ||
                      {function,Name,Arity,Entry,_} <:- Fs0},
    Fs = [diffable_fix_function(F, EntryLabels) || F <- Fs0],
    Code = {Mod,Exp,Attr,Anno,Fs,NumLabels},
    {ok,Code,St}.

diffable_fix_function({function,Name,Arity,Entry0,Is0}, LabelMap0) ->
    Entry = maps:get(Entry0, LabelMap0),
    {Is1,LabelMap} = diffable_label_map(Is0, 1, LabelMap0, []),
    Fb = fun(Old) -> error({no_fb,Old}) end,
    Is = beam_utils:replace_labels(Is1, [], LabelMap, Fb),
    {function,Name,Arity,Entry,Is}.

diffable_label_map([{label,Old}|Is], New, Map, Acc) ->
    case Map of
        #{Old:=NewLabel} ->
            diffable_label_map(Is, New, Map, [{label,NewLabel}|Acc]);
        #{} ->
            diffable_label_map(Is, New+1, Map#{Old=>New}, [{label,New}|Acc])
    end;
diffable_label_map([I|Is], New, Map, Acc) ->
    diffable_label_map(Is, New, Map, [I|Acc]);
diffable_label_map([], _New, Map, Acc) ->
    {Acc,Map}.

%% The independent tooling engine does not expose OTP's host-side utility modes.
validate_tooling_options(Input, Options) ->
    SourceOptions = case Input of
                        {forms, Fs} -> compile_options(Fs);
                        _ -> []
                    end,
    lists:foreach(fun(Option) ->
        Key = case Option of T when is_tuple(T) -> element(1,T); A -> A end,
        case lists:member(Key,[time,eprof,profile,fprof,call_time,call_memory,check_ssa,verbose,encrypt_debug_info,debug_info_key,
                               crypto_mode,compressed_literals,to_dis,makedep,
                               makedep_side_effect,from_asm,from_core,from_abstr]) of
            true -> erlang:error({unsupported_compiler_option,Option});
            false -> ok
        end
    end,Options++SourceOptions).
