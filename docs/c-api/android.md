# Android application embedding

Android support targets API 26+, ARM64 (`arm64-v8a`) and x86-64 (`x86_64`), with
separate interpreter and JIT SDKs. A device and emulator of the same ABI use the
same SDK. The binaries support both 4 KB and 16 KB pages; page size is not an SDK
variant. Application qualification is tracked independently from cross-build
success in the [product fixture](../../tests/android-app/README.md).

## Installed inputs

Keep the macOS or Linux host SDK and four Android prefixes independently
relocatable. Android prefixes contain public headers, `libbeam.so`, `libbeam.a`,
optional shared `libbeam-{file,network,zlib}.so` components, `libc++_shared.so`,
ordinary bytecode inventories under `lib/beam`, CMake exports/toolchain under
`lib/cmake/BEAM`, and verification/assembly helpers under `share/beam`. They contain
no executable compiler, package manager or subprocess helper. Modern components
require the shared runtime even when the static runtime archive is available.

Select NDK r28c explicitly with `ANDROID_NDK_ROOT`. The installed verified
`BEAMToolchain.cmake` fixes the ABI, API minimum, NDK revision `28.2.13676358` and
`c++_shared` policy. Native package builds use that toolchain; generation runs only
with source-attested tools on the build host.

```sh
"$HOST_SDK/bin/beam-pkg" build --target-sdk "$ANDROID_SDK" .
"$HOST_SDK/bin/beam-pkg" install --target-sdk "$ANDROID_SDK" .
"$HOST_SDK/bin/beam-pkg" export --target-sdk "$ANDROID_SDK" --output build/export .
```

The package exporter supplies build inputs from the installed selection. It does
not compile, resolve dependencies or assemble an APK. Its policy and metadata
formats belong to the [package specification](../../../beam-erlang-package/docs/package-format.md).

## AAR and Prefab integration

Use Python 3.11+ to run the selected SDK's `share/beam/tools/beam-android.py`:

```sh
python3 "$ARM_SDK/share/beam/tools/beam-android.py" aar \
  --sdk "$ARM_SDK" --sdk "$X86_SDK" --output build/aars
```

Select two SDKs of one flavor. The command verifies their inventories, ABI and
16 KB ELF alignment, then emits separate runtime, file, network and zlib AARs.
Each AAR contains both ABIs and SDK provenance. The runtime AAR supplies the
ABI-specific public headers; component Prefab targets export that dependency.
Components remain optional. AARs contain neither ordinary SDK bytecode nor
`libc++_shared.so`; the full SDKs remain separate deliverables.

Gradle enables Prefab and uses one CMake native build. Set `BEAM_DIR` to the
selected ABI's complete SDK, set `BEAM_ANDROID_PREFAB` before `find_package(BEAM)`,
and include the package export. The installed adapter validates Prefab library
bytes and provenance against the SDK, then supplies `BEAM::Runtime` from that
Prefab target. It does not import another runtime and preserves Gradle's Prefab
search roots.

```cmake
set(BEAM_ANDROID_PREFAB ON)
find_package(BEAM CONFIG REQUIRED COMPONENTS Runtime NIF)
include("${PACKAGE_EXPORT}/BEAMPackageExport.cmake")
add_library(application_jni SHARED owner.cpp)
target_link_libraries(application_jni PRIVATE BEAMPackageInputs)
beam_target_android_application(application_jni
  COMPONENTS file network zlib INVENTORIES base services)
```

Static package NIFs link into the JNI library through generated registration
references. Android shared NIFs explicitly link the selected runtime. The native
dependency graph supplies libraries to Gradle; do not also copy them into
`jniLibs`. Gradle packages exactly one C++ runtime per ABI from the selected NDK,
whose bytes must match the SDK inventory. Reject collisions instead of using
`pickFirst`.

The `assets` command stages one SDK/export under `beam/<abi>/`, preserving export
paths and digests. Select ordinary inventories and components explicitly. Use
`AAssetManager` to read assets, including compressed entries, and verify bytecode
against the generated expectations compiled into the JNI library. Extract only
resources that need ordinary file access into app-private storage.

## Image locations and ownership

`beam_library_path` may return an absolute `archive.apk!/lib/<abi>/libbeam.so`
location. Component and shared-NIF specifications accept the same explicit form.
Normalization resolves the archive's filesystem path and validates the member;
ordinary filesystem files, including names containing `!`, remain supported.

The application resolves its native entries through `ApplicationInfo.sourceDir`
and `splitSourceDirs`, using the JNI library's compiled ABI. Reject missing,
compressed or ambiguous entries. Do not search arbitrary directories, assume
`nativeLibraryDir` contains extracted copies, or use private linker namespaces.
Gradle owns uncompressed APK entries, alignment, stripping and signing. Verify
SDK hashes before assembly; after loading, verify runtime/component build
identities and immutable bytecode, not pre-stripping SDK ELF hashes.

Use the existing owner-thread initialization, async call, bounded pump, wake and
linked-NIF APIs. One persistent OS thread owns the system and all VM operations.
JNI commands copy inputs into a synchronized queue and wake that owner. Only
owner-thread callbacks enter JNI, and they enqueue main-thread UI updates without
waiting. Never share a JNIEnv across threads or retain an Activity.

Activity recreation attaches to the existing controller. Backgrounding closes
admission and requests VM teardown; foregrounding waits before creating a fresh
VM. Persist acknowledged application state before displaying success. Process
death creates a fresh system and restores application state; it does not resume
a VM snapshot. Continuous background execution is not promised.

## Qualification

Verify every packaged ELF and APK entry for 16 KB compatibility with the installed
`verify-apk` command and observe the running process's actual page size. Test both
ABIs/flavors on explicit 4 KB and 16 KB environments, API 26 on x86-64 and current
API 36 applications. An AAB must also be converted to signed split APKs and
installed. The independent fixture owns runtime-only/component selections,
package native code, lifecycle, negative resource and isolated-input checks.
Both flavors must launch on selected physical ARM64 hardware in nondebuggable
Release builds without a debugger. Missing infrastructure leaves its gate open.
