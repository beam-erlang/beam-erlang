%% SPDX-License-Identifier: Apache-2.0
-module(beam_boot).
-export([services/0, finish/1]).

-spec services() -> {ok, [atom()]} | {error, #{kind := unavailable, service := boot}}.
services() ->
    case persistent_term:get(beam_boot_services, undefined) of
        Services when is_list(Services) -> {ok, Services};
        _ -> {error, #{kind => unavailable, service => boot}}
    end.

-spec finish(0..2147483647) -> no_return() | {error, #{kind := unavailable, service := boot}}.
finish(Status) when is_integer(Status), Status >= 0, Status =< 2147483647 ->
    case persistent_term:get(beam_boot_services, undefined) of
        Services when is_list(Services) ->
            _ = beam_host:request({finish, Status}),
            receive after infinity -> ok end;
        _ ->
            {error, #{kind => unavailable, service => boot}}
    end;
finish(Status) ->
    erlang:error(badarg, [Status]).
