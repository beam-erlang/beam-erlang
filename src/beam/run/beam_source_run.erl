%% SPDX-License-Identifier: Apache-2.0
%% Source execution policy above the selected BEAM returning services.
-module(beam_source_run).
-export([main/1]).

main({Root, Plain0, {run, File, Module, Bytes}, Geometry})
  when is_binary(Root), is_list(Plain0), is_binary(File), is_atom(Module),
       is_binary(Bytes) ->
    Plain = [unicode:characters_to_list(list_to_binary(Arg)) || Arg <- Plain0],
    persistent_term:put(beam_run_context,
                        #{root => Root, arguments => Plain, geometry => Geometry}),
    {ok, Services} = beam_boot:services(),
    case lists:member(output, Services) of
        true -> start_io();
        false -> ok
    end,
    case load_source(Module, Bytes, File) of
        ok ->
            try Module:main(Plain)
            catch
                Class:Reason:Stack ->
                    diagnostic("beam: ~ts failed: ~p:~p~n~p~n",
                               [File, Class, Reason, Stack]),
                    beam_boot:finish(1)
            end;
        {error, Reason} ->
            diagnostic("beam: unable to load ~ts: ~p~n", [File, Reason]),
            beam_boot:finish(1)
    end.

load_source(Module, Bytes, File) ->
    case erlang:module_loaded(Module) of
        true -> {error, #{kind => conflict, module => Module}};
        false -> beam_code:load(Module, Bytes, File)
    end.

start_io() ->
    Leader = beam_run_io:start(standard_output),
    Error = beam_run_io:start(standard_error),
    true = register(user, Leader),
    true = register(standard_error, Error),
    true = erlang:group_leader(Leader, self()),
    watch(Leader),
    watch(Error).

watch(Pid) ->
    spawn(fun() ->
                  Ref = monitor(process, Pid),
                  receive
                      {'DOWN', Ref, process, Pid, Reason} ->
                          erts_internal:request_failure(output, Reason)
                  end
          end).

diagnostic(Format, Arguments) ->
    %% A missing or failed output device must not replace the program's failure.
    try io:format(standard_error, Format, Arguments)
    catch _:_ -> ok end.
