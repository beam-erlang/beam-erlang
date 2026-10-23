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
# Memory and allocators

The VM separates memory used for different purposes to limit fragmentation and
balance allocation speed against memory use. These internal allocator categories
are useful when reading diagnostics; they are not individually selectable
services or public allocator objects.

## Process memory

An Erlang process has its own stack and heap. The VM uses generational garbage
collection: newer data is collected more frequently, while surviving data can
move to an older generation. Messages and temporary work can also use heap
fragments. Reference-counted binaries can occupy memory outside the process
heap, so heap size alone does not describe every byte retained by a process.

Garbage collection reclaims unreachable Erlang data. It does not promise that
the allocator immediately returns every freed byte to the operating system.
Likewise, a process-memory total, live allocated blocks, allocator carrier sizes
and operating-system resident memory measure different things. Compare like
measurements when diagnosing growth.

## Allocator categories

| Name | Primary use |
| --- | --- |
| `eheap_alloc` | Process heaps and related Erlang heap data. |
| `binary_alloc` | Binary data. |
| `ets_alloc` | VM table storage. |
| `driver_alloc` | Native driver data. |
| `literal_alloc` | Constant terms in loaded code. |
| `temp_alloc` | Temporary allocations. |
| `sl_alloc`, `ll_alloc` | Blocks expected to be short-lived or long-lived. |
| `fix_alloc` | Frequently allocated fixed-size data types. |
| `std_alloc` | Other ordinary VM allocations. |
| `sys_alloc` | The operating system's underlying allocator, normally `malloc`. |
| `mseg_alloc` | Memory segments used by other allocators on supported systems. |

The `alloc_util` framework manages areas called *carriers*. A multiblock carrier
holds several allocations; a singleblock carrier holds one allocation. Carriers
come from memory segments or the system allocator. Free blocks can be coalesced
and reused, and a segment cache reduces repeated operating-system allocation
calls. A carrier may therefore remain allocated after some of its blocks become
free.

Different allocator strategies trade search cost, fragmentation and locality.
Those strategy choices and carrier thresholds are implementation settings; the
modern public API does not expose the inherited per-allocator tuning flags.

## Public controls

Embedding hosts configure a fresh VM using typed fields in `beam_vm_options`.
The [C API reference](../c-api/reference.md#discovery-and-configuration) owns the
exact defaults, accepted ranges and initialization rules.

- `min_heap_size` and `min_bin_vheap_size` set process heap and binary-accounting
  thresholds. They are expressed in words, with supported-size rounding.
- `max_heap_size` is a per-process limit; zero disables it. It is not a VM-wide
  memory budget and does not reserve memory in advance.
- `fullsweep_after` controls the default generational-collection count before a
  full sweep.
- `process_limit`, `atom_limit`, `port_limit` and `ets_limit` control capacities
  or sizing hints. A configured capacity is not guaranteed available memory.
- `allocator_tagging` selects the default, enabled or disabled tagging policy
  for the supported allocator group. Temporary, literal and test allocators
  retain their own defaults.

Allocator tagging is a per-VM choice. The default preserves the VM's mixed
allocator defaults; it is not equivalent to explicitly enabling all tags.
Initialize options through `beam_vm_options_init` and use a matching installed
SDK. The `beam` command does not accept a general emulator-argument string for
these settings.

## Reading memory diagnostics

A [crash dump](crash-dumps.md) can identify the allocator and block type involved
in a failed allocation. Compare process heap and message-queue data, binary
retention, loaded code, tables and allocator carriers before attributing growth
to one source. The presence of internal table data does not imply an `ets`
module is supplied to an application's modern composition.

An allocation failure can be fatal. A normal returned Erlang exception and a
native fatal-memory path have different consequences for an embedding host;
see [crash-dump production](crash-dumps.md#production-and-host-policy).
