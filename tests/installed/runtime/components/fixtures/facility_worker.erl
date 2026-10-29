%% SPDX-License-Identifier: Apache-2.0
-module(facility_worker).
-export([exercise/1, pid/0, fail/1, cleanup/0, fresh/0, complete/0]).

fresh() ->
    undefined = persistent_term:get(facility_calls, undefined),
    false = erlang:module_loaded(beam_boot),
    42.

exercise(Services) ->
    Count = persistent_term:get(facility_calls, 0),
    persistent_term:put(facility_calls, Count + 1),
    case Services band 1 of
        0 -> {error, #{kind := unavailable}} = beam_output:write(standard_output, <<"facility\n">>, infinity);
        _ -> ok = beam_output:write(standard_output, <<"facility\n">>, infinity)
    end,
    case Services band 2 of
        0 -> {error, #{kind := unavailable}} = beam_code:fetch(memory, facility_helper, infinity);
        _ ->
            {ok, Bytes} = beam_code:fetch(memory, facility_helper, infinity),
            case erlang:module_loaded(facility_helper) of
                true -> ok;
                false -> ok = beam_code:load(facility_helper, Bytes, memory)
            end,
            42 = facility_helper:value()
    end,
    case Services band 4 of
        0 -> false = erlang:module_loaded(zlib);
        _ when Count =:= 0 ->
            Stream = zlib:open(),
            ok = zlib:deflateInit(Stream),
            persistent_term:put(facility_stream, Stream);
        _ -> ok
    end,
    case Services band 32 of
        0 -> ok;
        _ -> program()
    end,
    42.

program() ->
    case whereis(facility_program_owner) of
        undefined ->
            Parent = self(),
            {Pid, ReadyRef} = spawn_monitor(fun() ->
                register(facility_program_owner, self()),
                Cat = open_port({spawn_executable, "/bin/cat"}, [binary, use_stdio, exit_status]),
                Sleep = open_port({spawn_executable, "/bin/sleep"}, [binary, {args, ["60"]}]),
                persistent_term:put(facility_programs, {Cat, Sleep}),
                Parent ! {self(), ready},
                program_loop(Cat)
            end),
            receive
                {Pid, ready} -> demonitor(ReadyRef, [flush]);
                {'DOWN', ReadyRef, process, Pid, Reason} -> error(Reason)
            after 5000 -> error(program_timeout)
            end;
        _ -> ok
    end,
    Ref = make_ref(),
    facility_program_owner ! {self(), Ref},
    receive {Ref, <<"ping">>} -> ok after 5000 -> error(program_timeout) end.

program_loop(Cat) ->
    receive
        {From, Ref} ->
            true = erlang:port_command(Cat, <<"ping">>),
            receive {Cat, {data, Bytes}} -> From ! {Ref, Bytes}
            after 5000 -> error(cat_timeout)
            end,
            program_loop(Cat)
    end.

pid() ->
    {_Cat, Sleep} = persistent_term:get(facility_programs),
    {os_pid, Pid} = erlang:port_info(Sleep, os_pid),
    Pid.

%% Failure injection targets the generic terminal mechanism, not synthetic owners.
fail(Origin) -> erts_internal:request_failure(Origin, injected_failure), await_failure().
await_failure() -> receive after 5000 -> error(failure_did_not_stop) end.

cleanup() -> {ok, ok} = beam_host:request(cleanup), 42.

complete() -> beam_host:request(complete), error(completion_returned).
