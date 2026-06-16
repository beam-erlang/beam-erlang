%% SPDX-License-Identifier: Apache-2.0
-module(onload_rejected).
-export([value/0]).
-on_load(load/0).
load() -> error.
value() -> <<"loaded">>.
