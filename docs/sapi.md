# Accent Messenger SAPI

Install `accent-messenger-sapi-0.4.0-setup.exe` with administrator approval,
then select **Accent Messenger** in a SAPI 5 application. Both 32-bit and
64-bit clients are supported. Close speech applications before updating.
The installer does not change your default voice. Uninstall through Windows
Apps/Programs; saved preferences remain for a later reinstall.

Setup offers **Open Accent Messenger settings**, checked by default. You can
also open it from the Start menu. The native Windows
dialog offers voice characteristic, inflection, word spacing, base rate,
pitch, volume, individual-digit reading, and a preview. Save and Preview update
both `%APPDATA%\Accent Messenger\settings.toml` and
`%ProgramData%\Accent Messenger\settings.toml`. Personal values take precedence
one setting at a time, then valid shared values, then built-in defaults. A missing
or invalid personal entry does not hide the shared setting. Both architectures
read the same files on the next utterance.

Version 0.4.0 includes shared settings. If you tested an earlier local build,
open the settings tool under your normal account and Save once to copy your
existing choices to ProgramData. Subsequent saves keep that copy current.
An account without personal settings, including a sign-in-screen service account,
can then read the shared preferences. On a multi-user computer, the shared copy
reflects the most recent successful save; each person's own choices still win.
The dialog reports if only the personal file could be saved.

Installation and DLL registration prepare the shared preferences folder so
standard users can update it. Executables and speech data remain in Program
Files. Re-registering preserves existing settings; unregistering and uninstalling
leave both settings files for later reuse. Registration does not copy the
administrator's profile over another person's choices.

No voice preferences are stored in the registry; system-wide COM and voice-token
registration necessarily use it. The settings change supplies shared preferences;
it does not configure NVDA to run on sign-in or secure screens. Actual speech
on those screens still needs live testing.

The real Windows stream test rejected the initial per-user token with access
denied before calling the engine; the machine token passed. A related
[Wine test correction](https://github.com/wine-mirror/wine/commit/327667a620b1d0c9f8dd47b9a85343a7badd4b20)
uses the same registry-root change.

The settings file uses these defaults (ReadDigits is 0 or 1):

```toml
Voice = 5
Inflection = 100
WordSpacing = 0
Rate = 50
Pitch = 50
Volume = 100
ReadDigits = 0
```

The native engine and approved sound match the NVDA add-on. SAPI application
rate, volume and XML pitch/volume changes combine with the settings. Compatible
fragments and bookmarks stay together until the normal 160-character chunk
limit, preventing an artificial pause at every fragment. Bookmarks and word
events retain order, with estimated timing within each chunk: the original
driver supplies no word timestamps. Skip requests report zero skipped units.
Phoneme-tag pronunciation falls back to its supplied text; a SAPI phoneme
alphabet mapping is not implemented.

XML and application rate/volume are combined as specified in Microsoft's
[SAPI engine porting guide](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee431802(v=vs.85)).
The old-host zero-byte-write retry follows the local Votrax SAPI adapter, with
cancellation polling and a five-second no-progress timeout.

Cancellation is checked during synthesis and while writing 20 ms audio blocks.
The initialized DOS engine is retained; no speech cache is used. One settings
file is read at each utterance. No transcript or diagnostic log is written.

The original DSP firmware remains unavailable. This is the original Aicom
pronunciation engine with a reconstructed sound generator, not a bit-exact
DSP emulation. The name has changed because the add-on is usable, not because
the missing firmware was recovered.

Both builds use MinSizeRel, static Visual C++ runtime, Windows 7 SP1 targets
and YY-Thunks. Windows 7 still needs real-machine validation. No external
Python or Visual C++ Redistributable is required. The matching source archive
is `accent-messenger-0.4.0-source.zip`, shared with the NVDA release.

Build with Visual Studio 2022, Windows SDK, CMake, Python and Inno Setup 6:
`python tools/prepare_native.py`, then `python tools/build_sapi.py`.
The build only stages and packages files; it does not register voices or play
audio. Native SAPI tests use a process-local registry override and capture
PCM without selecting an audio device.

Validation covers both architectures: real Windows SpVoice output to an in-memory
PCM stream, COM lifetime and isolated registration/unregistration, grouped
bookmarks, partial and zero-byte writes, cancellation during rendering and output,
volume, settings restoration, and 242 English number-parser comparisons per
architecture. The native settings dialog is exercised while hidden. These checks
open no audio device and install no voice into the real registry. Live SAPI
application and Windows 7 testing remain separate from these automated checks.

Shared-settings tests isolate both file paths in scratch directories. They check
personal precedence, missing-user/service-account fallback, invalid and oversized
files, partial-save reporting, and retention after unregistering. Neither the
real AppData nor ProgramData settings are changed by build-time tests.
