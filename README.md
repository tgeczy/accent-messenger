# Accent Messenger

I'm Tamas, and I spend a lot of my spare time getting old speech synthesizers
talking again. Accent Messenger is my attempt to bring another piece of
abandonware back into everyday use. The old Aicom Messenger engine now speaks
on Windows through both an NVDA add-on and a SAPI 5 voice.

If you just want to try it, [download the latest release](https://github.com/tgeczy/accent-messenger/releases/latest).
You don't need the original card, Python, NumPy, or a separate Visual C++
Redistributable installation.

## Why I wanted to save this voice

For me, these voices are part of the history of using a computer as a blind
person. I want to be able to use them again, and I know I'm not the only one.

Aicom Corporation in San Jose made the Accent-PC, the serial-connected
Accent-SA, and the Messenger-IC, a speech card that fitted into a laptop's
Type II PCMCIA slot. The Library of Congress's
[January 1998 assistive-device catalogue](https://files.eric.ed.gov/fulltext/ED420140.pdf)
still lists the products and Aicom as a supplier on printed pages 14 and 24.

The sad part is how little of the company's later story is easy to recover.
Aicom is remembered as having faded away, leaving this technology behind.
I haven't established an exact closure date, what happened to its assets, or
where a complete engineering archive might have gone. What I do have is old
software and material that other enthusiasts took the trouble to preserve.

This project started while I was digging through OS/2 screen-reader drivers.
Old Messenger disks, board photographs, demo recordings and captures of the
data sent to the card gave us enough pieces to start making it talk. It took
reverse engineering, listening, comparisons and a fair amount of patience.
Now there's a voice people can actually install and use.

## What is original, and what is reconstructed?

The preserved disks contain real, runnable Aicom code: `SPKMIC.TSR`, the
original DOS driver. It handles English pronunciation and produces the speech
parameters. I run that software in an x86 emulator for each utterance. A new
sound generator turns those parameters into audio.

The Messenger's original DSP firmware is still missing. That means this is a
reconstruction of its sound, with the original pronunciation engine underneath.
It isn't bit-exact emulation of the card. If the original DSP program and any
required external data are recovered, an emulated DSP could provide another
sound backend. The older Accent-SA and its SSI-263 sound are a separate problem;
this project does not emulate that chip.

## Installing the NVDA add-on

Download `accent-messenger-0.4.0.nvda-addon` from the release page, install it,
restart NVDA, and select **Accent Messenger** in the synthesizer dialog.

If you have one of the experimental builds, a startup dialog asks to remove
the old package. Choose Yes, then restart to finish. Your saved Messenger
voice settings stay in place. Choosing No leaves the old package installed
and asks again next startup, because both packages provide the same synth.

The add-on includes both 32-bit and 64-bit engines. Its source is a separate
download to keep the installed add-on small.

## Installing the SAPI voice

Run `accent-messenger-sapi-0.4.0-setup.exe` with administrator approval. It
installs the voice for both 32-bit and 64-bit SAPI applications. Select
**Accent Messenger** in whichever application you use for speech.

The option to open **Accent Messenger settings** is checked at the end of
setup. You can also open the settings tool from the Start menu. It has standard
Windows controls, a text box for trying the voice, and Preview and Stop buttons.

### Where the settings live

SAPI preferences live in plain TOML files:

- Your own settings: `%APPDATA%\Accent Messenger\settings.toml`
- Shared settings: `%ProgramData%\Accent Messenger\settings.toml`

Save updates both files. Your own choices take precedence. An account without
personal settings can use the shared copy, which is the part needed by speech
running under a sign-in-screen service account. On a shared computer, the most
recent save updates the shared defaults.

If you tried an earlier local build, open settings under your normal account
and Save once to copy your choices across. Unregistering or uninstalling the
voice preserves the preferences. NVDA's own voice settings remain separate.

This doesn't turn on NVDA at the sign-in screen for you. Actual speech on
secure screens still needs live testing. More detail is in the [SAPI guide](docs/sapi.md).

## Voice controls

Both versions have rate, pitch, volume, voice characteristic, inflection and
word spacing controls. V5 is the default voice characteristic; there are ten
to try. Inflection runs from monotone to full intonation in five steps.
Word spacing starts at zero, which is already the original driver's shortest
setting. Higher values add pauses.

Numbers are read as English words by default: `100` becomes "one hundred."
The individual-digits checkbox changes that to "one zero zero."

Long passages are split into short requests, so pauses and intonation can
still change at those boundaries. The extra voice characteristics are
reconstructed variants, not recovered JAWS presets. See [voice controls](docs/voice-controls.md)
for the original commands behind the settings.

## Older Windows and NVDA versions

The declared NVDA minimum is 2021.1. The native libraries target Windows 7 SP1
and use a statically linked runtime, with YY-Thunks for compatibility. Please
use an NVDA release that supports your Windows version.

I've tested speech through both NVDA and SAPI. Automated checks also cover both
engine architectures, stopping speech, settings, number reading and upgrades.
Windows 7 and older NVDA installations still need testing on real machines.
If you report a problem, please include your Windows and NVDA or SAPI application
versions, the text you spoke, and what you heard.

## Building it yourself

You'll need Visual Studio 2022 C++ tools, a Windows SDK, CMake and Python 3.13.
The SAPI installer also needs Inno Setup 6. These are build tools; people using
the finished voice don't need them.

The [build guide](docs/native-release.md) has the native and NVDA commands.
For a full build including SAPI, start in the repository folder:

```powershell
python tools/prepare_native.py
python tools/build_sapi.py
python tools/build_nvda.py
```

The native engine lives in `native/`, the NVDA adapter in `nvda-addon/`, and
the SAPI engine and settings tool in `sapi/`. Build scripts produce files in
`dist/`; they don't install or register the voice on your machine.

### How Unicorn is handled

I don't vendor a Unicorn source tree in this Git repository. The preparation
script downloads the pinned Unicorn 2.1.4 source archive and verifies its SHA-256
against `native/dependencies.json`. It unpacks that source into the ignored
`.build/` folder. There is no locally patched Unicorn fork to maintain here.

The finished DLLs link Unicorn statically, so users don't install it separately.
The companion release source ZIP includes the unmodified upstream archive as
part of the corresponding source. Keeping the dependency out of Git and
providing its source with the release are two different parts of the build.

## Source and credits

The new code is GPLv2; see [LICENSE](LICENSE). The original Aicom driver and
the other components have their own provenance and terms, recorded in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). If you share the binaries,
please keep the matching `accent-messenger-0.4.0-source.zip` alongside them.

Thanks to the people who saved the old software and hardware material, and to
everyone listening and helping me make the voice better.
