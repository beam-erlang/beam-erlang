%% SPDX-License-Identifier: Apache-2.0
-module(compiler_lifetime_fixture).
-compile({parse_transform,compiler_lifetime_transform}).
-export([value/0]).

value() ->
    absent = persistent_term:get({compiler_lifetime_transform,state}, absent),
    undefined = whereis(compiler_lifetime_worker),
    undefined = ets:whereis(compiler_lifetime_table),
    false = erlang:module_loaded(compiler_lifetime_transform),
    42.

