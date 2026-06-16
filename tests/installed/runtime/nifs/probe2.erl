-module(probe2).
-export([hold/0]).
hold() -> erlang:nif_error(not_loaded).
