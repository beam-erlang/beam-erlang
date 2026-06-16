%% SPDX-License-Identifier: Apache-2.0
-module(compiler_lifetime_transform).
-export([parse_transform/2]).

parse_transform(Forms, _) ->
    absent = persistent_term:get({compiler_lifetime_transform,state}, absent),
    undefined = whereis(compiler_lifetime_worker),
    undefined = ets:whereis(compiler_lifetime_table),
    persistent_term:put({compiler_lifetime_transform,state}, compiler_state),
    true = register(compiler_lifetime_worker,
                    spawn(fun() -> receive stop -> ok end end)),
    compiler_lifetime_table = ets:new(compiler_lifetime_table, [named_table,public]),
    %% Renaming an open output must preserve the descriptor's file identity.
    _ = file:delete("compiler-lifetime-output"),
    _ = file:delete("compiler-lifetime-renamed"),
    {ok, Output} = file:open("compiler-lifetime-output", [write,raw,binary,exclusive]),
    ok = file:rename("compiler-lifetime-output", "compiler-lifetime-renamed"),
    ok = file:write(Output, <<"renamed writer">>),
    {error, enotsup} = file:position(Output, bof),
    ok = file:close(Output),
    {ok, <<"renamed writer">>} = file:read_file("compiler-lifetime-renamed"),
    {error, enoent} = file:read_file("compiler-lifetime-output"),
    ok = file:delete("compiler-lifetime-renamed"),
    %% Teardown owns descriptors that client transforms leave open.
    _ = file:delete("compiler-lifetime-unclosed"),
    {ok, Unclosed} = file:open("compiler-lifetime-unclosed", [write,raw,binary,exclusive]),
    ok = file:write(Unclosed, <<"teardown closes this descriptor">>),
    Forms.

