# Paragon PR7-I

[![Build](https://github.com/ARYEZ/Paragon-PR7-I/actions/workflows/build.yml/badge.svg)](https://github.com/ARYEZ/Paragon-PR7-I/actions/workflows/build.yml)
[![Latest release](https://img.shields.io/github/v/release/ARYEZ/Paragon-PR7-I)](https://github.com/ARYEZ/Paragon-PR7-I/releases/latest)

The guitar instrument of the Paragon PR7 family: a VST3 instrument that plays a guitar from MIDI, for tight,
palm-muted djent and progressive metal rhythm parts. Its own plugin: no sampler host needed. Built with
[JUCE](https://juce.com).

It plays a clean DI guitar. For amps, cabinets and pedals, put [PR7-G](https://github.com/ARYEZ/Paragon-PR7-G) (the
family's guitar amp-and-effects plugin) after it in your DAW.

Paragon PR7-I starts from [Paragon PR7-D](https://github.com/ARYEZ/Paragon-PR7-D) 0.60.0, the family's drum
instrument, for its sample engine (velocity layers, round-robins, streaming from disk), MIDI handling, multiple
outputs, presets, undo and look. It installs alongside its siblings and keeps its own presets.

## Status

**0.1.0 is the starting point, not a guitar yet.** It is PR7-D with its own name, logo and presets folder, and with
PR7-D's groove player and song track taken out. Until the guitar engine arrives (stage 1), what it plays is still
PR7-D's drum kit.

PR7-I is built in stages, each one released (the plan, to be confirmed):

| Stage | What | Now |
|---|---|---|
| **0** | Its own plugin, from PR7-D's code, the drum-only tools taken out | **Done (0.1.0)** |
| **1** | The playing engine: each note on a string and fret as a player would choose, strings that ring and stop like real ones, sustain and palm mute | Next |
| **2** | The other articulations: staccato and chugs, pinch and natural harmonics, slides, legato, tremolo picking, fret and pick noises | Planned |
| **3** | Realism: alternate picking, round-robins and velocity layers, automatic double-tracking, a little timing and tuning variation | Planned |
| **4** | The editor and tools: strumming and chords, a tab-like note view, keyswitch maps | Planned |
| **5** | The sound source: your own DI recordings, with a "record this list" helper and an importer | Planned |

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
