%% SPDX-License-Identifier: Apache-2.0
-module(stdlib_probe).
-export([main/1]).

main([Root]) ->
    Modules = [array,dict,graph,gb_sets,gb_trees,orddict,ordsets,proplists,
               queue,sets,sofs,digraph,digraph_utils],
    Origins = beam_persistent_term:get(beam_boot_origins),
    beam_lists:foreach(fun(Module) ->
        Expected = beam_unicode:characters_to_binary(
            [Root, "/lib/beam/stdlib/ebin/", beam:atom_to_list(Module), ".beam"]),
        Expected = beam:element(2, beam_lists:keyfind(Module, 1, Origins))
    end, Modules),
    collections(),
    immutable_graph(),
    mutable_graph(),
    sofs_graphs(),
    io:put_chars("stdlib ok\n").

collections() ->
    A0 = array:new([{default,missing}]),
    A1 = array:set(3, value, A0),
    missing = array:get(3, A0),
    value = array:get(3, A1),
    [{3,value}] = array:sparse_to_orddict(A1),
    D = dict:from_list([{a,1},{b,2}]),
    {ok,2} = dict:find(b,D),
    3 = dict:fetch(a,dict:update_counter(a,2,D)),
    [a,b,c] = gb_sets:to_list(gb_sets:union(gb_sets:from_list([b,a]),
                                          gb_sets:from_list([c,b]))),
    {1,Remaining} = gb_sets:take_smallest(gb_sets:from_list([a,1])),
    [a] = gb_sets:to_list(Remaining),
    T = gb_trees:from_orddict([{a,1},{b,2}]),
    {value,2} = gb_trees:lookup(b,T),
    [{b,2}] = gb_trees:to_list(gb_trees:delete(a,T)),
    [{a,3}] = orddict:to_list(orddict:update_counter(a,2,orddict:from_list([{a,1}]))),
    [a,b,c] = ordsets:union(ordsets:from_list([b,a]),[b,c]),
    #{a := true,b := 2} = proplists:to_map([a,{b,2},{b,3}]),
    {[[a],[{b,2}]], [rest]} = proplists:split([a,{b,2},rest],[a,b]),
    Q = queue:from_list([a,b,c]),
    {{value,a},Q1} = queue:out(Q),
    [b,c] = queue:to_list(Q1),
    [a,b] = queue:to_list(queue:liat(Q)),
    true = beam:apply(queue,lait,[Q]) =:= queue:liat(Q),
    [1,2] = sofs:to_external(sofs:union(sofs:set([2]),sofs:set([1]))),
    beam_lists:foreach(fun(Version) ->
        S = sets:from_list([a,b,a],[{version,Version}]),
        2 = sets:size(S),
        [b] = sets:to_list(sets:filter(fun(X) -> X =:= b end,S)),
        [a,b] = beam_lists:sort(sets:to_list(sets:map(fun(X) -> X end,S))),
        [a] = sets:to_list(sets:intersection(S,sets:from_list([a],[{version,3-Version}]))),
        true = sets:is_equal(S,sets:from_list([b,a],[{version,3-Version}]))
    end,[1,2]),
    ok.

immutable_graph() ->
    G0 = graph:new(),
    G1 = graph:add_vertex(graph:add_vertex(G0,a),b),
    G2 = graph:add_edge(G1,a,b),
    [] = graph:vertices(G0),
    [] = graph:edges(G1),
    [a,b] = graph:topsort(G2),
    true = graph:is_acyclic(G2),
    2 = length(graph:vertices(graph:condensation(G2))),
    ok.

mutable_graph() ->
    G = digraph:new(),
    try
        a = digraph:add_vertex(G,a),
        b = digraph:add_vertex(G,b),
        _ = digraph:add_edge(G,a,b),
        _ = digraph:add_edge(G,b,a),
        [[a,b]] = [beam_lists:sort(C) || C <- digraph_utils:strong_components(G)],
        Tables = beam_lists:sort(beam_ets:all()),
        C = digraph_utils:condensation(G),
        1 = digraph:no_vertices(C),
        true = digraph:delete(C),
        Tables = beam_lists:sort(beam_ets:all()),
        Parent = self(),
        {Pid,Ref} = beam:spawn_monitor(fun() ->
            {a,[]} = digraph:vertex(G,a),
            badarg(fun() -> digraph:add_vertex(G,forbidden) end),
            Private = digraph:new([private]),
            Parent ! {private_graph,self(),Private},
            receive stop -> ok end
        end),
        receive {private_graph,Pid,Private} ->
            badarg(fun() -> digraph:vertices(Private) end),
            Pid ! stop,
            receive {'DOWN',Ref,process,Pid,normal} -> ok
            after 3000 -> error(owner_exit_timeout) end,
            badarg(fun() -> digraph:vertices(Private) end)
        after 3000 -> error(graph_owner_timeout) end,
        Tables = beam_lists:sort(beam_ets:all())
    after
        true = digraph:delete(G)
    end,
    badarg(fun() -> digraph:vertices(G) end),
    ok.

badarg(Fun) ->
    try Fun() of
        Value -> error({expected_badarg,Value})
    catch error:badarg -> ok end.

sofs_graphs() ->
    Family = sofs:family([{a,[b]},{b,[]}]),
    beam_lists:foreach(fun(G) ->
        try
            Family = sofs:digraph_to_family(G),
            Family = sofs:digraph_to_family(G,sofs:type(Family))
        after
            true = digraph:delete(G)
        end
    end,[sofs:family_to_digraph(Family),sofs:family_to_digraph(Family,[acyclic])]),
    ok.
