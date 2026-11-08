%% SPDX-License-Identifier: Apache-2.0
-module(beam_vm_bytes).
-export([trace_calls/0]).

%% The selected OTP host explicitly supplies the default tracer provider.
trace_calls() ->
    true = erlang:is_builtin(binary, match, 2),
    false = erlang:is_builtin(beam_bytes, find, 2),
    false = erlang:is_builtin(erts_internal, bytes_match, 3),
    Parent = self(),
    {Tracer, Monitor} = spawn_monitor(fun() -> collect(Parent, []) end),
    1 = erlang:trace_pattern({binary, match, 2}, true, []),
    1 = erlang:trace(self(), true, [call, {tracer, Tracer}]),
    {0, 1} = binary:match(<<"a">>, <<"a">>),
    1 = erlang:trace(self(), false, [call]),
    1 = erlang:trace_pattern({binary, match, 2}, false, []),
    receive
        {byte_calls, Tracer,
         [{binary, match, [<<"a">>, <<"a">>]}]} -> ok
    after 5000 -> error(byte_trace_timeout)
    end,
    Tracer ! stop,
    receive {'DOWN', Monitor, process, Tracer, normal} -> ok
    after 5000 -> error(byte_trace_owner_timeout)
    end.

collect(Parent, [Call]) ->
    Parent ! {byte_calls, self(), [Call]},
    receive stop -> ok end;
collect(Parent, Calls) ->
    receive {trace, Parent, call, Call} -> collect(Parent, [Call | Calls]) end.
