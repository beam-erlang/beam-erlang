%% SPDX-License-Identifier: Apache-2.0
-module(interactive_prestart_rejected).
-on_load(initialize/0).
-export([initialize/0]).
initialize() -> error(unexpected_prestart_execution).
