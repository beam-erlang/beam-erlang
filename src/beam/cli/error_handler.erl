%% SPDX-License-Identifier: Apache-2.0
-module(error_handler).
-export([undefined_function/3,undefined_lambda/3]).
undefined_function(shell_default,Function,Arguments) ->
    case erlang:function_exported(beam_interactive, undefined_function, 3) of
        true -> beam_interactive:undefined_function(shell_default, Function, Arguments);
        false -> missing(shell_default, Function, Arguments)
    end;
undefined_function(Module,Function,Arguments) ->
    %% Runtime support is acquired from its complete inventory before execution.
    %% Programs explicitly fetch and publish further code through beam_code.
    missing(Module,Function,Arguments).
undefined_lambda(_Module,_Fun,_Arguments) ->
    erlang:error(undef).
missing(Module,Function,Arguments) ->
    erlang:raise(error,undef,[{Module,Function,Arguments,[]}]).
