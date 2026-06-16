%% SPDX-License-Identifier: Apache-2.0
%% Explicit host operations for the compiler's isolated VM lifetime.
-module(beam_compiler_host).
-export([request/2, context/1]).
request(Operation, Arguments) ->
    case beam_host:request({compiler, Operation, Arguments}) of
        {ok, Reply} -> Reply;
        {error, Reason} -> erlang:error({compiler_host, Operation, Reason})
    end.
context(Key) -> maps:get(Key, persistent_term:get(beam_compiler_context)).
