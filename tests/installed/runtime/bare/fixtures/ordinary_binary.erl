%% SPDX-License-Identifier: Apache-2.0
-module(binary).
-export([split/2]).

split(<<"ab">>, <<"a">>) -> ordinary;
split(_, _) -> other.
