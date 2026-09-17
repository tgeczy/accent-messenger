# Accent Messenger 0.4.0

Install `accent-messenger-0.4.0.nvda-addon`, restart NVDA, and choose
**Accent Messenger** in the synthesizer dialog. The add-on includes both x86
and x64 engines. It needs no separate Python, NumPy, SciPy, Unicorn or Visual
C++ Redistributable installation. Complete source is a separate companion ZIP.

## Upgrading from experimental releases

The package name and installed folder are now `accentMessenger`. On startup,
its migration plugin detects `messengerExperimental` and
`accent-messenger-experimental` packages, including disabled copies. It asks
whether to remove them using NVDA's normal removal mechanism. Choose Yes,
then restart when offered. Choose No to leave them installed and be asked
again at the next startup. If removal fails, the dialog identifies the package
for removal through NVDA's add-on manager.

The synthesizer's internal identifier remains `messengerExperimental`, preserving
saved voice settings. The two package names must not remain installed together
because they supply the same synth module. The migration plugin becomes dormant
once no old package remains. It never prompts on secure screens.

## Changes in 0.4.0

- The standalone letter E/e, with or without a final period, retains full master
  level at its first voiced record. Tomi approved the straighter onset in the
  saved comparison. This narrow rendering correction leaves the original raw
  driver records intact and does not change E inside ordinary words.
- The public name is Accent Messenger, with the upgrade migration above.
- A separate [SAPI installer and native settings tool](sapi.md) share the same
  engine. SAPI preferences live in a TOML file; NVDA keeps its own voice settings.

## Voice controls retained from 0.3.7

- Fixed final gain doubles sample amplitude (about +6 dB); it introduces no
  envelope tracking or automatic normalization. At default settings, volume 50
  matches 0.3.6 volume 100, except for the new isolated E correction.
- Voice offers V0 through V9; V5 remains default. These are reconstructed
  variants, not recovered named JAWS presets. Pitch is independent.
- Inflection: 0 monotone; 25/50/75 reduced intonation; 100 full/default intonation.
  These invoke original M1/M2/M3/M4/M0 commands respectively.
- Word spacing exposes original S0 through S9. Zero is already the shortest
  setting. Long-text chunk boundaries can still add pauses.

See [voice-control verification](voice-controls.md) for manual references and
reconstruction limits.

## Voice corrections retained from 0.3.6

- Original demo measurements found our L regions substantially quieter relative
  to neighboring vowels. The new voiced-control mapping reduces this excess
  attenuation. Tomi reports improvement in the six-word comparison. The source
  level uses exponent 0.2, anchored at control 89, with the existing 4 ms timing.
  This remains an empirical reconstruction. Tomi now confirms no more wavering
  in live NVDA use with 0.3.6.
- P uses a shorter third-resonance transition at its extreme target of 255 and
  the first following target. This matches the clearer P trial without imposing
  the same timing on unrelated transitions.

## Retained from earlier builds

- Reuses the English `numwords` parser from Outspoken, following the existing
  approach of expanding numbers before the legacy pronunciation engine.
  "Read numbers as individual digits" defaults to unchecked: 100 is one hundred;
  checked, it is one zero zero. NVDA saves this setting in its voice configuration.
  Decimals, ordinals, grouped thousands and large-number handling follow Outspoken.
  Numbers are expanded before chunking so a chunk boundary cannot split a numeral.
- The download filename uses a lowercase slug and no longer embeds its source ZIP. Complete
  corresponding source remains a separate companion download to share alongside it.
- Leading records with master volume but zero excitation now prepare the vowel
  state instead of being discarded. Tomi confirmed the saved comparison restores
  the W glide in one/1. That preparation is preserved with the new level mapping.
  This is a renderer correction, not a pronunciation dictionary replacement.
- The existing 0.3.2 fixes below are retained. Tomi's live testing confirms lag
  cleared up and G/J sound fine. Additional affricate tuning is closed.
- Byte 3 has two separate fields: the original driver scales its low nibble
  independently of the high nibble. The renderer now uses only the low nibble
  for breath amplitude. Previously C3 was treated as 195 instead of 3, producing
  excessive air in words such as edge and manager. The upper field's DSP
  semantics remain unresolved.
- Audio feed no longer holds the cancellation lock while waiting for buffer
  space. Stop can interrupt that wait. A post-feed generation check clears a
  block accepted in the small check/feed race before any newer audio is sent.
- Speech indexes use playback callbacks instead of draining the player before
  subsequent synthesis. Completion callbacks discard cancelled generations.
  The player only idles at an empty-queue completion boundary.
- The original DOS pronunciation engine and sound renderer run inside NVDA.
- Cancellation stops audio and discards queued labels while retaining the
  loaded engine. A submitted pronunciation transaction finishes to the driver's
  idle boundary; its stale frames are then discarded before rendering. Rendering
  itself is interruptible. No CPU/JIT snapshot rollback is used.
- The clear profile retains consonant preparation and S/Z shaping, with the
  selected 3 dB noise reduction. No vowel-strength increase is applied.
- Both x86 and x64 DLLs are included and selected by NVDA's process architecture.
- New and older NVDA audio-device configuration layouts are supported.

Rate, pitch, volume, voice characteristic, inflection, word spacing, pause,
cancellation, speech indexes and breaks work with the English pronunciation
engine. Long text still splits at 160 characters and can acquire
extra pauses or different intonation. Character-mode commands and per-command
pitch changes are not yet implemented. This remains an experimental acoustic
reconstruction, not a faithful emulation of the missing Messenger DSP ROM.

## Compatibility and validation

The declared NVDA floor is **2021.1**. Its WavePlayer and notification APIs were
checked against the original NVDA source. Both native builds target Windows 7
SP1 or later, use the static Visual C++ runtime, and link YY-Thunks 1.1.9.
The DLL import audit finds only KERNEL32.dll, with a Windows 6.1 subsystem floor.
Use an NVDA release that itself supports your Windows version.

Windows 7 and older NVDA have **not been tested on a real installation** here.
Python 3.7 and 3.13, x86/x64 DLL execution, packaged adapter tests with audio
doubles, and the DLL import audit provide preliminary compatibility evidence.
The maintainer has confirmed working NVDA and SAPI speech. Older waveOut
playback and secure-screen behavior still need live validation.

The 128-case native comparison covers words, letters, sentences, text sanitation,
and four combinations of rate/pitch/volume. Both architectures produced the
same PCM as the research renderer with the approved voice corrections. Cancellation tests interrupt both
short and long requests, discard stale generations, and verify subsequent speech.
Reports in `dist/` distinguish synthetic audio tests from actual listening.
The original byte-field helper is separately checked in 3,840 combinations.
New adapter tests use blocked audio feeds, delayed callbacks and idle waits to
exercise cancellation races that the earlier immediate audio double missed.
These tests do not establish real-device latency. Tomi's live 0.3.6 feedback
confirms the volume swing is resolved for him. Gain tracking and output-gated voicing are excluded: Tomi heard
no useful improvement from those trials, with gating possibly worsening the
dip. The rejected 36 ms falling-amplitude timing trial is also excluded.

The final stress campaign clears Unicorn's translated-code cache at each request
boundary. Range invalidation alone produced an intermittent 32-bit access
violation during repeated synthesis. Full cache reset retains the initialized
guest driver while reclaiming generated host code; long cancellation/recovery
runs exercise this path. Some latency remains because a submitted pronunciation
request runs to idle before the next request starts.

For listening, check the voice first, then hold Tab, release it, and listen for
the final focused item. Also try Ctrl to stop, Shift to pause/resume, spelling,
different rates, and a paragraph. Report the NVDA version, Windows version,
and whether delays happen during navigation or after releasing Tab.

## Source and build

`native/src/frontend.*` runs the DOS pronunciation driver through Unicorn.
`renderer.*` implements the clear acoustic model, `noise.*` preserves its
deterministic excitation, and `api.cpp` exposes the C interface.
The NVDA package separates its queue, ctypes boundary, and audio compatibility.
There is no Python ABI dependency in the DLL.

The separate `accent-messenger-0.4.0-source.zip` includes the original driver as a separately identified asset,
calibration, native sources, unmodified Unicorn 2.1.4 source archive, and the
matching YY-Thunks objects/source/license. See THIRD-PARTY-NOTICES.md for terms.

Requirements for building: Visual Studio 2022 C++ tools and Windows SDK,
CMake 3.21 or newer, and Python 3.13 for the build scripts. End users need none
of these. From an extracted source ZIP:

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

MinSizeRel and static CRT apply to both architectures, including Unicorn.
Missing Windows 7 compatibility objects are a build error. Do not change this
to a warning or silently fall back to a modern-only DLL. The archive builder
uses explicit source roots so private `.codex/` notes are never packaged.

To rebuild YY-Thunks itself, use the included source-v1.1.9.zip and its upstream
Build scripts. The shipped objects match that release's official binaries.
