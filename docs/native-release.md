# Native engine and NVDA guide

For downloads and installation, start with the [project README](../README.md).
This guide covers the NVDA integration, compatibility and native build.
The [SAPI guide](sapi.md) covers its installer and settings tool.

## NVDA upgrades and settings

The installed package is `accentMessenger`. Its startup plugin detects the old
`messengerExperimental` and `accent-messenger-experimental` packages, including
disabled copies, and offers to remove them through NVDA's removal mechanism.
Choose Yes and restart to finish. Choosing No leaves the package installed and
asks again at the next startup, because both packages provide the same synth
module. A failed removal identifies the package for manual removal.

The synth identifier stays `messengerExperimental` so saved voice settings
survive the upgrade. The migration plugin is dormant once no old package
remains, and never prompts on secure screens.

Rate, pitch, volume, voice characteristic, inflection, word spacing and English
number reading are available. The individual-digits option defaults to off.
See [voice controls](voice-controls.md) for the original commands and limits.

## Runtime behavior

The original DOS pronunciation driver and reconstructed sound generator run
inside NVDA through a native DLL. Both x86 and x64 libraries are packaged;
NVDA's process architecture determines which is loaded. No external Python,
NumPy or Visual C++ Redistributable installation is required.

Cancellation stops playback, discards queued requests and interrupts rendering.
A pronunciation transaction already in progress finishes at the original
driver's idle boundary before its stale frames are discarded. The initialized
engine is retained. There is no speech cache or CPU snapshot rollback.
Audio writes release the cancellation lock while waiting for buffer space;
generation checks prevent cancelled speech from becoming current output.
Speech indexes use playback callbacks.

Long text is split at 160 characters. These boundaries can add pauses or change
intonation. Number expansion happens before splitting. Character-mode commands
and per-command pitch changes are not implemented. The sound generator remains
a reconstruction; the original Messenger DSP firmware has not been recovered.

## Compatibility and validation

The declared NVDA minimum is 2021.1. Native libraries target Windows 7 SP1,
use the static Visual C++ runtime, and link YY-Thunks 1.1.9. Use an NVDA version
that itself supports your Windows version. Windows 7 and older NVDA still need
testing on real installations; import audits and older-Python checks are
preliminary compatibility evidence.

The maintainer has tested working NVDA and SAPI speech. Automated validation
covers both architectures, a 128-case speech corpus, cancellation and recovery,
number reading, and packaged NVDA behavior with simulated audio devices.
Actual secure-screen speech still needs live testing.

When reporting a playback problem, include your Windows and NVDA versions,
the text, voice settings and what you heard. For navigation delays, distinguish
speech while holding Tab from speech after releasing it.

## Build

Requirements: Visual Studio 2022 C++ tools, a Windows SDK, CMake 3.21 or newer,
and Python 3.13. Run these commands from the repository or extracted companion
source archive:

```powershell
python tools/prepare_native.py
cmake -S . -B build-x64 -G "Visual Studio 17 2022" -A x64
cmake --build build-x64 --config MinSizeRel --target messenger --parallel
cmake -S . -B build-x86 -G "Visual Studio 17 2022" -A Win32
cmake --build build-x86 --config MinSizeRel --target messenger --parallel
python tools/audit_native.py
python tools/build_nvda.py --driver assets/SPKMIC.TSR --fit assets/calibration.json
python tools/test_native_nvda.py
```

Preparation downloads the pinned, hash-verified Unicorn source into `.build/`.
The companion release source archive includes that dependency for offline
preparation. See [Unicorn handling](../README.md#how-unicorn-is-handled) and
[component notices](../THIRD-PARTY-NOTICES.md).

MinSizeRel and the static runtime apply to both architectures, including
Unicorn. Missing Windows 7 compatibility objects are a build error. To rebuild
YY-Thunks, use the included `source-v1.1.9.zip` and its upstream build scripts.
The shipped objects match that release's official binaries.

Build tools write to `dist/`; they do not install the add-on. Packaging uses
explicit source paths and excludes private notes and research artifacts.

## Source layout

- `native/src/frontend.*`: original DOS driver execution through Unicorn.
- `native/src/renderer.*` and `noise.*`: sound model and deterministic excitation.
- `native/src/api.cpp`: C interface, without a Python ABI dependency.
- `nvda-addon/`: speech queue, ctypes boundary, audio compatibility and migration.
- `sapi/`: SAPI adapter, settings and installer.

The frontend resets Unicorn's translated-code cache at each request boundary
while retaining the guest driver's state. Range invalidation alone caused an
intermittent 32-bit access violation under repeated synthesis. Cancellation and
recovery tests cover the full-reset path.
