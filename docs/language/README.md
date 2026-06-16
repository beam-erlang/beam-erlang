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
# Erlang language orientation

This is a starting guide to reading the examples, not a complete language
reference. Use [get started](../getting-started.md) to run them.

## Values and matching

Erlang has numbers, atoms, tuples, lists, maps and binaries. An atom such as `ok`
is a named value. Variables begin with an uppercase letter or underscore and are
bound by matching:

```erlang
{ok, Count} = {ok, 3}.
#{count => Count, label => <<"items">>}.
```

`=` matches values; an already bound variable does not become a mutable slot.
`"text"` is a list of character codepoints, while `<<"text">>` is a binary.
APIs specify which representation they accept; for example, the component file
API uses UTF-8 path binaries.

## Modules and functions

A source file declares its module and exports the functions other modules may
call. Function identity includes the number of arguments, written as `name/arity`.
For example, `hello:main/1` is `main` with one argument in module `hello`.
Function clauses use patterns to select behavior:

```erlang
-module(greeting).
-export([message/1]).

message(<<>>) -> <<"Hello">>;
message(Name) when is_binary(Name) -> <<"Hello, ", Name/binary>>.
```

Compile this as `greeting.erl` with `beam compile`. A separate module becomes
callable only after it has been supplied to the execution VM. Use
[packages](../packaging/README.md), an embedding composition, or explicit code
loading for dependencies; compilation alone is not loading.

## Processes and messages

Erlang processes have independent mailboxes. This shell expression creates a
process and receives its reply:

```erlang
Parent = self(),
spawn(fun() -> Parent ! {reply, 42} end),
receive {reply, Value} -> Value after 1000 -> timeout end.
```

`!` sends a message. `receive` selects a matching message and may have a timeout.
A process can create other processes, but command lifetime still follows the
host's entry policy. A program using `main/1` should wait for required work before
returning.

## Signals and acknowledgements

Processes and ports communicate through asynchronous signals. Messages are one
kind of signal; links, monitors and exits use others. Sending a message does not
wait for the receiver to process it, and successful sending is not an
acknowledgement that the requested work finished.

Signals from the same sender to the same destination preserve send order. This
is not a total order across multiple senders, and it does not require `receive`
to select messages in send order: a receive expression selects a message that
matches one of its clauses. Unmatched messages remain available for later
receives. Programs must not depend on a stronger ordering suggested by one VM
implementation.

When completion matters, design a reply message and wait for it. Include a fresh
reference from `make_ref/0` in each request and its reply so that delayed replies
cannot satisfy a later request by accident. The earlier example's `reply`
message illustrates the acknowledgement, but its fixed tag is only suitable
for that one outstanding exchange. A receive timeout bounds the wait; it does
not cancel work already sent to another process.

A monitor reports process termination through a `{'DOWN', Ref, process, Pid,
Reason}` message. A link establishes a relationship through which process exits
can propagate; an exit signal is not always an ordinary mailbox message. Use
monitoring when a request protocol needs to distinguish a reply from worker
failure. The public [process interfaces](../stdlib/base.md#beam) provide spawning,
monitoring, links and sending without bringing in an application supervisor.

For timeout measurement across retries, see [time and deadlines](../runtime/time.md).
The [abstract-form reference](abstract-forms.md) describes compiler input for
advanced source transformations; it is separate from these execution semantics.

The [standard-library catalog](../stdlib/README.md) is the starting point for
library APIs. Language features do not imply that an OTP application service,
filesystem API or native component is present in a particular host.
