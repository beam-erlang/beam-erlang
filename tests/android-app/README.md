# Android application acceptance

This product-owned fixture tests real signed Release APKs and AAB-generated split
APKs. It owns its package, static/shared NIFs, Java Activity, JNI owner thread and
runner. It never imports playground sources.

`prepare.py --host-sdk HOST --sdk ARM64 --sdk X86_64 --output NEW` consumes two
installed target SDKs of one flavor. Set `ANDROID_NDK_ROOT` explicitly to NDK
28.2.13676358. The result contains copied SDKs, installed package exports, four
Prefab AARs and ABI-specific assets. No executable host tooling enters the target
SDK or application.

`runner.py --help` lists its explicit provisioned tool and signing inputs. Use
JDK 17, Gradle 8.13, Android platform/build tools 36, CMake 3.22.1 and bundletool
1.18.2. Supply the local key alias `qualification` and its password through
`BEAM_KEYSTORE_PASSWORD`. Dependencies must already exist in the selected Gradle
home; builds always run offline and verify checked-in dependency checksums.

Pass one or more `--emulator SERIAL` and/or `--device SERIAL` selections. The runner
observes ABI, API, OS fingerprint and page size and rejects mismatches. It builds
from copied SDKs/AARs/exports while denying original input access through Seatbelt
on macOS or a private user/mount namespace on Linux. `--forbid DIRECTORY` adds
original source/build roots. Missing namespace support fails isolation.

The runner checks runtime-only, individual-component and complete applications;
static/shared NIF activation; UI heartbeats; repeated VM lifetimes; Java GC and
exceptions; Activity recreation; background teardown; and persisted state after
process death. Missing/corrupt immutable assets and invalid native selections are
prepared before signing. A subsequent valid application must recover. ART signal
handlers and owner-thread signal masks/floating-point mode are checked around VM
initialization and teardown.

Every APK set is checked for unique runtime/C++ closure, ABI, ELF and ZIP 16 KB
alignment, signatures, and absence of development inputs. The API 35 16 KB
emulator has compatibility fallback explicitly disabled and observed; API 36+
also uses manifest opt-out. Runtime identities and immutable bytecode are checked
inside the app. Pre-stripping SDK hashes are verified before assembly, not compared
to Gradle-transformed native images at runtime.

`--build-only` records execution as untested. `--keep-work` is an explicit failure
diagnosis option; remove its recorded directory after diagnosis. Normal runs
remove disposable copied inputs, build products and installed fixture apps,
retaining compact JSON evidence and command diagnostics beside `--result`.

`release/qualify-android.py` binds reports to exact transported candidate and peer
ABI archives. API 35 4 KB/16 KB, API 36, API 26 on x86-64, and both-flavor physical
ARM64 execution are mandatory release boundaries. Unavailable hardware is never
converted into a passing report. Windows application qualification is deferred.
