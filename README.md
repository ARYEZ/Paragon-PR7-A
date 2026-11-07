# Paragon PR7-I

[![Build](https://github.com/ARYEZ/Paragon-PR7-I/actions/workflows/build.yml/badge.svg)](https://github.com/ARYEZ/Paragon-PR7-I/actions/workflows/build.yml)
[![Latest release](https://img.shields.io/github/v/release/ARYEZ/Paragon-PR7-I)](https://github.com/ARYEZ/Paragon-PR7-I/releases/latest)

The guitar instrument of the Paragon PR7 family: a VST3 instrument that plays a guitar from MIDI, for tight,
palm-muted djent and progressive metal rhythm parts. Its own plugin: no sampler host needed. Built with
[JUCE](https://juce.com).

It plays a clean DI guitar. For amps, cabinets and pedals, put [PR7-G](https://github.com/ARYEZ/Paragon-PR7-G) (the
family's guitar amp-and-effects plugin) after it in your DAW.

Paragon PR7-I started from [Paragon PR7-D](https://github.com/ARYEZ/Paragon-PR7-D) 0.60.0, the family's drum
instrument, for its streaming from disk, presets, undo, MIDI learn and look. It installs alongside its siblings and
keeps its own presets. The guitar is a 7-string in drop A (A E A D G B E).

## Status

PR7-I is built in stages, each one released:

| Stage | What | Now |
|---|---|---|
| **0** | Its own plugin, from PR7-D's code | **Done (0.1.0)** |
| **1a** | The playing engine with sustain: strings and frets as a player chooses, one note per string, alternate picking with round-robins, velocity layers; render lists and importing renders; the drums and effects removed | **Done (0.2.0)** |
| **1b** | Palm mute (by velocity, as the reference does), your own key map (C-1 palm mute / sustain by velocity), power chords | **Done (0.3.0)** |
| **1c** | The GUITAR page in PR7-D's Tron look, laid out like the reference's main page | Next (mock-up first) |
| **2** | The other articulations: staccato and chugs, pinch and natural harmonics, slides, legato, tremolo picking, fret and pick noises | Planned |
| **3** | Realism: automatic double-tracking, a little timing and tuning variation | Planned |
| **4** | The editor and tools: strumming and chords, keyswitch maps | Planned |

## Using it

1. **Render the lists.** On the GUITAR page, each string has a MIDI file (click MIDI to save it, drag it onto a DAW track,
   or SAVE ALL LISTS). Load it on a track with your guitar instrument as a clean DI (its effects off, one guitar, no
   anti-repetition, ringing or release noises) and bounce it to a WAV from bar 1 to the end. Each string is 300 notes:
   every fret 0-24, three dynamics, two takes each of a down and an up stroke, about 28 minutes.
2. **Import.** Drop each WAV on its string's row. PR7-I finds the notes, cuts them, checks their pitch and saves them in
   the guitar's folder (CHOOSE FOLDER... to put it elsewhere). A wrong file is refused and the row says why.
3. **Play.** Notes go where a player would put them; the fretboard along the bottom shows each one's string and fret.
   Put PR7-G after PR7-I for the amp, cabinet and effects.

**Keyswitches** (the reference instrument's defaults, so parts written for it play the same): force string C-2 (low A)
to F#-2 (high E), G-2 off; hand position A#-2 (its velocity is the fret), B-2 auto; picking up / down / alternate C7 /
C#7 / D7. Articulations by KEY MAP: **Split** (default): C-1 palm mute at velocity 1-59, sustain at 60-119, C#-1 the
same as power chords; **Factory**: C-1 sustain, C#-1 palm mute, D#-1 / E-1 power chords. Note names as in Cubase
(middle C = C3); SETTINGS can switch to C4.

## Download

Get the latest version from the **[Releases](https://github.com/ARYEZ/Paragon-PR7-I/releases/latest)** page: download
the zip for your computer. (Builds of every push also appear under **Actions** → a run → **Artifacts**.)

Unzip it and copy `Paragon PR7-I.vst3` to your VST3 folder:

| OS | VST3 folder |
|---|---|
| Windows | `C:\Program Files\Common Files\VST3` |
| Linux | `~/.vst3` |

There's no macOS build for now (only Windows and Linux).

Then rescan plugins in your DAW: PR7-I shows up as an instrument.

## Making a release

1. Set the new version in `CMakeLists.txt` (`project(ParagonX1 VERSION x.y.z)`).
2. Add a `## x.y.z` section to `CHANGELOG.md`.
3. Commit and push.

When a push carries a version that hasn't been released yet, GitHub Actions builds Windows and Linux, tags the commit
`vx.y.z` and publishes the release with notes from the changelog.

## Building from source

Requirements: CMake 3.24+, a C++20 compiler (Visual Studio 2022, or GCC 11+/Clang 14+), and Git.
JUCE and Catch2 are downloaded automatically at pinned versions.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The plugin is written to `build/ParagonX1_artefacts/Release/VST3/` (the build target keeps its family name).

On Linux, install JUCE's dependencies first (the list is in `.github/workflows/build.yml`).

To render the editor to an image without a DAW:

```bash
PARAGON_SNAPSHOT=editor.png ./build/tests/ParagonX1Tests "[snapshot]"
```

## License

Paragon PR7-I is free software under the [GNU Affero General Public License v3](LICENSE.md),
which is required by JUCE's open-source licence.

It uses:
- [JUCE](https://github.com/juce-framework/JUCE): AGPLv3
- The Inter typeface (in `resources/fonts`) by Rasmus Andersson, under the SIL Open Font License

PR7-I is our own design and code, inspired by how commercial guitar instruments play. It ships no third-party
sounds: it plays your own recordings from your computer.
