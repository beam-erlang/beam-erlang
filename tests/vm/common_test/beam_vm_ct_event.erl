%% SPDX-License-Identifier: Apache-2.0
%% Record Common Test's terminal events without changing suite callbacks.
-module(beam_vm_ct_event).
-behaviour(gen_event).
-include_lib("common_test/include/ct_event.hrl").
-export([init/1, handle_event/2, handle_call/2, handle_info/2,
         terminate/2, code_change/3]).

init([Path]) ->
    {ok, File} = file:open(Path, [write, binary]),
    ok = emit(File, #{event => <<"handler_start">>}),
    {ok, File}.

handle_event(#event{name = Name, data = {Suite, Case, Result}}, File)
  when Name =:= tc_done; Name =:= tc_user_skip; Name =:= tc_auto_skip ->
    Status = case {Name, Result} of
                 {tc_user_skip, _} -> <<"user_skipped">>;
                 {tc_auto_skip, _} -> <<"auto_skipped">>;
                 {tc_done, ok} -> <<"passed">>;
                 {tc_done, {skipped, _}} -> <<"user_skipped">>;
                 {tc_done, {auto_skipped, _}} -> <<"auto_skipped">>;
                 _ -> <<"failed">>
             end,
    ok = emit(File, #{event => atom_to_binary(Name),
                      suite => text(Suite), name => text(Case),
                      status => Status, reason => text(Result)}),
    {ok, File};
handle_event(_, File) -> {ok, File}.

handle_call(_, File) -> {ok, ok, File}.
handle_info(_, File) -> {ok, File}.
code_change(_, File, _) -> {ok, File}.
terminate(_, File) ->
    ok = emit(File, #{event => <<"handler_stop">>}),
    file:close(File).

text(Term) -> unicode:characters_to_binary(io_lib:format("~0tP", [Term, 30])).
emit(File, Record) -> file:write(File, [json:encode(Record), $\n]).
