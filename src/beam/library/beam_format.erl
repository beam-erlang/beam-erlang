%% SPDX-License-Identifier: Apache-2.0
-module(beam_format).
-export([integer/1, term/2]).

-define(MARK, <<"...">>).

-spec integer(integer()) -> binary().
integer(Value) when is_integer(Value) ->
    erlang:integer_to_binary(Value);
integer(Value) ->
    erlang:error(badarg, [Value]).

-spec term(term(), #{depth := non_neg_integer(), bytes := non_neg_integer()}) -> binary().
term(Value, #{depth := Depth, bytes := Bytes} = Limits)
  when is_integer(Depth), Depth >= 0, is_integer(Bytes), Bytes >= 0, map_size(Limits) =:= 2 ->
    {Text, _} = emit(Value, Depth, Bytes),
    Text;
term(Value, Limits) ->
    erlang:error(badarg, [Value, Limits]).

emit(_Value, _Depth, Budget) when Budget =< 0 ->
    {<<>>, 0};
emit(_Value, 0, Budget) ->
    marker(Budget);
emit(Value, _Depth, Budget) when is_integer(Value) ->
    emit_integer(Value, Budget);
emit(Value, _Depth, Budget) when is_atom(Value) ->
    emit_atom(Value, Budget);
emit(Value, _Depth, Budget) when is_float(Value) ->
    emit_float(Value, Budget);
emit(Value, _Depth, Budget) when is_binary(Value) ->
    emit_text(<<"<<", (erlang:integer_to_binary(byte_size(Value)))/binary, " bytes>>">>, Budget);
emit(Value, _Depth, Budget) when is_pid(Value) ->
    emit_text(<<"<pid>">>, Budget);
emit(Value, _Depth, Budget) when is_reference(Value) ->
    emit_text(<<"<ref>">>, Budget);
emit(Value, _Depth, Budget) when is_function(Value) ->
    emit_text(<<"<fun>">>, Budget);
emit(Value, Depth, Budget) when is_list(Value) ->
    emit_list(Value, Depth, Budget);
emit(Value, Depth, Budget) when is_tuple(Value) ->
    emit_tuple(Value, Depth, Budget);
emit(Value, Depth, Budget) when is_map(Value) ->
    emit_map(Value, Depth, Budget);
emit(_Value, _Depth, Budget) ->
    emit_text(<<"<term>">>, Budget).

%% A scalar that does not fit reserves the marker first. Only the leading
%% bytes that fit beside that marker are copied. binary:part of a finished
%% rendering is not used.
emit_text(Text, Budget) ->
    Size = byte_size(Text),
    case Size =< Budget of
        true -> {Text, Budget - Size};
        false ->
            {Mark, _} = marker(Budget),
            Room = Budget - byte_size(Mark),
            {<<(copy_prefix(Text, Room))/binary, Mark/binary>>, 0}
    end.

copy_prefix(_Text, Room) when Room =< 0 ->
    <<>>;
copy_prefix(<<>>, _Room) ->
    <<>>;
copy_prefix(<<Byte, Rest/binary>>, Room) ->
    <<Byte, (copy_prefix(Rest, Room - 1))/binary>>.

emit_atom(Atom, Budget) ->
    %% Every atom fits in 255 bytes. Below that, do not turn the full atom
    %% text into the result and then shorten it.
    case Budget >= 255 of
        true ->
            Text = erlang:atom_to_binary(Atom),
            {Text, Budget - byte_size(Text)};
        false ->
            limited_atom(Atom, Budget)
    end.

limited_atom(Atom, Budget) ->
    %% Walk at most Budget bytes and stop. A longer atom never becomes a
    %% finished rendering that is then shortened to the budget.
    case read_atom(erlang:atom_to_binary(Atom), Budget, <<>>) of
        {ok, Text} ->
            {Text, Budget - byte_size(Text)};
        {overflow, Bounded} ->
            {Mark, _} = marker(Budget),
            Room = Budget - byte_size(Mark),
            case Room =< 0 of
                true ->
                    {Mark, 0};
                false ->
                    {<<(read_prefix(Bounded, Room))/binary, Mark/binary>>, 0}
            end
    end.

%% {ok, Text} when the whole atom fits. {overflow, Text} is only the leading
%% Budget bytes; the rest of the atom is not copied.
read_atom(<<>>, _Left, Acc) ->
    {ok, Acc};
read_atom(<<_, _/binary>>, 0, Acc) ->
    {overflow, Acc};
read_atom(<<Byte, Rest/binary>>, Left, Acc) ->
    read_atom(Rest, Left - 1, <<Acc/binary, Byte>>).

read_prefix(_Text, Room) when Room =< 0 ->
    <<>>;
read_prefix(<<>>, _Room) ->
    <<>>;
read_prefix(<<Byte, Rest/binary>>, Room) ->
    <<Byte, (read_prefix(Rest, Room - 1))/binary>>.

emit_float(Value, Budget) ->
    %% Short form only. Fixed-point decimals raise badarg for magnitudes such
    %% as 1.0e300 because the digit buffer cannot hold them. A short text that
    %% does not fit is replaced by the marker and is not sliced.
    try erlang:float_to_binary(Value, [short]) of
        Text when byte_size(Text) =< Budget ->
            {Text, Budget - byte_size(Text)};
        _Text ->
            marker(Budget)
    catch
        error:badarg ->
            marker(Budget)
    end.

emit_integer(Value, Budget) ->
    Sign = Value < 0,
    Absolute = case Sign of
                   true -> -Value;
                   false -> Value
               end,
    Size = estimated_digits(Absolute) + sign_width(Sign),
    %% integer_to_binary runs only when the decimal text fits. A value that
    %% does not fit never builds that text.
    case Size =< Budget of
        true ->
            Text = erlang:integer_to_binary(Value),
            case byte_size(Text) =< Budget of
                true -> {Text, Budget - byte_size(Text)};
                false -> limited_integer(Sign, Absolute, Budget)
            end;
        false ->
            limited_integer(Sign, Absolute, Budget)
    end.

limited_integer(Sign, Absolute, Budget) ->
    {Mark, _} = marker(Budget),
    Room = Budget - byte_size(Mark) - sign_width(Sign),
    case Room > 0 of
        false ->
            {Mark, 0};
        true ->
            Leading = erlang:integer_to_binary(leading_digits(Absolute, Room)),
            Prefix = case Sign of
                         true -> <<"-", Leading/binary>>;
                         false -> Leading
                     end,
            {<<Prefix/binary, Mark/binary>>, 0}
    end.

estimated_digits(Number) ->
    trunc(integer_log10(Number)) + 1.

%% The first Count decimal digits. Only the top 53 bits are scaled, using a
%% short logarithm series, so the full decimal text is never built.
leading_digits(Number, Count) ->
    Log = integer_log10(Number),
    Fraction = Log - float(trunc(Log)),
    trunc(pow10_float(Fraction + float(Count - 1)) + 1.0e-8).

integer_log10(Number) ->
    Bits = bit_length(Number),
    Shift = case Bits > 53 of
                true -> Bits - 53;
                false -> 0
            end,
    Top = Number bsr Shift,
    TopDigits = positive_digits(Top),
    Mantissa = float(Top) / float(small_pow10(TopDigits - 1)),
    log10(Mantissa) + float(TopDigits - 1) + float(Shift) * log10(2.0).

positive_digits(Number) when Number < 10 ->
    1;
positive_digits(Number) ->
    1 + positive_digits(Number div 10).

small_pow10(0) ->
    1;
small_pow10(Exponent) ->
    10 * small_pow10(Exponent - 1).

log10(Value) ->
    ln(Value) / 2.302585092994046.

ln(Value) ->
    Z = (Value - 1.0) / (Value + 1.0),
    2.0 * ln_series(Z, Z * Z, 0.0, 1.0, 40).

ln_series(_Term, _Square, Sum, _Step, 0) ->
    Sum;
ln_series(Term, Square, Sum, Step, Left) ->
    ln_series(Term * Square, Square, Sum + Term / Step, Step + 2.0, Left - 1).

pow10_float(Exponent) ->
    Natural = Exponent * 2.302585092994046,
    exp_series(1.0, 0.0, 1.0, Natural, 40).

exp_series(_Term, Sum, _Step, _Natural, 0) ->
    Sum;
exp_series(Term, Sum, Step, Natural, Left) ->
    exp_series(Term * Natural / Step, Sum + Term, Step + 1.0, Natural, Left - 1).

sign_width(true) -> 1;
sign_width(false) -> 0.

bit_length(Number) ->
    upper_bound(Number, 1).

upper_bound(Number, Bound) when Number >= (1 bsl Bound) ->
    upper_bound(Number, Bound bsl 1);
upper_bound(Number, Bound) ->
    search_bound(Number, Bound bsr 1, Bound).

search_bound(Number, Low, High) when High =:= Low + 1 ->
    case Number >= (1 bsl Low) of
        true -> High;
        false -> Low
    end;
search_bound(Number, Low, High) ->
    Middle = (Low + High) bsr 1,
    case Number >= (1 bsl Middle) of
        true -> search_bound(Number, Middle, High);
        false -> search_bound(Number, Low, Middle)
    end.

emit_list(Items, Depth, Budget) ->
    case take(<<"[">>, Budget) of
        {<<"[">> = Open, Left} ->
            prepend(Open, close_seq(emit_items(Items, Depth - 1, Left, true), <<"]">>));
        Other ->
            Other
    end.

emit_tuple(Tuple, Depth, Budget) ->
    case take(<<"{">>, Budget) of
        {<<"{">> = Open, Left} ->
            prepend(Open, close_seq(emit_elements(Tuple, 1, tuple_size(Tuple),
                                                  Depth - 1, Left, true),
                                    <<"}">>));
        Other ->
            Other
    end.

emit_map(Map, Depth, Budget) ->
    case take(<<"#{">>, Budget) of
        {<<"#{">> = Open, Left} ->
            prepend(Open, close_seq(emit_map_iter(erts_internal:map_next(0, Map, iterator),
                                                  Depth - 1, Left, true), <<"}">>));
        Other ->
            Other
    end.

prepend(Open, {Inner, Budget}) ->
    {<<Open/binary, Inner/binary>>, Budget}.

emit_map_iter(none, _Depth, Budget, _First) ->
    {<<>>, Budget};
emit_map_iter(Iterator, Depth, Budget, First) ->
    case map_step(Iterator) of
        none ->
            {<<>>, Budget};
        {Key, Value, Next} ->
            {Prefix, Left0} = separator(First, Budget),
            {KeyText, Left1} = emit(Key, Depth, Left0),
            case Left1 of
                0 ->
                    {<<Prefix/binary, KeyText/binary>>, 0};
                _ ->
                    {Arrow, Left2} = take(<<" => ">>, Left1),
                    case Left2 of
                        0 ->
                            {<<Prefix/binary, KeyText/binary, Arrow/binary>>, 0};
                        _ ->
                            {ValueText, Left3} = emit(Value, Depth, Left2),
                            case Left3 of
                                0 ->
                                    {<<Prefix/binary, KeyText/binary, Arrow/binary,
                                      ValueText/binary>>, 0};
                                _ ->
                                    {More, End} = emit_map_iter(Next, Depth, Left3, false),
                                    {<<Prefix/binary, KeyText/binary, Arrow/binary,
                                      ValueText/binary, More/binary>>, End}
                            end
                    end
            end
    end.

map_step(none) ->
    none;
map_step({Key, Value, Next}) ->
    {Key, Value, continue_map(Next)};
map_step(Suspended) ->
    map_step(continue_map(Suspended)).

continue_map(none) ->
    none;
continue_map({_, _, _} = Next) ->
    Next;
continue_map([Path | Map]) when is_map(Map) ->
    erts_internal:map_next(Path, Map, iterator).

emit_elements(_Tuple, Index, Size, _Depth, Budget, _First) when Index > Size ->
    {<<>>, Budget};
emit_elements(Tuple, Index, Size, Depth, Budget, First) ->
    {Prefix, Left} = separator(First, Budget),
    {Text, After} = emit(element(Index, Tuple), Depth, Left),
    case After of
        0 ->
            {<<Prefix/binary, Text/binary>>, 0};
        _ ->
            {More, End} = emit_elements(Tuple, Index + 1, Size, Depth, After, false),
            {<<Prefix/binary, Text/binary, More/binary>>, End}
    end.

emit_items([], _Depth, Budget, _First) ->
    {<<>>, Budget};
emit_items([Item | Rest], Depth, Budget, First) ->
    {Prefix, Left} = separator(First, Budget),
    {Text, After} = emit(Item, Depth, Left),
    case After of
        0 ->
            {<<Prefix/binary, Text/binary>>, 0};
        _ ->
            {More, End} = emit_items(Rest, Depth, After, false),
            {<<Prefix/binary, Text/binary, More/binary>>, End}
    end.

separator(true, Budget) ->
    {<<>>, Budget};
separator(false, Budget) ->
    take(<<", ">>, Budget).

close_seq({Inner, Budget}, Closer) ->
    {Text, Left} = take(Closer, Budget),
    {<<Inner/binary, Text/binary>>, Left}.

take(Text, Budget) when is_binary(Text) ->
    Size = byte_size(Text),
    if
        Size =< Budget -> {Text, Budget - Size};
        true -> marker(Budget)
    end.

marker(Budget) when Budget >= 3 ->
    {?MARK, Budget - 3};
marker(0) ->
    {<<>>, 0};
marker(1) ->
    {<<".">>, 0};
marker(2) ->
    {<<"..">>, 0}.
