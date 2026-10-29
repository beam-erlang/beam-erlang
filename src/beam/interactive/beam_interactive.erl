%% SPDX-License-Identifier: Apache-2.0
%% Explicit shell/evaluator composition above the selected BEAM services.
-module(beam_interactive).
-export([main/1, project/1, finish/1, get_env/2, get_env/3, set_env/3,
         classify_function/1, undefined_function/3,
         unsupported/2, unsupported_helper/2]).

main({Root, Plain0, Command, Geometry, Terminal})
  when is_binary(Root), is_list(Plain0), is_boolean(Terminal) ->
    start(Root, Plain0, Command, Geometry, Terminal, boot).

project({Root, Plain, Command, Geometry, Terminal}) ->
    start(Root, Plain, Command, Geometry, Terminal, host).

start(Root, Plain0, Command, Geometry, Terminal, Transport) ->
    persistent_term:put(beam_interactive_transport, Transport),
    Plain = [unicode:characters_to_list(list_to_binary(Arg)) || Arg <- Plain0],
    persistent_term:put(beam_interactive_context,
                        #{root=>Root, arguments=>Plain, geometry=>Geometry}),
    ets:new(beam_shell_settings, [named_table, public, set]),
    Profile = #{mode=>interactive, terminal=>Terminal, geometry=>Geometry,
                recover_lines=>Command =:= shell, transport=>Transport},
    Leader = beam_run_io:start(standard_output, Profile),
    Error = beam_run_io:start(standard_error, Profile),
    true = register(user, Leader),
    true = register(standard_error, Error),
    true = erlang:group_leader(Leader, self()),
    watch(Leader),
    watch(Error),
    case Command of
        shell -> run_shell();
        {eval, Expression} -> evaluate(Expression)
    end.

watch(Pid) ->
    spawn(fun() ->
                  Ref = monitor(process, Pid),
                  receive
                      {'DOWN', Ref, process, Pid, Reason} ->
                          case persistent_term:get(beam_interactive_transport) of
                              host -> finish(1);
                              boot -> erts_internal:request_failure(output, Reason)
                          end
                  end
          end).

run_shell() ->
    Pid = shell:start(true, false),
    Ref = monitor(process, Pid),
    receive
        {'DOWN', Ref, process, Pid, normal} -> finish(0);
        {'DOWN', Ref, process, Pid, Reason} -> error({shell_terminated, Reason})
    end.

evaluate(Expression) ->
    try
        Text = unicode:characters_to_list(Expression),
        case erl_scan:string(Text) of
            {ok, Tokens, _} ->
                case erl_parse:parse_exprs(Tokens) of
                    {ok, Exprs} ->
                        {value, Value, _} = erl_eval:exprs(Exprs, erl_eval:new_bindings()),
                        Value;
                    {error, Error} -> error({parse, Error})
                end;
            {error, Error, _} -> error({scan, Error})
        end
    catch
        Class:Reason:Stack ->
            try io:format(standard_error, "beam: evaluation failed: ~ts~n",
                          [erl_error:format_exception(Class, Reason, Stack)])
            catch _:_ -> ok end,
            finish(1)
    end.

%% Session settings belong to the entry process and disappear with its VM.
get_env(stdlib, Name) ->
    case ets:lookup(beam_shell_settings, Name) of
        [{Name, Value}] -> {ok, Value};
        [] -> undefined
    end.
get_env(stdlib, Name, Default) ->
    case get_env(stdlib, Name) of
        {ok, Value} -> Value;
        undefined -> Default
    end.
set_env(stdlib, Name, Value) ->
    true = ets:insert(beam_shell_settings, {Name, Value}),
    ok.

%% Used by the retained shell's rewriting of local function definitions.
classify_function(Name) when is_list(Name) ->
    case erl_scan:string(Name) of
        {ok, [{atom, _, Atom}], _} -> classify_function(Atom);
        _ -> []
    end;
classify_function(Name) when is_atom(Name) ->
    case lists:keymember(Name, 1, shell_default:module_info(exports)) of
        true -> "shell_default";
        false ->
            case lists:keymember(Name, 1, erlang:module_info(exports)) of
                true -> "erlang";
                false ->
                    case shell:local_func(Name) of
                        true -> "shell";
                        false -> "user_defined"
                    end
            end
    end.

%% This is the sole dynamic shell dispatch exception to fail-closed lookup.
%% It consults an existing shell definition and never acquires code.
undefined_function(shell_default, Function, Arguments) ->
    case unsupported_helper(Function, length(Arguments)) of
        true -> unsupported(Function, length(Arguments));
        false ->
            Fun = try shell:get_function(Function, length(Arguments))
                  catch error:_ -> undefined end,
            case is_function(Fun, length(Arguments)) of
                true -> apply(Fun, Arguments);
                false -> erlang:raise(error, undef,
                                     [{shell_default, Function, Arguments, []}])
            end
    end.

unsupported(Function, Arity) ->
    erlang:error({unsupported_shell_helper, Function, Arity}).

unsupported_helper(Function, Arity) ->
    lists:member({Function, Arity},
                 [{c,1},{c,2},{c,3},{lc,1},{nc,1},{l,1},{nl,1},
                  {rr,1},{rr,2},{rr,3},{save_module,1},
                  {h,1},{h,2},{h,3},{ht,1},{ht,2},{ht,3},
                  {hcb,1},{hcb,2},{hcb,3},
                  {cd,1},{pwd,0},{ls,0},{ls,1},{erlangrc,1}]).

finish(Status) ->
    case persistent_term:get(beam_interactive_transport, boot) of
        host -> erlang:halt(Status);
        boot -> beam_boot:finish(Status)
    end.
