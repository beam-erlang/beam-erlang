%% SPDX-License-Identifier: Apache-2.0
-module('bound.tables').
-export([t_new/2,t_insert/2,t_lookup/2,t_info/2,t_select/2,t_chunk/3,
         t_match/2,t_drop/1,t_compile/1,t_run/3,version/0]).
-ifdef(ON_LOAD).
-on_load(initialize/0).
initialize() -> ok.
-endif.
version() -> ?GENERATION.
t_new(_,_) -> erlang:nif_error(undef).
t_insert(_,_) -> erlang:nif_error(undef).
t_lookup(_,_) -> erlang:nif_error(undef).
t_info(_,_) -> erlang:nif_error(undef).
t_select(_,_) -> erlang:nif_error(undef).
t_chunk(_,_,_) -> erlang:nif_error(undef).
t_match(_,_) -> erlang:nif_error(undef).
t_drop(_) -> erlang:nif_error(undef).
t_compile(_) -> erlang:nif_error(undef).
t_run(_,_,_) -> erlang:nif_error(undef).
