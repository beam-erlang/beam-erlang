%% SPDX-License-Identifier: Apache-2.0
-module(compiler_lifetime_fixture).
-compile({parse_transform,compiler_lifetime_transform}).
-export([value/0]).

value() ->
    absent = beam_persistent_term:get({compiler_lifetime_transform,state}, absent),
    undefined = whereis(compiler_lifetime_worker),
    undefined = beam_ets:whereis(compiler_lifetime_table),
    false = beam:module_loaded(compiler_lifetime_transform),
    42.

