%% SPDX-License-Identifier: Apache-2.0
-module(preload_on_load).
-on_load(initialize/0).
initialize() -> erlang:halt(99).
