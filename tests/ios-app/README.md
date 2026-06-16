# Real iOS application acceptance

This independent product fixture uses only installed SDKs, public APIs and
package exports. It owns no playground code and runs through CI/release
qualification. Python 3.11+, CMake, Ninja, an explicitly selected Xcode and
installed matching-generation host/target SDKs are required.

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
python3 tests/ios-app/runner.py --host-sdk "$HOST_SDK" \
  --target-sdk "$SIMULATOR_SDK" --simulator "$SIMULATOR_UDID" \
  --work "$SCRATCH" --result "$RESULTS/simulator.json"
python3 tests/ios-app/runner.py --host-sdk "$HOST_SDK" \
  --target-sdk "$DEVICE_SDK" --device "$DEVICE_ID" \
  --signing-team "$TEAM_ID" --work "$SCRATCH" --result "$RESULTS/device.json"
```

Select a simulator UDID backed by an installed runtime; the runner never silently
chooses a runtime. Physical execution requires Developer Mode, pairing, an
unlocked device, and configured signing access. Launch uses `devicectl` without
a debugger and always builds Release. `--build-only --unsigned` is useful for
compilation but never satisfies execution or signing gates.

The runner copies both SDK and export, denies original SDK/source/export paths
during Xcode configuration/build, builds and signs normal/missing/corrupt resource
variants separately, checks that development inputs are absent from each app,
and installs/launches the real application. The app checks a linked package NIF,
resource integrity, file/loopback-network/zlib components, idle host events,
busy rejection, three VM lifetimes, copied result survival, main-thread heartbeat
progress, and VM reconstruction after foreground/background transitions.
Missing/corrupt bytecode is prepared before signing and must fail verification.

The JSON report records target identity, actual execution OS, source hashes,
commands and named variant outcomes. A companion log records diagnostics.
Disposable SDKs, exports, builds and installed applications are removed in all
catchable outcomes; a simulator booted by the runner is shut down afterward.
Use a dedicated device/simulator: the fixture's bundle identifier is
`org.erlangsters.acceptance.ios`, replaced and removed by this test.

CI uses `release/qualify-ios.py` to bind this evidence to the acquired SDK
candidate. Physical-device execution is mandatory independently of simulator
results and the deployment minimum.
