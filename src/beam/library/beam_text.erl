%% SPDX-License-Identifier: Apache-2.0
-module(beam_text).
-export([from_utf8/1, concat/1]).

-spec from_utf8(binary()) -> {ok, binary()} | {error, #{kind := invalid_data}}.
from_utf8(Bytes) when is_binary(Bytes) ->
    case validate(Bytes) of
        true -> {ok, Bytes};
        false -> {error, #{kind => invalid_data}}
    end;
from_utf8(Bytes) ->
    erlang:error(badarg, [Bytes]).

-spec concat([binary()]) -> binary().
concat(Texts) when is_list(Texts) ->
    concat(Texts, <<>>);
concat(Texts) ->
    erlang:error(badarg, [Texts]).

concat([], Acc) ->
    Acc;
concat([Text | Rest], Acc) when is_binary(Text) ->
    case validate(Text) of
        true -> concat(Rest, <<Acc/binary, Text/binary>>);
        false -> erlang:error(badarg, [[Text | Rest]])
    end;
concat(Texts, _Acc) ->
    erlang:error(badarg, [Texts]).

validate(<<>>) ->
    true;
validate(<<C, Rest/binary>>) when C < 16#80 ->
    validate(Rest);
validate(<<C1, C2, Rest/binary>>)
  when C1 >= 16#C2, C1 =< 16#DF, C2 >= 16#80, C2 =< 16#BF ->
    validate(Rest);
validate(<<C1, C2, C3, Rest/binary>>)
  when C1 >= 16#E0, C1 =< 16#EF, C2 >= 16#80, C2 =< 16#BF,
       C3 >= 16#80, C3 =< 16#BF ->
    Code = ((C1 band 16#0F) bsl 12) bor ((C2 band 16#3F) bsl 6) bor (C3 band 16#3F),
    if
        C1 =:= 16#E0, C2 < 16#A0 -> false;
        Code >= 16#D800, Code =< 16#DFFF -> false;
        true -> validate(Rest)
    end;
validate(<<C1, C2, C3, C4, Rest/binary>>)
  when C1 >= 16#F0, C1 =< 16#F4, C2 >= 16#80, C2 =< 16#BF,
       C3 >= 16#80, C3 =< 16#BF, C4 >= 16#80, C4 =< 16#BF ->
    Code = ((C1 band 16#07) bsl 18) bor ((C2 band 16#3F) bsl 12) bor
            ((C3 band 16#3F) bsl 6) bor (C4 band 16#3F),
    if
        C1 =:= 16#F0, C2 < 16#90 -> false;
        C1 =:= 16#F4, C2 > 16#8F -> false;
        Code > 16#10FFFF -> false;
        true -> validate(Rest)
    end;
validate(_) ->
    false.
