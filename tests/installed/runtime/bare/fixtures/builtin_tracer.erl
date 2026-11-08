%% SPDX-License-Identifier: Apache-2.0
-module(builtin_tracer).
-export([enabled/3, trace/5, resource/0, resources/0]).
enabled(_, _, _) -> erlang:nif_error(undef).
trace(_, _, _, _, _) -> erlang:nif_error(undef).
resource() -> erlang:nif_error(undef).
resources() -> erlang:nif_error(undef).
