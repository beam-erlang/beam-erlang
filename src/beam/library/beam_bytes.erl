%% SPDX-License-Identifier: Apache-2.0
-module(beam_bytes).
-export([size/1, slice/3]).

-spec size(binary()) -> non_neg_integer().
size(Bytes) when is_binary(Bytes) ->
    byte_size(Bytes);
size(Bytes) ->
    erlang:error(badarg, [Bytes]).

-spec slice(binary(), non_neg_integer(), non_neg_integer()) -> binary().
slice(Bytes, Offset, Count)
  when is_binary(Bytes), is_integer(Offset), Offset >= 0,
       is_integer(Count), Count >= 0, Offset + Count =< byte_size(Bytes) ->
    <<_:Offset/binary, Slice:Count/binary, _/binary>> = Bytes,
    Slice;
slice(Bytes, Offset, Count) ->
    erlang:error(badarg, [Bytes, Offset, Count]).
