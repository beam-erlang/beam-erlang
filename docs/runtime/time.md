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
# Time and deadlines

Use monotonic time to measure elapsed work or set a timeout. Use system time
when a value must represent a wall-clock date. They answer different questions
and should not be substituted for each other.

## Clocks and offsets

The operating system supplies a system clock and, on supported systems, a
monotonic clock. System time can change when the clock is corrected. A monotonic
clock does not go backwards, but two consecutive reads may return the same
value. Clock resolution describes the smallest distinguishable interval;
precision describes how reliably an interval can be measured, and accuracy
describes its agreement with the intended time scale.

The VM exposes:

| Clock | Meaning | Typical use |
| --- | --- | --- |
| `erlang:monotonic_time(Unit)` | Time on the current VM's monotonic scale, with no portable wall-clock origin. | Elapsed durations and local deadlines. |
| `erlang:system_time(Unit)` | The VM's view of POSIX wall-clock time. | Timestamps interpreted outside the current VM. |
| `erlang:time_offset(Unit)` | The offset between the VM's monotonic and system clocks. | Understanding the relation between clock readings. |

In native units at a given instant, system time is monotonic time plus the
current time offset. Converting the values separately to coarser units can
introduce rounding differences. Separate calls are separate observations;
adding two readings taken at different instants need not equal a later system
clock reading. Clock correction can change the offset without moving monotonic
time backwards.

The current modern initialization uses the VM's default time correction and
multi-time-warp behavior. It does not expose the inherited emulator time-mode
flags through `beam` or the embedding options. Programs should tolerate
wall-clock corrections instead of requiring a clock that never changes.

Monotonic time belongs to one VM lifetime. Do not persist its absolute values,
compare them across VMs, or interpret them as dates. Host suspension and OS
clock behavior also affect elapsed-time observations; these clocks are not a
hard real-time execution guarantee.

## Measuring elapsed time

Read monotonic time in native units, subtract the readings, then convert the
difference to the desired unit. Native units are implementation-dependent;
conversion through `erlang:convert_time_unit/3` avoids assuming a fixed frequency.

```erlang
Start = erlang:monotonic_time(native),
Elapsed = erlang:monotonic_time(native) - Start,
erlang:convert_time_unit(Elapsed, native, microsecond).
```

This shell expression measures the interval between its two reads. Place the
operation being measured between those reads. A very short interval may convert
to zero. Unit conversion and clock resolution can lose distinctions between
nearby events; timestamps alone are not unique identifiers or proof of causal
ordering.

## Deadlines with beam_time

The ordinary base module `beam_time` represents an absolute monotonic deadline
in native units. Its full function contract is in the
[base-library reference](../stdlib/base.md#beam_time).

```erlang
Deadline = beam_time:deadline_after(1000),
Remaining = beam_time:remaining(Deadline),
Expired = beam_time:expired(Deadline),
{Remaining, Expired}.
```

`deadline_after/1` accepts a nonnegative integer number of milliseconds or `infinity`.
Use the same deadline across retries or a series of operations to retain one
overall time budget. Starting a new full timeout on every retry extends that
budget and can prevent termination under repeated failures.

`remaining/1` returns whole milliseconds, rounded down and clamped at zero.
Consequently a zero remaining value can precede actual expiration by less than
a millisecond. Use `expired/1` when the exact comparison with the native deadline
matters. `infinity` stays infinite and never expires.

A deadline is data, not a background timer: computing it does not schedule work
or cancel an operation. Components and host services define their own deadline,
waiting and cancellation behavior. See the [file](../components/file/README.md#errors-and-deadlines),
[network](../components/network/README.md#errors-and-deadlines) and
[host-service](../stdlib/services.md) manuals.

## Receive timeouts and scheduling

`receive ... after Milliseconds` uses the VM's monotonic timer machinery. Timer
resolution and scheduler load constrain when the timeout clause can execute;
expiration does not reserve a scheduler or guarantee immediate execution. A
message already eligible for selection and a timeout are also distinct events.
Use explicit acknowledgements when a protocol needs proof that work completed,
as described in the [language guide](../language/README.md#signals-and-acknowledgements).
