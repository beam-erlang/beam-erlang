%% SPDX-License-Identifier: Apache-2.0
-module(android_shared).
-export([value/0]).
value() -> erlang:nif_error(not_activated).
