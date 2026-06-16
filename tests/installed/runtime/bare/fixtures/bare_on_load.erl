%% SPDX-License-Identifier: Apache-2.0
-module(bare_on_load).
-on_load(initialize/0).
-export([answer/0]).
initialize() -> erlang:halt(99).
answer() -> 0.
