# First call from C

This standalone project compiles `answer.erl` with the selected BEAM/Erlang SDK,
embeds its bytecode and calls `answer:value/0` from C. It prints `42`.

Copy this directory into a writable location. Select a complete desktop SDK
containing its runtime and compiler:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/absolute/path/to/beam
cmake --build build
./build/first-call
```

On Windows, run `build/first-call.exe` with the selected SDK's runtime DLLs
available to the application. Keep the SDK and consumer native toolchains matched.
The CMake target supplies the shared runtime and its public headers; the compiler
is required only while building this example.
