%% SPDX-License-Identifier: Apache-2.0
-module(bare_bridge).
-export([answer/0, no_handler/0, requests/0, pending/0, pending_result/0,
         pending_dead/0, dead_requester/0, fairness/0, fairness_result/0,
         halt_in_callback/0, background_halt/0]).

answer() -> 42.

no_handler() ->
    {error, no_handler} = beam_host:request(no_handler),
    ok.

requests() ->
    {ok, <<"copied">>} = beam_host:request(copy),
    Binary = <<(erlang:unique_integer([positive])):64, 0:524288>>,
    Term = {echo, #{binary => Binary, shared => {Binary, Binary},
                    nested => [one, {2, [-3, 16#10000000000000000]},
                               {self(), make_ref()}]}},
    {ok, Term} = beam_host:request(Term),
    true = erlang:garbage_collect(),
    {error, no_reply} = beam_host:request(no_reply),
    {error, invalid_reply} = beam_host:request(invalid_reply),
    {error, invalid_reply} = beam_host:request(invalid_after_binary),
    {error, invalid_reply} = beam_host:request(trailing_reply),
    {error, invalid_reply} = beam_host:request(compressed_reply),
    {error, {callback_failed, 4}} = beam_host:request(failed),
    {ok, ok} = beam_host:request(reentrant),
    ok.

pending() ->
    true = register(bare_pending, spawn(fun() ->
        receive after 100 -> ok end,
        Result = beam_host:request(pending),
        receive {From, Ref} -> From ! {Ref, Result} end
    end)),
    ok.

pending_result() ->
    Ref = make_ref(),
    bare_pending ! {self(), Ref},
    receive {Ref, {ok, ok}} -> ok end.

pending_dead() ->
    Pid = spawn(fun() ->
        receive after 25 -> beam_host:request(pending_dead) end
    end),
    spawn(fun() -> receive after 100 -> exit(Pid, kill) end end),
    ok.

dead_requester() ->
    Pid = spawn(fun() -> beam_host:request(dead_requester) end),
    %% A separate process can terminate a requester while C handles its request.
    Ref = monitor(process, Pid),
    spawn(fun() -> receive after 100 -> exit(Pid, kill) end end),
    receive {'DOWN', Ref, process, Pid, killed} -> ok end.

fairness() ->
    First = spawn(fun() -> beam_host:request(fairness_first) end),
    Ref = monitor(process, First),
    spawn(fun() ->
        receive after 100 -> ok end,
        Second = spawn(fun() ->
            {ok, ok} = beam_host:request(fairness_second),
            receive {From, Tag} -> From ! {Tag, ok} end
        end),
        true = register(bare_fairness, Second),
        %% The second child has no receive before its host request, so waiting
        %% establishes that it reached the bridge while C handles the first.
        await_waiting(Second),
        exit(First, kill)
    end),
    receive {'DOWN', Ref, process, First, killed} -> ok end.

await_waiting(Pid) ->
    case process_info(Pid, status) of
        {status, waiting} -> ok;
        {status, _} -> receive after 1 -> await_waiting(Pid) end
    end.

fairness_result() ->
    Ref = make_ref(),
    bare_fairness ! {self(), Ref},
    receive {Ref, ok} -> ok end.

halt_in_callback() ->
    spawn(fun() -> receive after 100 -> erlang:halt(23) end end),
    beam_host:request(halt_in_callback).

background_halt() ->
    spawn(fun() -> receive after 250 -> erlang:halt(19) end end),
    ok.
