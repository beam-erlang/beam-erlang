-module(hello_world).
-export([main/1]).

main(_Args) ->
    io:format("Hello, World!~n"),
    beam:halt(0).
