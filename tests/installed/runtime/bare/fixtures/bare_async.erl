%% SPDX-License-Identifier: Apache-2.0
-module(bare_async).
-export([answer/0, waiting/0, idle/0, crash/0, halt/0]).
answer() -> 42.
waiting() -> receive forever -> ok end.
idle() ->
    spawn(fun() -> receive after 30 -> beam_host:request(idle) end end),
    42.
crash() -> erlang:error(async_failure).
halt() -> erlang:halt(7).
