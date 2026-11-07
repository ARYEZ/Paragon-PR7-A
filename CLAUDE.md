# Paragon PR7-I: notes for Claude

This repository starts as a copy of **Paragon PR7-D** (the drum instrument, repo `ARYEZ/Paragon-PR7-D`) at PR7-D **0.60.0** plus its CI fix for pluginval (branch `claude/new-session-yhgoe4`, commit `aab0ee8`), full history included, to become **Paragon PR7-I**, the **guitar instrument** of the PR7 family: a VST3 instrument played from MIDI, a spiritual cousin of Impact Soundworks' Shreddage 3 (it works the way that plugin works, but sounds and runs the way the owner wants; no Kontakt). The owner plays **djent and progressive metal**: weigh features by what helps tight, palm-muted rhythm parts. Siblings: **PR7-G** (the guitar amp-and-effects plugin, `ARYEZ/Paragon-PR7-G`), **PR7-D** (drums), **PR7-K** (synth), **PR7-B** (bass), **PR7-V** (vocal), **PR7-C**, **PR7-X** (utility; PR7-E until its 0.20.0), **PR7-M**, and **PR7-S** (the owner's DAW, based on Cubase 15).

**PR7-G is the amp side** (checked in its README, November 2026: Neural Amp Modeler captures, cab IRs, pedals). So PR7-I puts out a clean DI guitar and the owner puts PR7-G after it; PR7-I doesn't rebuild amps.

## First: make it its own plugin (0.1.0)

1. **CMakeLists.txt** (`juce_add_plugin`): `PRODUCT_NAME` "Paragon PR7-I", `BUNDLE_ID` `com.paragon.paragonpr7i`, `PLUGIN_CODE` `Pr7i` (never another sibling's: `Px1a` PR7-G, `Pr7b`, `Pr7v`, `Pr7c`, `Pr7e` PR7-X, `Pr7k`, `Pr7d`, `Pr7m` PR7-M), `PLUGIN_MANUFACTURER_CODE Prgn`; an instrument, `VST3_CATEGORIES Instrument Sampler`; version restarted at 0.1.0 (the CMake target name stays `ParagonX1`, as across the family).
2. **Presets folder** (`source/rigs/RigLibrary.cpp`): "Paragon PR7-I" (tested in `tests/RigTests.cpp`); nothing carries over from PR7-D's. The preset file extensions (`.pr7rig`, `.pr7kit`, `.pr7comp`...) are the family's and stay.
3. **Settings file** (`source/ui/Finish.cpp`): `applicationName` "Paragon PR7-I". Also the temp folder (drag files) and the disk streamer's thread name.
4. **Logo** (`source/ui/Logo.cpp`): "-I", an I-beam I from the P's parts (0.24 stem between 0.2 bars top and bottom, so it doesn't read as a 1 or an l beside the slanted 7); the icon (`drawIcon`) is "7I".
5. **Text:** About (SETTINGS), MIDI-link tooltips, rack faceplates ("PR7-I SERIES"), README.
6. **Release workflow:** `Paragon PR7-I.vst3`, `ParagonPR7-I-${TAG}-...` zips, "Paragon PR7-I ${TAG}" (the identity commit switched the release job off, `if: false`, so CI couldn't publish a half-made 0.1.0; 0.1.0 switched it back on).
7. **CHANGELOG.md** starts at 0.1.0.
8. **Drum-only tools taken out** (0.1.0): the GROOVES tab and everything behind it (`source/grooves/`: the groove library, groove databases and their SQLite reader, preview, RIFF MATCH, TAP TO FIND), the song track and SONG CREATOR (`source/song/`, `SongTrack`, `BlockEditor` (grid editor), `PlayStylePanel`, `SongCreator`, bounce, song structures), and their tests, test data and snapshot scenes. MainView: five tabs (DRUMS MIXER FX PRESETS SETTINGS, `tabEdges` six edges); the strip under the pages holds only a dim note (`stripNote`) saying the guitar's note view goes there (**no on-screen keyboard**: PR7-D 0.41.0 took it off every screen at the owner's ask). The drum sequencer grid no longer takes dropped MIDI files (they went through the groove reader). Undo snapshots and the project state no longer carry a song.
9. **Kept for now, on purpose:** the drum kit's voice engine and the DRUMS page (kit, KIT VIEW, sequencer, DRUM MACHINE, MIXER mic channels, simulated mics, DrumGizmo kits). Until stage 1 they're the only thing that makes sound, and they hold the pieces the plan keeps (sample loading, articulations, velocity curves, MIDI IN, IMPORT A RENDER). **Stage 1 replaces them** with the guitar engine and a guitar page, then removes the rest of the drum code at once.

## What to keep from PR7-D (the brief)

- **The sample engine:** `dsp/synth/Instrument.*` (velocity layers, round-robins, SFZ and folder loading), `DiskStreamer.*` (streaming from disk), `SamplePack.*` (name rules for a sample folder). Note: `Instrument` lives in `synth::drums` and takes `Sample` / `HitMemory` from `Drums.h`; stage 1 has to move those out before `Drums.*` can go.
- **MIDI handling:** MIDI-in mapping, keyswitch-style articulation slots, velocity curves (`VelocityCurve.h`), the MIDI monitor (`MidiInPanel`).
- **The infrastructure:** multiple outputs, undo, presets, the test kit (`tests/PluginTestUtils.h`), CI with pluginval (keep the retry for pluginval's known Linux shutdown crash), the PR7 look-and-feel, the progress box, RENDER IMPORT (the model for a "record this list" helper and importer).
- PR7-D's own notes at the seed commit (`git show aab0ee8:CLAUDE.md`) describe all of this code in detail.

## The plan (offered to the owner; confirm it with them, one stage per release)

1. **The playing engine.** Each note gets a string and a fret the way a player would choose them, with control over string and position. Strings ring and stop like real ones: one note per string, note-offs, letting a string ring. Sustain and palm mute (by velocity zone or keyswitch).
2. **The other articulations.** Staccato and chugs, pinch harmonics, natural harmonics, slides, legato hammer-ons and pull-offs, tremolo picking, fret and pick noises.
3. **Realism.** Alternate up/down picking, round-robins and velocity layers; automatic double-tracking (a second, slightly different performance, panned); a little timing and tuning variation.
4. **The editor and tools.** Strumming and chords, a tab-like note view (in the strip under the pages), keyswitch maps.
5. **The sound source the owner picks:** their own DI recordings (recommended: a "record this list" helper and an importer, like IMPORT A RENDER); notes they render out of Shreddage (their licence call); modelled strings (a backup only: PR7-D's modelled drums showed synthesis falls short of real recordings).

**Open questions for the owner (asked with 0.1.0):** which sound option; Shreddage 3's manual (work from its chapters, never commit it); the guitar (six, seven or eight strings; tunings, e.g. drop A, drop E); whether the effects rack PR7-D carried (ECHELON, DOMINION, CONSTRICTOR, LIMITER, sends, inserts) stays, now that PR7-G does the guitar's effects.

## Rules (from the owner and the family's CLAUDE.md files)

- **Names:** our own designs, "inspired by"; **no Impact Soundworks, Shreddage or Kontakt names or branding** on the plugin (nor PR7-D's Toontrack / Superior Drummer / EZdrummer ones). Never read or ship their library files: Kontakt libraries are protected, and we never get round copy protection.
- **Audio** comes only from the owner's own recordings, or from sounds they render out themselves; whether the vendor's licence allows re-sampling is the owner's call. Never commit it.
- **Manuals** (Shreddage 3's, and PR7-D's SD3 extract) stay with the owner (uploads / `ARYEZ/manuals`): never commit them.
- **Colours: the PR7 family's only** (the owner, 0.11.0): the red / pink / orange range (hue about 318° through 0° to 42°), pearl white and onyx (greys, near black). No green, blue, purple, yellow or cyan anywhere, JUCE's defaults included (`ParagonLookAndFeel` sets a family `ColourScheme` first: its fill and highlight orange). the `[colours]` tests check the look-and-feel's slots and the palette (PR7-D's lived in `tests/SongTests.cpp`, removed with the song track: **add a palette test back in stage 1**); check new screens' pixels too (the scratchpad's `colours.py` scans the source). The kit view's pictures are checked by `KitViewTests.cpp`'s colours test.
- Ask for source material (manual chapters, example files) rather than invent specifics; say when something is a guess or our own design; say what can't be checked here (listening).

## The project

- An AGPLv3 **JUCE VST3** instrument in the PR7 family's look, sharing the family's code.
- **Code layout:** the kit and sequencer in `source/dsp/synth/` (the folder name and `synth::` namespace are PR7-K's), the effects in `source/dsp/effects/`, `dsp/eq/`, `dsp/dominion/`; UI in `source/ui/`, parameters in `source/Parameters.*` and `source/RackParameters.cpp`, presets ("rigs" in the code, as across the family) in `source/rigs/`, tests in `tests/`. (PR7-D's groove player, `source/grooves/`, and song track, `source/song/`, were removed in PR7-I 0.1.0.)
- **Parameters:**
  - Saved by ID and choices by **index**: only ever *append* choices, never reorder (`drums::modelNames`, `chokeNames`, `drumseq::triggerNames`, `reverseNames`, `rollNames`, `seq::divisionNames`, `timingNames`, `ParamIDs::insertBusNames`, and the effects' lists).
  - New parameters get a new version hint number (PR7-D's went up to 40; PR7-I's own start at 41).
  - Migrations for changed defaults live in `setStateInformation` and `Rig::fromXml`.
- **Shared DSP pieces:** `BypassFade` and `TailWatch` (EffectUtils.h), and `Circuit` (Circuit.h: models analog stages from their schematics, part by part).
- **The drum code, as PR7-K built it** (its CLAUDE.md's stage 6 has every detail; in short):
  - `dsp/synth/Drums.*`: `drums::Kit`, 20 slots since 0.2.0, 23 since 0.17.0 (`slotDefaults`: C BD, C# RIM, D SD, D# CLP, E SD2, F LT, F# CH, G MT, G# PH, A HT, A# OH, B CY, then RD RB CR2 CHN SPL HH TMB SHK, then RE HO1 HO3; the hats, HH / HO1 / HO3 too, in CHOKE group 1; drums 13-20's parameters have version hint 24, 21-23's 26); models KICK SNARE CLAP CLOSED HAT OPEN HAT TOM RIM COWBELL CYMBAL SAMPLE RIDE BELL TAMBOURINE SHAKER; `KitSettings::mapped` / `keys` route notes through the note map (`NoteMap.h`) instead of ROOT; `modelLevel` puts every model's default hit at -3 dBFS; up to 6 hits per slot (VOICES), 48 hits at once (`maxHits`; 192 voices since 0.20.0 for multi-mic hits); CHOKE fades the group over 5 ms; one-shots. `Outputs`: the mix, the sends (only while their effect is on and the drum isn't on a bus), the insert buses; the kit *adds* into them. `sampleFiles::load / encode / decode / findHits`. SAMPLE START / END, the sample editor (GAIN, MONO, DELAY with the anchor, LP / HP, the drawn envelope), reverse (`reverseSource`, the processor's `updateDrumReverses`).
  - `dsp/synth/DrumSequencer.h`: `drumseq::Note` / `Pattern` / `Settings`, `hitTime`, `forEachHit` (the one place hits are worked out), `toMidi`. SONG follows `ppq`; KEY: `Kit::patternForKey` (root-4..root-1).
  - Processor: `getKitSettings`, `getDrumSeqSettings`, samples as shared pointers handed to the audio thread as plain pointers (`samplesForAudio`, retired 2 s), kits and sequences (`captureDrumKit`, `applyDrumKit`, `loadDrumKit`, LINK...), `drumPatternToMidi`, `previewDrum`.
  - UI: `ui/DrumPage.*` (rows, `DrumHitDisplay`, `HitPicture`, the envelope strip, several chosen), `ui/DrumSeqPage.*` (`DrumSeqGrid`, `NoteField`, `DrumSeqPanel`).
- **The effects, as PR7-K ported them** (its CLAUDE.md's stage 7): PR7-E 0.18.1's ECHELON, DOMINION, CONSTRICTOR and LIMITER in one heap `Rack` (`source/Rack.h`); PR7-G 0.52.0's DELAY / REVERB as send returns, MOD, MOD 2, LO-FI, and the insert buses' DRIVE / GLITCH (with PR7-K's speed-ups, marked in the code). When PR7-E or PR7-G changes these effects, port the change.

## Build, test, check

- **Build:** `cmake -B build && cmake --build build -j8`. On a fresh Linux machine, install the packages listed in `.github/workflows/build.yml` first.
- **Tests (Catch2):** `xvfb-run -a build/tests/ParagonX1Tests`. All must pass. Windows gives a thread 1 MB of stack (Linux 8 MB): keep big data (patterns, tables) on the heap, never inline in `ModMatrix`, `Rig` or the kit's settings (`tests/StackSizeTests.cpp`); before a release run the tests once under `ulimit -s 1024` to catch what Linux hides. New features get tests that check what the feature really does to the sound (levels, timing to the sample, frequencies), not just that it runs.
  - Test tones: `tests/PluginTestUtils.h` (`testkit::sine`, `saw`, `useSample`, `keyFor`): a slot set to SAMPLE playing a generated tone comes out sample for sample at PITCH 0, so effect tests measure exactly. `tests/TestUtils.h`: `levelAt`, `rms`, `frequencyOf`, `Spectrum`...
  - Hits in tests go in as MIDI (at a sample position), **not** through `getKeyboardState().noteOn`: JUCE spreads the keyboard's presses across the block by the clock, so two presses can land at different samples on different runs (it made "identical" runs differ in 0.1.0).
- **pluginval:** `xvfb-run -a <pluginval> --strictness-level 10 --validate-in-process --validate "build/ParagonX1_artefacts/Release/VST3/Paragon PR7-I.vst3"`. Check its exit code. CI runs it too. Linux pluginval sometimes crashes in its own host at shutdown, after SUCCESS (about 1 run in 6-16 here: a segfault in `juce::RunLoop::Impl::registerEventHandler` on its message thread). Since 0.60.0 CI gives that crash after SUCCESS up to 3 more tries (it failed both of the old two); a crash before SUCCESS or any other failure fails the build. If CI still fails on it, reproduce under gdb before calling it that.
- **Screenshots** for checking the UI, via `xvfb-run -a build/tests/ParagonX1Tests "[snapshot]"` with environment variables:
  - `PARAGON_SNAPSHOT` (output path), `PARAGON_SNAPSHOT_WIDTH` (default 1600; 3200 for 2x), `PARAGON_SNAPSHOT_FINISH=0|1|2` (the plugin's finish: pearl white, pearl black, pearl white + neon).
  - DRUMS (the default page, or `PARAGON_SNAPSHOT_PAGE=drums`; the KIT VIEW unless a DRUM* variable below asks for the DRUM view; `PARAGON_SNAPSHOT_LAYERS=1`: the snare's LAYERS tab (one aligned, one multi-mic); `PARAGON_SNAPSHOT_KIT=1`: kick, snare, crash and china just hit, glowing, the ride's bell chosen; `PARAGON_SNAPSHOT_ARTIC=1`: the hi-hat's articulations, OPEN 3 off, the pedal half open; `PARAGON_SNAPSHOT_VELOCITY=1`: the snare's VELOCITY curve and gate; `PARAGON_SNAPSHOT_INSTRUMENT=1`: the snare from a folder of 16 hits, its layers): `PARAGON_SNAPSHOT_DRUM=n` chooses slot n, `PARAGON_SNAPSHOT_DRUMS=1` plays kick, snare and closed hat first, their rows lit; `=2` a file of nine kicks, the 4th chosen; `PARAGON_SNAPSHOT_DRUMSEQ=1` the SEQUENCER view, a beat playing with the song, the snares chosen; `PARAGON_SNAPSHOT_DRUMEDIT=1` the clap with an envelope, an anchor, GAIN and LP, `=2` with `DRUMS=2` a fade drawn on the chosen kick; `PARAGON_SNAPSHOT_DRUMCHOSEN=1` the hats and cymbal chosen together, the tom's playhead.
  - `PARAGON_SNAPSHOT_MACHINE=1`: THE DRUM MACHINE playing Room Beat on its own clock, the snare chosen; `=2` drums 19-36 with REC armed; `PARAGON_SNAPSHOT_MACHINEFINISH=0|1|2` in that finish.
  - `PARAGON_SNAPSHOT_PROGRESS=1`: the progress box over the page (a render import 62% done, with CANCEL).
  - `PARAGON_SNAPSHOT_PAGE=fx` (the FX page; `PARAGON_SNAPSHOT_RACK=1`: everything at work on "Room Beat" playing with the song, kick and snare on bus A, the sends too; `PARAGON_SNAPSHOT_FX=eq|dominion|comp|limiter|sends|delay|reverb|modfx|mod|mod2|lofi|inserts|insdrive|insglitch|insmod|inslofi`: that view, `PARAGON_SNAPSHOT_BUS=1-4` the inserts' bus; `PARAGON_SNAPSHOT_MATCH=1` with `FX=eq`: EQ Match open).
  - `PARAGON_SNAPSHOT_PAGE=mixer`: THE MIXER with a generated multi-mic SFZ kit, a hit just played, SNARE TOP chosen (`PARAGON_SNAPSHOT_DGKIT=<kit file>` with `PARAGON_SNAPSHOT_LAYER=<audio>`: a DrumGizmo kit, the kick layered, its channel shown; `PARAGON_SNAPSHOT_MICSIM=1`: instead the Djent Kit through SIMULATED MICS, their panel open, `=2` its strips; `PARAGON_SNAPSHOT_MIXERBUS=1`: buses and sends set, the snare top's SENDS tab; `=2`: scrolled to the buses, BUS 1 chosen; `PARAGON_SNAPSHOT_MIXERFX=1`: the snare top's effects, the compressor chosen, `=2` the EQ; `PARAGON_SNAPSHOT_MIXERROUTE=1`: ROUTE MICS on the snare, `=2` the user channels).
  - `PARAGON_SNAPSHOT_PAGE=settings PARAGON_SNAPSHOT_MIDIIN=1|2|3`: MIDI IN / E-DRUMS (the snare's note with 3 CC zones and the monitor; the VELOCITY tab; the HI-HAT tab calibrating).
  - `PARAGON_SNAPSHOT_PAGE=presets|settings`, `PARAGON_SNAPSHOT_PRESET=name` (a factory preset), `PARAGON_SNAPSHOT_PARAMS=id=value,...`, `PARAGON_SNAPSHOT_NOTES=1` (the kick, snare and hat keys held), `PARAGON_SNAPSHOT_MIDI=1` (demo MIDI links, a knob learning; with `PAGE=settings` the links list).
  - Menus open in their own window, so the editor snapshot can't show one: `PARAGON_SNAPSHOT=menu.png ... "[snapshot-menu]"` draws the drum models' menu as the plugin's look-and-feel does.
  - Render, look, crop and zoom before claiming a visual change is right.

## Releases

- Bump `project(ParagonX1 VERSION x.y.z)` in CMakeLists.txt and add a matching `## x.y.z` section at the top of CHANGELOG.md, then push.
- CI builds Windows and Linux (macOS left out since 0.46.0, the owner's choice: GitHub stopped giving its job a machine; the matrix entry is kept commented out in `build.yml`) and publishes a GitHub Release with the two zips, using that changelog section as the notes. It publishes from a push to **any** branch whose version isn't released yet.
- After pushing, check the release published (both zips) about 15-20 minutes later, **and that its `target_commitish` is your commit**. If the version was already taken, CI skips silently and the release you'd link is someone else's build. Send the owner the link.
- **One chat per plugin at a time.** In October 2026 two chats released PR7-G and PR7-V under the same version numbers: one chat's releases were skipped, and features (and a UI fix) went missing from the builds the owner installed. Before choosing a version, check the repo's tags *and* other branches for unreleased work; if another branch is ahead, merge it first so every release contains everything.
- The owner installs from GitHub Releases on **Windows**.

## How the owner likes to work

- **Explain the why**, specifically: what was wrong, what changed, and the trade-offs. Plain language; the owner is a musician, not a DSP engineer. Offer choices with a recommendation.
- **Be honest about limits:**
  - Say when something is our design rather than a copy ("modelled on", "inspired by").
  - Say when a detail is a guess.
  - Say what can't be checked here (audio by ear).
  - Ask for source material, such as manuals, example groove files or chapters, rather than invent specifics.
- **No trademarked names or branding** on our models. "Inspired by" in descriptions is fine.
- **Show it:** send a rendered screenshot of any visual change.
- **"merge it"** means: open a PR from the working branch to main and merge it, then reset the working branch to the new main.
- Commit messages explain what and why.
