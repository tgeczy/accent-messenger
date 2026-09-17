# Accent Messenger

Version **0.4.0** provides the Accent Messenger NVDA add-on and a native SAPI 5
engine, using the original Aicom English pronunciation software and a reconstructed
sound generator. The original Messenger DSP firmware remains unavailable.

## Aicom and the voice we are preserving

Aicom Corporation in San Jose made speech products for computer access,
including the Accent-PC, the serial-connected Accent-SA, and the Messenger-IC
card for laptops with a Type II PCMCIA slot. The Library of Congress's
[January 1998 assistive-device catalogue](https://files.eric.ed.gov/fulltext/ED420140.pdf)
still lists these products and Aicom as a supplier (printed pages 14 and 24).
For blind users, speech like this was part of the everyday experience of using
a computer. Keeping those familiar voices usable is the reason for this project.

Aicom's later story is much harder to reconstruct than its product line. The
company is remembered as having faded away, leaving its technology behind, but
this project has not established a definite closure date, an acquisition trail,
or the whereabouts of a complete engineering archive. The surviving pieces we
can work with are historical software and material saved by hardware enthusiasts.

This revival began while investigating old OS/2 screen-reader drivers. Preserved
Messenger disks, board photographs, demonstration recordings and host-to-card
captures provided the route forward. The original DOS driver still contains
the pronunciation engine and generates speech parameters. Running that driver
in an emulator, then reconstructing the sound generator and refining it through
listening, made speech possible again without the missing DSP program.

The goal is to preserve a usable piece of blind computing history while keeping
clear which parts are original and which have been reconstructed. The older
Accent-SA and its SSI-263 sound remain a separate emulation problem.

## Download and install

Download the [NVDA add-on, SAPI installer and matching source](https://github.com/tgeczy/accent-messenger/releases/tag/v0.4.0).

Install `accent-messenger-0.4.0.nvda-addon` and select **Accent Messenger**.
On restarting NVDA, the migration prompt offers to remove the old experimental
package. Choose Yes and restart to finish; existing Messenger voice preferences
are retained. Choosing No asks again on the next startup.

The separate `accent-messenger-sapi-0.4.0-setup.exe` installs both 32-bit and
64-bit SAPI support. Setup offers to open the accessible settings tool, checked
by default. It stores preferences in
`%APPDATA%\Accent Messenger\settings.toml`, with shared fallback preferences in
`%ProgramData%\Accent Messenger\settings.toml`. Save keeps both files current.
Personal values take precedence; accounts without them use shared preferences.
After replacing an earlier local build, Save once under your normal account
to copy your choices across. See the [SAPI guide](docs/sapi.md).

This release includes the listening-approved isolated E onset correction and
preserves the voice controls, louder output, responsive cancellation and earlier
consonant improvements. No speech cache, external Python, NumPy, or Visual C++
Redistributable installation is required. Source is a separate companion ZIP.

See the [installation and build guide](docs/native-release.md). NVDA 2021.1 is
the declared minimum. Both native architectures target Windows 7 SP1; actual
Windows 7 and older NVDA installations still need testing. The maintainer has
confirmed working NVDA and SAPI speech; secure-screen behavior still needs
live validation.

See [voice controls](docs/voice-controls.md) for the original commands and
reconstruction limits. The native core, NVDA adapter and SAPI adapter are
separate modules. Automated checks cover both architectures, cancellation,
PCM regression, migration, number reading, real Windows SAPI output to memory,
and personal/shared TOML settings.

Build with Visual Studio 2022 C++ tools, Windows SDK, CMake and Python 3.13.
SAPI packaging additionally uses Inno Setup 6. End users need none of these.
Build scripts never install a voice or open an audio device.

Maintained by Tamas Geczy. See [LICENSE](LICENSE) and
[component notices](THIRD-PARTY-NOTICES.md), including the separate provenance
of the original Aicom driver.
