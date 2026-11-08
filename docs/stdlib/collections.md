# Standard-library collections and graphs

These thirteen ordinary Erlang modules retain the complete OTP 29.0 default
APIs and exported types under their upstream names. Load the verified `stdlib`
inventory together with the required base providers. Normal `beam run`, shell
and eval already select it. C hosts use `BEAM_SDK_INVENTORY_STDLIB` through
[SDK inventory verification](../c-api/deployment.md#verify-ordinary-bytecode).
Loading code alone starts no workers and acquires no OS services.

| Module | Functions | Role |
| --- | ---: | --- |
| `array` | 50 | Extensible functional arrays, defaults, sparse traversal and conversions. |
| `dict` | 21 | Functional hash dictionaries with exact key equality. |
| `graph` | 63 | Immutable directed graphs, traversal, connectivity and condensation. |
| `gb_sets` | 42 | Ordered balanced-tree sets and iterators. |
| `gb_trees` | 31 | Ordered balanced-tree dictionaries and iterators. |
| `orddict` | 21 | Ordered-list dictionaries. |
| `ordsets` | 21 | Ordered-list sets. |
| `proplists` | 22 | Property-list lookup, normalization, partitioning and map conversion. |
| `queue` | 38 | Functional double-ended FIFO queues. |
| `sets` | 23 | Functional sets with both version-1 and version-2 representations. |
| `sofs` | 83 | Typed sets, relations, families and graph conversions. |
| `digraph` | 34 | Mutable directed graphs backed by process-owned ETS tables. |
| `digraph_utils` | 18 | Algorithms over ETS-backed directed graphs. |

Collection equality, ordering and representations remain specific to each
module. For example, `dict` and version-1 `sets` retain `beam:phash/2` hashing;
`sets` supports both representations and mixed-version operations. Compatibility
helpers and aliases remain exported, including deprecated `queue:lait/1` (use
`queue:liat/1`), `array:upgrade/1` and `sofs:fam2rel/1,rel2fam/1`. The three
upstream `graph:dump/1,2,3` debugging helpers remain disabled.

```erlang
Q = queue:from_list([first, second]),
{{value, first}, Rest} = queue:out(Q),
[second] = queue:to_list(Rest).
```

`graph` operations return new values; old graphs remain usable and discarded
values are garbage collected. `digraph:new/0,1` creates three ETS tables whose
creator owns writes. Protected graphs permit other processes to read; private
graphs permit only their owner. Explicit `digraph:delete/1` or owner exit reclaims
the tables. Dropping the graph term alone does not delete them.

`digraph_utils:condensation/1` returns a new ETS-backed graph that its caller must
delete. Its temporary tables follow the existing explicit cleanup and ETS owner
lifetime. All four `sofs:family_to_digraph/1,2` and
`sofs:digraph_to_family/1,2` functions are available; graphs created by the former
also require deletion. Neither graph representation substitutes for the other.

Compiler tooling retains its separate canonical sources and private VM lifetime.
OTP installs its own canonical modules and does not acquire this modern inventory.
