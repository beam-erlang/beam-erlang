%% SPDX-License-Identifier: Apache-2.0
-module(bare_async).
-export([answer/0, waiting/0, requesting/0, idle/0, crash/0, halt/0]).
answer() -> 42.
waiting() -> receive forever -> ok end.
requesting() -> {ok, ok} = beam_host:request(requesting), 42.
idle() ->
    erlang:spawn(fun() -> receive after 30 -> beam_host:request(idle) end end),
    42.
crash() -> beam:error(async_failure).
halt() -> beam:halt(7, []).
