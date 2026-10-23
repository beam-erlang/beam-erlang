# Get started

Use an explicitly selected BEAM/Erlang installation. If you are building from
source, follow [building and installing](../README.md#building) first.
Set `BEAM_PREFIX` to the absolute installation directory and add its commands to
your shell's path:

```sh
export BEAM_PREFIX=/absolute/path/to/beam-install
export PATH="$BEAM_PREFIX/bin:$PATH"
beam version
```

The examples work with either the interpreter or JIT installation. Keep an
installation's executable, runtime and supporting assets together.

## Open the shell

```sh
beam shell
```

Enter expressions ending with a period:

```erlang
1 + 2.
Name = <<"BEAM/Erlang">>.
{hello, Name}.
q().
```

The shell displays expression results and keeps variable bindings between
expressions. `q().` exits. Bare `beam` opens the same shell.

For an expression from your command shell:

```sh
beam eval 'io:format("Hello, World!~n").'
```

`eval` writes requested output; it does not automatically print the expression's
return value.

## Run a source file

Save this as `hello.erl`:

```erlang
-module(hello).
-export([main/1]).

main(Args) ->
    io:format("Hello, World!~nArguments: ~p~n", [Args]).
```

```sh
beam run hello.erl -- first second
```

`main/1` receives `["first", "second"]`: a list of Unicode character lists.
The command compiles in memory, starts a fresh execution VM and finishes when
`main/1` returns. It does not write `hello.beam`. Arguments after `--` are passed
literally to your program.

To save bytecode instead:

```sh
beam compile -o ebin -- hello.erl
```

This creates `ebin/hello.beam` without executing `main/1`. Compiling a file does
not automatically make it available to an existing VM. See
[code loading and capabilities](concepts.md) for the distinction.

## Choose the next step

- For a multi-module program, read [packages and projects](packaging/README.md).
- For the command options and execution profiles, read the
  [CLI manual](cli/README.md).
- To call Erlang from a C application, follow the
  [embedding guide](c-api/guide.md).
- For APIs available to Erlang code, start with the
  [modern standard library](stdlib/README.md).
