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

The [standard-library catalog](../stdlib/README.md) is the starting point for
library APIs. Language features do not imply that an OTP application service,
filesystem API or native component is present in a particular host.
