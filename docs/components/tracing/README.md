# Tracing

`beam_trace` configures VM tracing sessions and sequential message tracing.
The VM owns event production, trace state and session cleanup. The passive
`beam-tracing` library supplies the ordinary API, its private error formatter
and native delivery callbacks.

## Session API

| Function | Responsibility |
| --- | --- |
| `session_create(Name, Tracer, Options)` | Create an independent session. A tracer can be a PID/port using the selected default backend, or explicit `{Module, State}`. |
| `session_destroy(Session)` | Retire a session and its trace settings. |
| `session_info(Subject)` | Enumerate sessions or inspect their association with a process, port or function. |
| `process(Session, Processes, Enable, Flags)` | Select process events and their flags. |
| `port(Session, Ports, Enable, Flags)` | Select port events and their flags. |
| `function(Session, MFA, MatchSpec, Flags)` | Select function calls and a match specification. |
| `send(Session, MatchSpec, Flags)` | Configure the session's message-send match specification. |
| `recv(Session, MatchSpec, Flags)` | Configure the session's message-receive match specification. |
| `info(Session, Subject, Item)` | Inspect session settings. |
| `delivered(Session, Tracee)` | Request a delivery barrier; receive `{trace_delivered, Tracee, Ref}`. The session argument is ignored, preserving the upstream contract. |
| `system(Session, Event, Value)` | Configure session-scoped system monitoring. Its private native dependency remains `erts_internal:system_monitor/3`. |

These functions retain OTP 29.0 validation, results, events, error causes and
handle lifetimes. Match specifications use the VM's
[tracing dialect](../../reference/match-specifications.md). Delivery activation
does not create a collector: the caller supplies the recipient or custom tracer.

## Sequential API

Sequential tracing propagates a token with messages so events can be correlated
across processes. It uses a VM-wide sequential tracer independently of session
selection.

| Function | Responsibility |
| --- | --- |
| `set_token(Token)` | Set or clear the complete token. |
| `set_token(Component, Value)` | Change one token field or event flag. |
| `get_token()` | Read the complete token. |
| `get_token(Component)` | Read one token component. |
| `print(Term)`, `print(Label, Term)` | Emit a sequential print event when enabled. |
| `reset_trace()` | Clear sequential trace state. |
| `set_system_tracer(Tracer)` | Select the sequential event recipient/backend. |
| `get_system_tracer()` | Read that selection. |

## Embedding and activation

Acquire `lib/libbeam-tracing.so` with `beam_library_open`, or link
`BEAM::TracingStatic`, include `<beam/libraries/tracing.h>` and pass
`beam_tracing_library()` to `beam_library_from_static`. `BEAM::Tracing` exposes
shared delivery. Static libraries work with either runtime linkage; shared
delivery uses the shared runtime.

Use the existing [library composition sequence](../README.md): supply both
ordinary modules, the resident NIF registration and its native owner, together
with the native bindings required by the selected code. Set composition's
`default_tracer_module` to `"beam_trace"` when PID/port tracers should use this
backend. Initialize and start the VM, then call private
`beam_trace:load_nif/0` before using that backend. Explicit Host startup actions
can perform the same call before application entry. A descriptor neither
selects the default identity nor activates delivery.

The module has twenty public functions. Its ten native-access entries,
`enabled/3`, `trace/5` and `load_nif/0` are private implementation exports.
Qualified native-access calls retain load-time native dispatch; the NIF
callbacks keep their original protocol names. Explicitly selected native helper
exports can exist without acquiring or activating the ordinary library.
An unset default or inactive backend cannot deliver through a default PID/port
tracer. Explicit custom `{Module, State}` tracers remain supported.

Normal `beam run`, `beam shell` and `beam eval` select and activate tracing
before user entry. Compiler-only execution and custom application/project
compositions retain explicit acquisition policy. Canonical OTP supplies its own
tracing modules and `erl_tracer` backend.

Mandatory `beam_internal` cleanup is separate: it is embedded and protected,
starts as a VM system process, and remains present without this library.

[Return to native libraries](../README.md).
