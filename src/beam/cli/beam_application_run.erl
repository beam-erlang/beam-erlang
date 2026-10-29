%% SPDX-License-Identifier: Apache-2.0
%% Ordinary application entry using explicitly selected returning services.
-module(beam_application_run).
-export([main/1]).
main({Entry, Function, Arguments}) ->
    {ok, Services} = beam_boot:services(),
    case lists:member(output, Services) of
        true ->
            Leader = beam_run_io:start(standard_output),
            Error = beam_run_io:start(standard_error),
            true = register(user, Leader),
            true = register(standard_error, Error),
            true = group_leader(Leader, self()),
            watch(Leader), watch(Error);
        false -> ok
    end,
    Module = binary_to_existing_atom(Entry, utf8),
    Name = binary_to_existing_atom(Function, utf8),
    try
        case erlang:function_exported(Module, Name, 1) of
            true -> apply(Module, Name, [Arguments]);
            false -> erlang:error({missing_application_entry, Module, Name, 1})
        end
    catch Class:Reason:Stack ->
        try io:format(standard_error, "beam: application failed: ~p:~p~n~p~n", [Class,Reason,Stack]) catch _:_ -> ok end,
        erlang:halt(1)
    end.

watch(Pid) ->
    spawn(fun() ->
        Ref = monitor(process, Pid),
        receive {'DOWN', Ref, process, Pid, Reason} ->
            erts_internal:request_failure(output, Reason)
        end
    end).
