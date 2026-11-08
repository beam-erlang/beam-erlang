# Native calls and language operations

The host selects each native callable's public module, function and arity.
An implementation identifier describes its VM contract independently of that
name. Modern hosts normally expose VM primitives under `beam` and clocks under
`beam_time`; OTP hosts retain their canonical names. See the
[native contract catalog](../c-api/native-contracts.md) and
[composition reference](../c-api/reference.md#optional-native-bif-bindings).

Language operations retain their semantics independently of those public
exports. For example:

| Expression | Meaning |
| --- | --- |
| `X + Y` | Language arithmetic, available without a public addition binding. |
| `when is_float(X)` | Language type guard, available without a public type-test binding. |
| `beam:is_float(X)` | Ordinary public MFA dispatch; an unavailable MFA raises `undef`. |
| `when beam:is_float(X)` | Qualified guard; succeeds only with the matching guard-eligible native binding and a true result. |

An absent, ordinary or different native target fails a qualified guard. An
ordinary function at the same MFA cannot turn into a guard implementation.
Dynamic calls and external funs retain ordinary MFA dispatch and error behavior.
Explicit module names in user calls and MFA data are preserved.
The interactive evaluator also uses its target profile for generated PID, port
and reference constructors. Constant arithmetic patterns use private language
operations; reading extended literal terms requires the matching native binding.

## Compilation targets

The output target profile is independent of the VM running the compiler.
`beam compile` defaults to the modern host inventory. Auto-imported calls in
function bodies, such as `is_float(X)`, use that profile's public destination;
bare language guards retain their language meaning. Compile compiler-side
transforms with `--native-bindings=canonical`; the
[CLI reference](../cli/README.md#compilation) describes both naming profiles.

Compiler integrations can supply an explicit
`{beam_native_bindings, [{ID, Module, Function, Arity}]}` option or compile
attribute. IDs are atoms from the matching SDK catalog. Unknown IDs, wrong
arities, duplicate IDs and duplicate destinations are rejected. This describes
generated calls and qualified guards; the execution host still supplies its own
composition. These compiler integration options remain experimental.

Public calls remain conservative through optimization. A familiar function
name alone supplies no native purity, result-type, constant-folding or
control-flow guarantee. The loader and execution backend specialize a call only
after resolving its selected descriptor. Calls retain direct native dispatch.

## Private execution metadata

The compiler distinguishes public imports, language operations, private VM
execution and qualified guards. Descriptor requirements use versioned,
checksum-covered `Meta` data, validated before code publication. Debug and
abstract-code information retains the source forms.

Private execution and continuation targets do not publish callable exports.
Evaluator descriptor checks resolve to constants when code is loaded; they do
not perform a registry lookup for each expression. Native selections stay fixed
through restart and reset between VM lifetimes. The four selectable instruction
callables use their existing instruction entry points.
