<!--
%CopyrightBegin%

SPDX-License-Identifier: Apache-2.0

Copyright Ericsson AB 2023-2025. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

%CopyrightEnd%
-->
# Crash dumps

A crash dump records VM state during a fatal failure. It can reveal which
processes, allocations or native operations were involved. This guide describes
the shared VM's readable dump format; it does not imply that every host enables
the same failure triggers or diagnostic tools.

## Production and host policy

An ordinary Erlang exception returned through the embedding API is not a native
crash. A host can inspect its copied exception and continue according to the
[C API lifecycle contract](../c-api/reference.md#system-and-vm-lifecycle).
Fatal native failures instead terminate the process; `beam_native_fail` documents
that boundary in the [SDK guide](../c-api/deployment.md#native-headers-and-bindings).
A dump-producing failure path can still fail to write a complete dump because
of resource exhaustion, an unwritable destination or a configured limit.

Process-owned hosts can explicitly select signal and diagnostic ownership.
Bare and returning hosts preserve the host's process policy. In particular,
`SIGUSR1` is not a general instruction for obtaining a dump from an arbitrary
embedding process. Follow the host's own diagnostic procedure and the
[process-policy reference](../c-api/reference.md#process-policy-and-context).
The modern product does not supply the Observer crash-dump viewer.

When the shared VM's dump writer runs, its default destination is
`erl_crash.dump` relative to the process working directory. `ERL_CRASH_DUMP`
selects another path. The writer also observes `ERL_CRASH_DUMP_SECONDS` and
`ERL_CRASH_DUMP_BYTES`; setting either limit to zero suppresses the dump. These
settings affect an already selected dump-producing path, not whether a host
owns a signal or enables a particular native failure trigger.

A dump can contain messages, process dictionaries, binaries and other program
data. Treat it as application data when deciding where to store or share it.

## General information

The header gives the dump creation time, a slogan describing the failure,
the system version, the atom count, and the runtime thread that started the
dump. Look for `Slogan: <reason>` before interpreting the larger data sections.

Common slogans include:

| Slogan | Interpretation |
| --- | --- |
| `<A>: Cannot allocate <N> bytes of memory (of type "<T>")` | Allocator `<A>` could not obtain `<N>` bytes for block type `<T>`. Heap, heap-fragment and binary types often help identify where to investigate. |
| `<A>: Cannot reallocate <N> bytes of memory (of type "<T>")` | The corresponding failure while resizing an allocation. |
| `Unexpected op code <N>` | Possible damaged bytecode or a compiler/runtime error; preserve the exact build and input artifact. |
| `Received SIGUSR1` | A host with the relevant signal handling reached the dump path through that signal. |

Other slogans can come from the particular host or an unrecoverable VM error.
Do not infer an OTP boot or application failure merely from a missing-function
message in a modern composition. Compare the process, module and allocator
sections with the selected host's inventory and entry policy.

`Atoms: <number>` is the atom count at the time of failure. Unexpected growth
can indicate creation of many distinct atoms from external input. Atom capacity
is configured separately from process heap limits; see
[memory and allocators](memory.md).

## Scheduler Information

Under the tag _=scheduler_ is shown information about the current state and
statistics of the schedulers in the runtime system. On operating systems that
allow suspension of other threads, the data within this section reflects what
the runtime system looks like when a crash occurs.

The following fields can exist for a scheduler:

- **`=scheduler:id`** - Heading. States the scheduler identifier.

- **Scheduler Sleep Info Flags** - If empty, the scheduler was doing some
  work. If not empty, the scheduler is either in some state of sleep, or
  suspended.

- **Scheduler Sleep Info Aux Work** - If not empty, a scheduler internal
  auxiliary work is scheduled to be done.

- **Current Port** - The port identifier of the port that is currently
  executed by the scheduler.

- **Current Process** - The process identifier of the process that is
  currently executed by the scheduler. If there is such a process, this entry is
  followed by the _State_, _Internal State_, _Program Counter_, and _CP_ of that
  same process. The entries are described in section
  [Process Information](#process-information).

  Notice that this is a snapshot of what the entries are exactly when the crash
  dump is starting to be generated. Therefore they are most likely different
  (and more telling) than the entries for the same processes found in the
  _=proc_ section. If there is no currently running process, only the _Current
  Process_ entry is shown.

- **Current Process Limited Stack Trace** - This entry is shown only if there
  is a current process. It is similar to
  [_=proc_stack_](#process-data), except that only the function frames
  are shown (that is, the stack variables are omitted). Also, only the top and
  bottom part of the stack are shown. If the stack is small (< 512 slots), the
  entire stack is shown. Otherwise the entry _skipping ## slots_ is shown, where
  `##` is replaced by the number of slots that has been skipped.

- **Run Queue** - Shows statistics about how many processes and ports of
  different priorities are scheduled on this scheduler.

- **\*\*\* crashed \*\*\*** - This entry is normally not shown. It signifies
  that getting the rest of the information about this scheduler failed for some
  reason.

## Memory Information

Under the tag _=memory_ is shown information similar to what can be obtained on
a living node with `erlang:memory()`.

## Internal Table Information

Under the tags _=hash_table:<table_name>_ and _=index_table:<table_name>_ is
shown internal tables. These are mostly of interest for runtime system
developers.

## Allocated Areas

Under the tag _=allocated_areas_ is shown information similar to what can be
obtained on a living node with
`erlang:system_info(allocated_areas)`.

## Allocator

Under the tag _=allocator:<A>_ is shown various information about allocator `A`.
The information is similar to what can be obtained on a living node with
`erlang:system_info({allocator, <A>})`.
For more information, see also [memory and allocators](memory.md).

## Process Information

The Erlang crashdump contains a listing of each living Erlang process in the
system. The following fields can exist for a process:

- **`=proc:<pid>`** - Heading. States the process identifier.

- **State** - The state of the process. This can be one of the following:

  - **Scheduled** - The process was scheduled to run but is currently not
    running ("in the run queue").

  - **Waiting** - The process was waiting for something (in `receive`).

  - **Running** - The process was currently running. If the BIF
    `erlang:halt/1` was called, this was the process calling it.

  - **Exiting** - The process was on its way to exit.

  - **Garbing** - The process was garbage collecting when the crash dump
    was written. The rest of the information for this process is limited.

  - **Suspended** - The process is suspended, either by the BIF
    `erlang:suspend_process/1` or because it tries to write to a busy port.

- **Registered name** - The registered name of the process, if any.

- **Spawned as** - The entry point of the process, that is, what function was
  referenced in the `spawn` or `spawn_link` call that started the process.

- **Last scheduled in for | Current call** - The current function of the
  process. These fields do not always exist.

- **Spawned by** - The parent of the process, that is, the process that
  executed `spawn` or `spawn_link`.

- **Started** - The date and time when the process was started.

- **Message queue length** - The number of messages in the process' message
  queue.

- **Number of heap fragments** - The number of allocated heap fragments.

- **Heap fragment data** - Size of fragmented heap data, in words. This is
  data either created by messages sent to the process or by the Erlang BIFs.
  This amount depends on so many things that this field is usually
  uninteresting.

- **Link list** - Process IDs of processes linked to this one. Can also
  contain ports. If process monitoring is used, this field also tells in which
  direction the monitoring is in effect. That is, a link "to" a process tells
  you that the "current" process was monitoring the other, and a link "from" a
  process tells you that the other process was monitoring the current one.

- **Reductions** - The number of reductions consumed by the process.

- **Stack+heap** - The size of the stack and heap, in words (they share memory
  segment).

- **OldHeap** - The size of the "old heap", in words. The Erlang virtual
  machine uses generational garbage collection with two generations. There is
  one heap for new data items and one for the data that has survived two garbage
  collections. The assumption (which is almost always correct) is that data
  surviving two garbage collections can be "tenured" to a heap more seldom
  garbage collected, as they will live for a long period. This is a usual
  technique in virtual machines. The sum of the heaps and stack together
  account for the process stack and heap storage; binaries and other referenced
  allocations must be considered separately.

- **Heap unused, OldHeap unused** - The amount of unused memory on each heap,
  in words. These fields distinguish reserved heap capacity from used space.

- **Memory** - The total memory used by this process, in bytes. This includes
  call stack, heap, and internal structures. Same as
  `erlang:process_info(Pid,memory)`.

- **Program counter** - The current instruction pointer. This is only of
  interest for runtime system developers. The function into which the program
  counter points is the current function of the process.

- **CP** - The continuation pointer, that is, the return address for the
  current call. Usually useless for other than runtime system developers. This
  can be followed by the function into which the CP points, which is the
  function calling the current function.

- **Arity** - The number of live argument registers. The argument registers if
  any are live will follow. These can contain the arguments of the function if
  they are not yet moved to the stack.

- **Internal State** - A more detailed internal representation of the state of
  this process.

See also section [Process Data](#process-data).

## Port Information

This section lists the open ports, their owners, any linked processes, and the
name of their driver or external process.

## ETS Tables

This section contains information about VM tables that exist in the selected
composition, including internal tables. Their presence does not supply a public
modern `ets` module. The following fields are of interest for each table:

- **`=ets:<owner>`** - Heading. States the table owner (a process identifier).

- **Table** - The identifier for the table. If the table is a `named_table`,
  this is the name.

- **Name** - The table name, regardless of if it is a `named_table` or not.

- **Hash table, Buckets** - If the table is a hash table, that is, if it is
  not an `ordered_set`.

- **Hash table, Chain Length** - If the table is a hash table. Contains
  statistics about the table, such as the maximum, minimum, and average chain
  length. Having a maximum much larger than the average, and a standard
  deviation much larger than the expected standard deviation is a sign that the
  hashing of the terms behaves badly for some reason.

- **Ordered set (AVL tree), Elements** - If the table is an `ordered_set`.
  (The number of elements is the same as the number of objects in the table.)

- **Fixed** - If the table is fixed using `ets:safe_fixtable/2` or some
  internal mechanism.

- **Objects** - The number of objects in the table.

- **Words** - The number of words allocated to data in the table.

- **Type** - The table type, that is, `set`, `bag`, `duplicate_bag`, or
  `ordered_set`.

- **Compressed** - If the table was compressed.

- **Protection** - The protection of the table.

- **Write Concurrency** - If `write_concurrency` was enabled for the table.

- **Read Concurrency** - If `read_concurrency` was enabled for the table.

## Timers

This section contains information about all the timers started with the BIFs
`erlang:start_timer/3` and `erlang:send_after/3`. The following fields exist for
each timer:

- **`=timer:<owner>`** - Heading. States the timer owner (a process identifier),
  that is, the process to receive the message when the timer expires.

- **Message** - The message to be sent.

- **Time left** - Number of milliseconds left until the message would have
  been sent.

## Distribution Information

The shared dump format can describe connected nodes in independently composed
distribution hosts. Modern `beam` does not establish such a node. An ordinary
modern dump can report `no_distribution`; the other fields below describe a
dump whose host enabled distribution:

- **`=node:<node_name>`** - The node name.

- **no_distribution** - If the node was not distributed.

- **`=visible_node:<channel>`** - Heading for a visible node, that is, an alive
  node with a connection to the node that crashed. States the channel number for
  the node.

- **`=hidden_node:<channel>`** - Heading for a hidden node. A hidden node is a
  connection not advertised as an ordinary visible peer. States the channel
  number for the node.

- **`=not_connected:<channel>`** - Heading for a node that was connected to the
  crashed node earlier. References (that is, process or port identifiers) to the
  not connected node existed at the time of the crash. States the channel number
  for the node.

- **Name** - The name of the remote node.

- **Controller** - The port controlling communication with the remote node.

- **Creation** - An integer that, together with the node name, identifies a specific
  instance of the node.

- **`Remote monitoring: <local_proc> <remote_proc>`** - The local process was
  monitoring the remote process at the time of the crash.

- **`Remotely monitored by: <local_proc> <remote_proc>`** - The remote process
  was monitoring the local process at the time of the crash.

- **`Remote link: <local_proc> <remote_proc>`** - A link existed between the
  local process and the remote process at the time of the crash.

## Loaded Module Information

This section contains information about all loaded modules.

First, the memory use by the loaded code is summarized:

- **Current code** - Code that is the current latest version of the modules.

- **Old code** - Code where there exists a newer version in the system, but
  the old version is not yet purged.

Then, all loaded modules are listed. The following fields exist:

- **`=mod:<module_name>`** - Heading. States the module name.

- **Current size** - Memory use for the loaded code, in bytes.

- **Old size** - Memory use for the old code, in bytes.

- **Current attributes** - Module attributes for the current code. This field
  contains encoded term data.

- **Old attributes** - Module attributes for the old code, if any. This field
  contains encoded term data.

- **Current compilation info** - Compilation information (options) for the
  current code, as encoded term data.

- **Old compilation info** - Compilation information (options) for the old
  code, if any, as encoded term data.

## Fun Information

This section lists all funs. The following fields exist for each fun:

- **`=fun`** - Heading.

- **Module** - The name of the module where the fun was defined.

- **Uniq, Index** - Identifiers.

- **Address** - The address of the fun's code.

- **Refc** - The number of references to the fun.

## Process Data

For each process there can be _=proc_stack_ and _=proc_heap_ tags,
followed by raw memory information for the stack and heap of the process.

For each process there is also a _=proc_messages_ tag if the process message
queue is non-empty, and a _=proc_dictionary_ tag if the process dictionary (the
`put/2` and `get/1` storage) is non-empty.

The raw data records stack frames, heap terms, queued messages and dictionary
entries. A stack frame can identify a function and live variables, but its layout
is implementation-specific. Correlate it with the matching source and build;
do not treat an internal address as a portable object identifier. Decoding tools
can help with this format, but no viewer is supplied by the modern runtime
manuals or implied by an embedding composition.

When interpreting the data for a process, it is helpful to know that anonymous
function objects (funs) are given the following:

- A name constructed from the name of the function in which they are created
- A number (starting with 0) indicating the number of that fun within that
  function

## Atoms

This section presents all the atoms in the system. This is only of interest if
one suspects that dynamic generation of atoms can be a problem, otherwise this
section can be ignored.

Notice that the last created atom is shown first.

## Format limits

The crash-dump format follows the shared VM implementation and can evolve with
its version. Fields can be absent, incomplete or platform-dependent. This guide
explains useful diagnostic records; it is not a stable machine-readable schema.
Keep the dump's system version and the exact product build identity with a bug
report so that its contents can be interpreted against the correct implementation.
