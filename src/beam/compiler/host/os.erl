%% SPDX-License-Identifier: Apache-2.0
-module(os).
%% Explicit native selections coexist with the compiler support functions.
-export([putenv/2, unsetenv/1, timestamp/0, system_time/0, system_time/1, perf_counter/0, set_signal/2, env/0]).

-export([getenv/1,getenv/2,getpid/0,type/0]).
getenv(Name) -> beam_compiler_host:request(getenv, [Name]).
getenv(Name, Default) -> case getenv(Name) of false -> Default; Value -> Value end.
getpid() -> beam_compiler_host:request(getpid, []).
type() -> erlang:system_info(os_type).

%% Native implementation declarations.
putenv(_,_) -> erlang:nif_error(undefined).
unsetenv(_) -> erlang:nif_error(undefined).
timestamp() -> erlang:nif_error(undefined).
system_time() -> erlang:nif_error(undefined).
system_time(_) -> erlang:nif_error(undefined).
perf_counter() -> erlang:nif_error(undefined).
set_signal(_,_) -> erlang:nif_error(undefined).
env() -> erlang:nif_error(undefined).
