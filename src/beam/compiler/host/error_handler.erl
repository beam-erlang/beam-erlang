%% SPDX-License-Identifier: Apache-2.0
-module(error_handler).
-export([undefined_function/3,undefined_lambda/3]).
undefined_function(Module,Function,Arguments) ->
    %% Tooling is acquired from its complete inventory before execution.
    %% Transforms must explicitly load any additional client dependencies.
    missing(Module,Function,Arguments).
undefined_lambda(_Module,_Fun,_Arguments) ->
    erlang:error(undef).
missing(Module,Function,Arguments) ->
    erlang:raise(error,undef,[{Module,Function,Arguments,[]}]).
