%% SPDX-License-Identifier: Apache-2.0
-module(chosen).
-ifdef(PROVIDER).
-export([float/1,whereis/1,erase/1,put/2,dirty_cpu/2,dirty_io/2,self/0,
         fail/1,raise/1,stop/1,log/1,invoke_fun/2,invoke/3,relax/0,sleep/0]).
-beam_native_slots([{float,1},{whereis,1},{erase,1},{put,2},{dirty_cpu,2},
    {dirty_io,2},{self,0},{fail,1},{raise,1},{stop,1},{log,1},{invoke_fun,2},
    {invoke,3},{relax,0},{sleep,0}]).
float(_) -> erlang:nif_error(undef).
whereis(_) -> erlang:nif_error(undef).
erase(_) -> erlang:nif_error(undef).
put(_,_) -> erlang:nif_error(undef).
dirty_cpu(_,_) -> erlang:nif_error(undef).
dirty_io(_,_) -> erlang:nif_error(undef).
self() -> erlang:nif_error(undef).
fail(_) -> erlang:nif_error(undef).
raise(_) -> erlang:nif_error(undef).
stop(_) -> erlang:nif_error(undef).
log(_) -> erlang:nif_error(undef).
invoke_fun(_,_) -> erlang:nif_error(undef).
invoke(_,_,_) -> erlang:nif_error(undef).
relax() -> erlang:nif_error(undef).
sleep() -> erlang:nif_error(undef).
-else.
-export([float/1]).
float(_) -> ordinary.
-endif.
