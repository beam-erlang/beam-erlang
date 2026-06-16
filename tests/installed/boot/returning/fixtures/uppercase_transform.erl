%% SPDX-License-Identifier: Apache-2.0
-module(uppercase_transform).
-export([apply/1]).
apply(Bytes) when is_binary(Bytes) ->
    << <<(upper(Byte))>> || <<Byte>> <= Bytes >>.

upper(Byte) when Byte >= $a, Byte =< $z -> Byte - 32;
upper(Byte) -> Byte.
