# Runtime reference

Start with [runtime concepts](../concepts.md) for explicit composition, code and
capabilities, and VM lifetimes. These guides explain mechanisms useful when
writing or diagnosing programs:

- [Time and deadlines](time.md): monotonic time, wall clocks, units and bounded waits.
- [Memory and allocators](memory.md): process heaps, allocator categories and exposed controls.
- [Crash dumps](crash-dumps.md): host policy and interpretation of diagnostic data.
- [Match specifications](../reference/match-specifications.md): the shared VM's
  matching language and its relationship to available library interfaces.

A VM mechanism does not by itself supply an application library or a command.
The [standard-library catalog](../stdlib/README.md) identifies public modules;
the [C API](../c-api/README.md) describes controls available to embedding hosts.
