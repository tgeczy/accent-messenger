# Accent Messenger reconstruction

The new native engine, NVDA and SAPI integration, and build tools are copyright 2026
Tamas Geczy and distributed under GNU GPL version 2, whose text is in LICENSE.
The companion source describes an approximate sound model; it is not Aicom's
missing DSP program. Development and testing were assisted by OpenAI Codex.

## Components

- **Outspoken number parser**: copyright 2026 Tamas Geczy, MIT. `numwords.py`
  is reused from `outspoken-nvda`, with a local fix for very long zero-padded
  input. Messenger selects its English rules. Its license is included as
  `synthDrivers/messengerExperimental/OUTSPOKEN-NUMWORDS-LICENSE.txt`.

- **Unicorn 2.1.4**: copyright its contributors, GPLv2. The native DLLs statically
  link the x86 emulation core. Unmodified corresponding source, including its
  QEMU components and notices, is included in the source ZIP. Source origin and
  SHA-256 are recorded in `native/dependencies.json` inside that ZIP.
- **NumPy 2.4.4 random distribution code and tables**: BSD terms in
  `licenses/NUMPY-LICENSE.txt` in the add-on and
  `native/third_party/NUMPY-LICENSE.txt` in the source ZIP. The native normal distribution sampler
  is adapted from `numpy/random/src/distributions/distributions.c`.
  PCG arithmetic uses the terms in `licenses/PCG-LICENSE.md` (also under
  `native/third_party` in the source ZIP).
  NumPy itself is not a runtime dependency.
- **YY-Thunks 1.1.9**, Chuyu Team, MIT: Windows 7 compatibility objects. Both
  objects were verified byte-for-byte against the official 1.1.9 object release.
  The license, matching source archive and source hash accompany them under
  `native/third_party/YY-Thunks` in the source ZIP.
  The add-on also includes `licenses/YY-Thunks-LICENSE.txt`.
- **Microsoft Visual C++ runtime**: linked statically using the licensed Visual
  Studio build tools. No separate Visual C++ Redistributable installation is
  required by these DLLs. Windows system DLLs are not copied into the add-on.
- **SPKMIC.TSR**, original Aicom Messenger DOS software: copyright Aicom and
  any other original rightsholders. This historical binary is a separate data
  input executed by the emulator; it is not covered by the new code's GPL.
  It comes from `messenger.ima` in the preserved Aicom Messenger IC archive at
  https://id10t-tech.com/filedump/Aicom%20Messenger%20IC.zip . Its SHA-256 is
  `a2963b7025b67c577e3e3839342d9f49c39c7039d0ffcad36b1f066c816a1572`.
  No public-domain dedication or redistribution license for that historical
  binary has been established by this project.

Complete corresponding source is provided as the separate companion download
`accent-messenger-0.4.0-source.zip`. Keep that download alongside
the add-on when sharing this version. It contains build instructions and
third-party notices. Private development memories are excluded.
