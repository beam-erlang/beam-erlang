%% SPDX-License-Identifier: Apache-2.0
%% Project entry above the public VM lifecycle; no boot service state.
-module(beam_project_run).
-export([main/1]).
main({Entry, Arguments}) ->
    Profile = #{mode => run, terminal => false, geometry => {80, 24}, transport => host},
    Leader = beam_run_io:start(standard_output, Profile),
    Error = beam_run_io:start(standard_error, Profile),
    true = register(user, Leader),
    true = register(standard_error, Error),
    true = group_leader(Leader, self()),
    watch(Leader), watch(Error),
    try
        Module = binary_to_existing_atom(Entry, utf8),
        case erlang:function_exported(Module, main, 1) of
            true -> Module:main([unicode:characters_to_list(A) || A <- Arguments]);
            false -> erlang:error({missing_project_entry, Module, main, 1})
        end
    catch Class:Reason:Stack ->
        try io:format(standard_error, "beam: project failed: ~p:~p~n~p~n", [Class, Reason, Stack]) catch _:_ -> ok end,
        erlang:halt(1)
    end.
watch(Pid) ->
    spawn(fun() ->
        Ref = monitor(process, Pid),
        receive {'DOWN', Ref, process, Pid, _} -> erlang:halt(1) end
    end).
