-module(probe2).
-export([hold/0]).
hold() -> beam:nif_error(not_loaded).
