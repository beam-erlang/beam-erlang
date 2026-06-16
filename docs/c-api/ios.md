# iOS application deployment

The initial target is iOS/iPadOS 15+, ARM64 device and Apple Silicon simulator,
using the interpreter. Build each SDK with its own iOS preset and retain its
complete independently relocatable prefix. Cross-build generation uses isolated,
source-attested native tools. No executable host compiler or package manager is
installed in an iOS SDK.

A development machine keeps three sibling prefixes; none nests inside another:

```text
macos-sdk/
  bin/{beam,beam-launch,beam-pkg}
  lib/beam/tools/compiler/        # executing compiler
  lib/beam/apps/beam-pkg/         # executing package policy
  include/, lib/, share/         # normal host SDK

ios-device-sdk/                  # same shape for ios-simulator-sdk/
  include/                       # embedding and NIF headers
  lib/BEAM.framework/
  lib/BEAM{file,network,zlib}.framework/
  lib/libbeam.a                  # existing bare static-runtime option
  lib/beam/                      # bytecode, target facts and integrity inventory
  lib/cmake/BEAM/                 # exports and BEAMToolchain.cmake
  share/beam/                    # consumer verifier and assembly tools

xcframeworks/
  BEAM.xcframework/               # device + simulator slices
  BEAM{file,network,zlib}.xcframework/
  provenance.json
```

Each application separately embeds its selected frameworks and runtime resources.
The package export remains a build input, alongside the full target SDK; neither
whole directory is copied into the application.

Select Xcode with `DEVELOPER_DIR`. Each installed target supplies the verified
`lib/cmake/BEAM/BEAMToolchain.cmake`; use it as `CMAKE_TOOLCHAIN_FILE` with the
Xcode generator. It fixes the target architecture, device/simulator variant and
deployment minimum and locates Apple SDK files through the selected Xcode.

The macOS `beam-pkg` builds and installs with `--target-sdk`, then exports an
installed selection. Include its `BEAMPackageExport.cmake`, link
`BEAMPackageInputs`, and copy only `BEAM_PACKAGE_ASSETS` as resources. Package
tool policy is specified in the [package repository](../../../beam-erlang-package/docs/commands.md).

An application target can call:

```cmake
beam_target_ios_application(App COMPONENTS file network zlib INVENTORIES base services)
```

This verifies and deploys selected SDK bytecode and frameworks. The application
still explicitly reads/verifies module bytes, prepares composition, initializes
its owner-thread system and activates the VM. Xcode embeds and signs frameworks;
the final application must omit SDK headers, CMake files, compilers, package
archives and static link inputs. Do not compare signed framework bytes with
pre-signing Mach-O hashes at runtime. Keep exact runtime/component build checks
and bytecode verification against compiled expectations.

`share/beam/tools/beam-xcframework.py --device-sdk ... --simulator-sdk ... --output ...`
aggregates the four verified runtime/component frameworks and records
slice-to-SDK identities in `provenance.json`. Full SDK archives remain separate
deliverables. XCFrameworks do not contain the host compiler or replace complete
target SDKs. The existing static runtime archive remains available for bare hosts;
modern application components remain dynamic frameworks.

The independent [product application fixture](../../tests/ios-app/README.md)
owns real-app qualification. Simulator tests supplement lower-level `simctl spawn`
checks. Physical-device Release execution without a debugger remains mandatory;
neither a successful cross-build nor simulator execution closes that gate.
