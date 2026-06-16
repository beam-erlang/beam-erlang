%% SPDX-License-Identifier: Apache-2.0
%% Fixed language profile for the matching-build interactive frontend.
-module(erl_features).
-export([enabled/0, configurable/0, keywords/0, keywords/1]).
enabled() -> [maybe_expr].
configurable() -> [maybe_expr, compr_assign].
keywords() -> ['maybe', 'else'].
keywords(maybe_expr) -> ['maybe', 'else'];
keywords(compr_assign) -> [].
