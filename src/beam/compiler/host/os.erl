%% SPDX-License-Identifier: Apache-2.0
-module(os).
-export([getenv/1,getenv/2,getpid/0,type/0]).
getenv(Name) -> beam_compiler_host:request(getenv, [Name]).
getenv(Name, Default) -> case getenv(Name) of false -> Default; Value -> Value end.
getpid() -> beam_compiler_host:request(getpid, []).
type() -> erlang:system_info(os_type).
