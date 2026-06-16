-module(probe).
-export([hold/0, fatal/1, main/1, retain/0, implicit/1, foreign_runtime/1, packet/6, packet_invalid/0, packets/0]).
-nifs([hold/0, fatal/1, packet/6, packet_invalid/0]).
hold() -> erlang:nif_error(unloaded).
fatal(_) -> erlang:nif_error(unloaded).
packet(_,_,_,_,_,_) -> erlang:nif_error(unloaded).
packet_invalid() -> erlang:nif_error(unloaded).
retain() -> persistent_term:put(native_probe, hold()), ok.
implicit(Path) -> erlang:load_nif(Path, undefined).
foreign_runtime(Path) ->
    Expected = "Failed to load NIF library " ++ binary_to_list(Path) ++
        ": 'native library depends on another BEAM runtime image'",
    {error, {load_failed, Expected}} =
        erlang:load_nif(Path, undefined),
    {ok, ok} = erts_internal:host_request(check_runtime_mapping),
    erlang:halt(0).
main(["halt"]) -> erlang:halt(7);
main(["exception"]) -> erlang:error(native_project_exception);
main(["input"]) -> io:put_chars(io:get_line("prompt> "));
main(Args) ->
    false = erlang:module_loaded(beam_pkg),
    false = erlang:module_loaded(application),
    packets(), retain(), io:format("NATIVE_PROJECT ~tp~n", [Args]).

packets() ->
    packet_checks().

packet_checks() ->
    ok = packet_invalid(),
    Frames = [{0, raw, <<"raw">>}, {1, 1, <<3,"abc">>},
              {2, 2, <<0,3,"abc">>}, {3, 4, <<0,0,0,3,"abc">>},
              {4, asn1, <<4,3,"abc">>}, {5, sunrm, <<128,0,0,3,"abc">>},
              {6, cdr, <<"GIOP",1,2,0,0,0,0,0,3,"abc">>},
              {7, fcgi, <<1,6,0,1,0,3,2,0,"abc",0,0>>},
              {8, line, <<"line\n">>}, {9, tpkt, <<3,0,0,7,"abc">>},
              {10, http, <<"GET / HTTP/1.1\r\n">>},
              {11, httph, <<"Host: example\r\n\r\n">>},
              {12, ssl_tls, <<23,3,3,0,3,"abc">>},
              {13, http_bin, <<"HTTP/1.1 200 Fine\r\n">>},
              {14, httph_bin, <<"X-Custom: value\r\n\r\n">>}],
    packet_frames(Frames),
    packet_http([<<"OPTIONS * HTTP/1.1\r\n">>, <<"UNKNOWN raw HTTP/1.0\r\n">>,
                 <<"GET /path HTTP/1.1\r\n">>, <<"GET urn:example HTTP/1.1\r\n">>,
                 <<"GET http://example:8080/path HTTP/1.1\r\n">>,
                 <<"GET https://example HTTP/1.1\r\n">>,
                 <<"GET https://[::1]:443/path HTTP/1.1\r\n">>,
                 <<"HTTP/1.0 204 \r\n">>, <<"bad request\r\n">>]),
    packet_compare(11, httph, <<"X-Unknown: one\r\n two\r\n\r\n">>,0,0,10),
    packet_compare(14, httph_bin, <<"cOnTenT-TyPe: text/plain\r\n\r\n">>,0,0,10),
    packet_compare(12, ssl_tls, <<128,6,1,3,1,0,0,0>>,0,0,10),
    packet_compare(3, 4, <<255,255,255,255>>,0,0,10),
    packet_compare(6, cdr, <<"BAD!",0:64>>,0,0,10),
    packet_compare(7, fcgi, <<2,0:56>>,0,0,10),
    packet_compare(9, tpkt, <<2,0,0,4>>,0,0,10),
    packet_compare(8, line, <<"abc;rest">>,0,0,$;),
    {{ok,{http_request,'GET',_,_},<<>>},1} = packet(10,<<"GET / HTTP/1.1\r\n">>,0,0,10,0),
    {{ok,{http_header,_, 'Host',_,_},<<"\r\n">>},1} = packet(10,<<"Host: example\r\n\r\n">>,0,0,10,1),
    {{ok,http_eoh,<<>>},0} = packet(10,<<"\r\n">>,0,0,10,1),
    ok.

packet_frames([]) -> ok;
packet_frames([{Format,Type,Bytes}|Rest]) ->
    packet_prefixes(Format,Type,Bytes,0),
    packet_compare(Format,Type,<<Bytes/binary,"tail">>,0,0,10),
    packet_compare(Format,Type,Bytes,2,0,10),
    packet_compare(Format,Type,Bytes,0,2,10),
    packet_frames(Rest).
packet_prefixes(_,_,Bytes,N) when N > byte_size(Bytes) -> ok;
packet_prefixes(Format,Type,Bytes,N) ->
    packet_compare(Format,Type,binary_part(Bytes,0,N),0,0,10),
    packet_prefixes(Format,Type,Bytes,N+1).
packet_http([]) -> ok;
packet_http([Bytes|Rest]) ->
    packet_compare(10,http,Bytes,0,0,10),
    packet_compare(13,http_bin,Bytes,0,0,10),
    packet_http(Rest).
packet_compare(Format,Type,Bytes,Maximum,Truncate,Delimiter) ->
    Options = [{packet_size,Maximum},{line_length,Truncate}],
    Selected = case Type of line -> [{line_delimiter,Delimiter}|Options]; _ -> Options end,
    Expected = erlang:decode_packet(Type,Bytes,Selected),
    case packet(Format,Bytes,Maximum,Truncate,Delimiter,0) of
        {Expected,_} -> ok;
        Actual -> erlang:error({packet_mismatch,Type,Bytes,Selected,Expected,Actual})
    end,
    ok.
