# Voice controls

NVDA and SAPI expose voice commands implemented by the original pronunciation
driver. The sound generator reconstructs their acoustic effect.

The local original `demo_utility_1/MANUAL.DOC`, sections 3.2.2.14 through
3.2.2.18, documents pitch P0..9, voice characteristic V0..9, rate R0..H,
intonation M0..4, and word spacing S0..9. DEMO1.TXT uses M1 for its robotic
voice and restores M0; DEMO2.TXT varies both pitch and voice characteristic.

| Control | Original command | Default |
| --- | --- | --- |
| Voice V0..V9 | V0..V9 | V5 |
| Inflection 0,25,50,75,100 | M1,M2,M3,M4,M0 | 100 (M0) |
| Word spacing 0..9 | S0..S9 | 0 |

Intonation is discrete, not a continuously adjustable pitch-range multiplier.
The reduced modes can coincide for some sentences. Monotone still contains
driver transition records, so not every raw pitch byte is identical. Voice
characteristic changes the original formant target controls; pitch is separate.
The approximate renderer retains its V5 calibration and fixed upper resonances,
so these variants do not establish authentic reproduction of every demo voice.
They are not named Perfect Accent/JAWS presets, whose exact settings remain unknown.

Spacing is already at the driver's shortest setting. Changing it cannot make
the default more connected; separate NVDA requests, indexes and 160-character
chunks can also affect continuity. No silence trimming is introduced here.

## Output level

Output gain is a fixed factor of two after the synthesis/output curve.
It does not measure or smooth output loudness. Existing final integer clipping
remains a bound for exceptional peaks. The control sweep includes all ten voices,
all five intonation modes, spacing extremes and selected pitch/rate extremes;
none of those trials clips. This is not an exhaustive guarantee for all text.
At default voice settings, volume 50 matches version 0.3.6 at volume 100,
except for the isolated E/e onset correction added in 0.4.0. That correction
retains full master level at the first voiced record for E/e with an optional
final period. It leaves the original driver's raw records unchanged.

## Queued settings and validation

Development checks compared original-driver frames and headroom with the native
option path and research renderer, including restoration to defaults,
prior-version PCM, and invalid inputs. Packaged adapter tests exercise the
released controls and settings captured for queued utterances.
All option values are captured with each queued utterance. The API sends M/S
only when they change: gratuitous repetitions emit preparation records and can
alter a following onset. Cancellation during an option change invalidates the
remembered settings so the next request reasserts them. This state tracks driver
configuration; no speech audio or pronunciation results are cached.
