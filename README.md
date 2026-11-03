# Paragon PR7-D

[![Build](https://github.com/ARYEZ/Paragon-PR7-D/actions/workflows/build.yml/badge.svg)](https://github.com/ARYEZ/Paragon-PR7-D/actions/workflows/build.yml)
[![Latest release](https://img.shields.io/github/v/release/ARYEZ/Paragon-PR7-D)](https://github.com/ARYEZ/Paragon-PR7-D/releases/latest)

The drum instrument of the Paragon PR7 family: a VST3 instrument with a kit of drum synths and your own samples, a
drum sequencer, and effects. Built with [JUCE](https://juce.com).

Paragon PR7-D starts from [Paragon PR7-K](https://github.com/ARYEZ/Paragon-PR7-K) 0.24.0, the family's synthesizer,
whose drum section it takes further as an instrument of its own. It shares the family's look, preset browser and
undo. Its siblings are [PR7-G](https://github.com/ARYEZ/Paragon-PR7-G) (guitar),
[PR7-B](https://github.com/ARYEZ/Paragon-PR7-B) (bass), [PR7-V](https://github.com/ARYEZ/Paragon-PR7-V) (vocal),
[PR7-C](https://github.com/ARYEZ/Paragon-PR7-C), [PR7-E](https://github.com/ARYEZ/Paragon-PR7-E) (utility) and PR7-K
(synth). It installs alongside all of them and keeps its own presets.

## Status

PR7-D is built in stages, each one released:

| Stage | What | Now |
|---|---|---|
| **1** | Its own plugin: PR7-K's kit, drum sequencer, sample editor and effects, without the synth | **Works (0.1.0)** |
| **2** | The bigger kit: 20 drums, with ride, ride bell, second crash, china, splash, half-open hat, tambourine, shaker | **Works (0.2.0)** |
| **3** | Note maps: Root, a groove-library layout, General MIDI, and your own | **Works (0.2.0)** |
| **4** | Multiple outputs: 16 stereo outputs; each drum, insert bus and send return chooses one | **Works (0.3.0)** |
| **5** | The groove player: browse, filter, preview and drag MIDI grooves from your own folders | **Works (0.16.0)**: browser in columns with back / forward, filter columns with your packs' labels, active-filter chips, search suggestions, preview, drag out, into the sequencer, tap to find, groove parts, User MIDI; grooves kept in `midiDB` database files too (0.16.0: rebuilt to work like the reference's groove tab) |
| **6** | The song track: grooves laid out as a song, played with your DAW | **Works (0.12.0)**: blocks from grooves or MIDI files, move, lengthen, duplicate, mute, song parts, change tempo, PLAY, EXPORT MIDI; editing inside a block (paste or remove drums, quantize, velocity, split, merge, EDIT BLOCK); the SONG CREATOR; tempo and time signature changes; IMPORT MIDI FILE; RECORD; BOUNCE to audio; FOLLOW DAW |
| **7** | The kit view: a drawn kit (our own 3D kit, black with glowing rings) to click and play | **Works (0.14.0)**: click a drum to choose and hear it; each piece glows as it plays |
| **9** | The sample engine: velocity layers, round-robins, mics, bleed, a mixer | **Works (0.20.0)**: drums from SFZ kits or folders of your own hits, with velocity layers, round-robins, hit variation, layer and voice limits; multi-mic kits through the MIXER's mic channels, with bleed per drum, time offset, release and outputs; 8 buses and two sends per channel or bus (into a bus, the FX page's insert buses, DELAY, REVERB) (0.21.0); four effect slots per channel and bus: EQ, COMPRESSOR, GATE, TRANSIENT, DRIVE, LO-FI, MOD (0.22.0); ROUTE MICS: each drum's mics to any channel, a new user channel or none, up to 8 user channels (0.23.0); simulated mics and a room for the synth drums (0.26.0) |
| **10** | The drum machine: the kit as a drum machine's face, over the whole window | **Works (0.38.0)**: a column per drum (TUNE, DECAY, LEVEL, its pad) in two banks of 18, the tempo screen, PLAY on its own clock at TEMPO, REC into the pattern, TAP, the chosen drum's 16 steps, patterns A-D with COPY, CLEAR, FILL |
| **11** | Drum buses by default: the kick's, snare's and toms' channels grouped | **Works (0.39.0)**: KICK, SNARE and TOMS buses in a new mixer (renamable), channels added later following their drum, older projects unchanged |
| **8** | Articulations: each drum of the kit a piece played several ways | **Works (0.18.0)**: the articulation list (ALL, ON, level), the ride's edge, five hat openness steps, stand-ins for articulations switched off, the hi-hat pedal (CC 4) and its calibration, cymbals grabbed by note-off or aftertouch; a velocity curve and gate per drum; MIDI IN / E-DRUMS (incoming curve, channel, CC zones, LEARN, presets) and the MIDI monitor |

## Using it

- **DRUMS** (the page it opens on):
  - **KIT VIEW** (the right-hand panel's first tab): the kit from the drummer's seat, our own 3D kit in the PR7
    colours (black drums lit by glowing rings, pink on the hi-hat's side through red to orange on the ride's). **Click a
    drum to choose it and hear it** (SHIFT / CTRL choose several, as on the rows): the snare's head, hoop (rimshot) and
    shell (sidestick); the ride's bow, bell and edge; the hi-hat and its pedal; toms, cymbals, kick and a tambourine.
    Each piece glows as it plays, from your DAW, the sequencer, a groove or the song. The clap and shaker are on the
    rows only.
  - **MACHINE** (0.38.0, the tab left of KIT VIEW): THE DRUM MACHINE, the kit as an "Advanced Drum Computer" over the
    whole window (the drum list, keyboard and OUTPUT give way to it; its VIEW keys go back). Laid out like the early-80s
    drum machines, in PR7-K's pearl black: **a column per drum** (TUNE = its pitch, DECAY, a LEVEL fader, its pad:
    click it to choose and hear the drum; it glows as it plays), 18 at a time (**BANK** 1-18 / 19-36). **PLAY** runs the
    beat from its first step at **TEMPO** with no DAW needed (while the DAW plays, the beat follows the DAW); **STOP**;
    **REC**: while it runs, what you play (pads, the DAW, a controller) goes into the pattern on its nearest step (one undo
    step per take); **TAP** the tempo; **SWING** (the sequencer's SHUFFLE, shown as 50-75%); **MASTER** (OUTPUT) with its
    meter. The **steps** are the chosen drum's in the pattern shown, 16 at a time (< > for longer patterns; they follow the
    beat while it plays). **PATTERN** A-D (**COPY** then a letter copies the pattern there), **SONG** (lit: the beat follows
    the song; off: the patterns play from keys), **CLEAR** (the chosen drum's notes; SHIFT: all), **FILL** (every step
    shown), **MUTE** / **SOLO** the chosen drum. **Right-click a pad** (or its name) to put any drum on it (0.39.0; it
    swaps with that drum's pad; saved with the project). The screen: pattern, time signature, bar and step, the preset, the tempo
    (DAW when the DAW leads), swing.
  - **ARTICULATIONS** (0.17.0, over the kit view's top left): the chosen drum's piece and the ways it's played (the
    snare's center, rimshot and sidestick; the hi-hat closed, open 1 to 4 and pedal; the ride's bow, bell and edge).
    Click a name to choose it and hear it, **ALL** to choose every one (the DRUM page's settings then go to all of
    them); the dot switches one **off** (its notes then play the nearest one that's on, named beside it: a lighter kit,
    or old presets as they were); drag its **level**. The hi-hat's **PEDAL (CC 4)**: a controller's pedal opens the
    hat step by step on its pedal note (note 7 in Groove Library, or any note set to HI-HAT (PEDAL) in a Custom map);
    drag the marks to set where each step begins. Cymbals: **MUTE TAIL**, grab it to stop it ringing, by letting the
    key go (NOTE OFF) or by pressing a pad harder (AFTERTOUCH).
  - **The kit:** 29 drums: kick, rim, snare, clap, a second snare, three toms, closed, pedal and open hat, cymbal,
    ride, ride bell, second crash, china, splash, half-open hat, tambourine, shaker, (0.17.0) the ride's edge and
    two more hi-hat openness steps, and (0.33.0) a fourth tom, the hi-hat's shoulder and four cymbal chokes. Each is one of our own drum synths
    (kick, snare, clap, hats, tom, rim, cowbell, cymbal, ride, bell, tambourine, shaker) or a **SAMPLE** you load (drop
    an audio file on its row).
  - **LOAD...** (the DRUM view; 0.19.0: THE SAMPLE ENGINE): one audio file; a folder of hits (velocity layers and
    round-robins, sorted by loudness or by numbered names); an SFZ instrument; or a whole SFZ kit onto every drum.
    With an instrument: its layers drawn by velocity, RANDOMIZE, ADJACENT layers, VEL > VOL, LAYER LIMIT, VOICES.
    Presets and projects remember where the files are and load them again.
- **ACOUSTIC KICK, SNARE, TOM** (0.24.0): modelled drums computed from the physics, no samples: the head's modes,
  strike POSITION, what strikes it (BEATER / STICK), how hard (brighter, a pitch glide), muffling (DECAY), the kick's
  CLICK and FRONT HEAD, the snare's WIRES, the shell, VARIATION between hits. Factory preset A06 Djent Kit.
- **ACOUSTIC CRASH, RIDE, CHINA, SPLASH, STACK, HI-HAT, HAT PEDAL** (0.25.0): modelled cymbals: inharmonic plate modes,
  the WASH blooming after the strike (its highs dying first), the bell; STICK, PING (the china's TRASH), BELL > EDGE,
  WASH, the hi-hat's OPEN (its plates rattling), the pedal's chick.
- **MIXER** (0.20.0): a multi-mic kit's mic channels (kick in / out, snare top / bottom, hi-hat, toms, cymbals,
  overheads, room, ambience, other): BLEED, PAN, fader and meter, M / S / polarity, OUTPUT; click a channel's name for
  its PROPERTIES (close-mic audio and the bleed from each drum: on, level, polarity; TIME OFFSET; RELEASE). Synth drums
  and single samples don't use it. 0.21.0: 8 BUSES (fader, pan, M / S / polarity, OUTPUT, sends; a bus feeds only a
  later one); each channel's OUTPUT can be a bus or an FX bus (the FX page's insert buses A-D); two SENDS per channel
  or bus (to a bus, an FX bus, DELAY or REVERB; POST / PRE FADER; a mic's DIRECT and BLEED sound with their own on,
  level and pan, LINK); MICS / BUSES; MENU...: channel mute before or after the sends, STEREO REVERSE, STEREO or
  MULTICHANNEL OUTPUTS. 0.22.0: four EFFECT slots per channel and bus (before the pan and fader): EQ (low cut, low
  shelf, two bells, high shelf, high cut), COMPRESSOR (VCA / OPTO / FET, side-chain high-pass, MIX), GATE (threshold,
  range, attack, hold, release, side-chain filters), TRANSIENT (attack, sustain), DRIVE, LO-FI, MOD; each with ON and
  OUTPUT, copy / paste; PROPERTIES > EFFECTS for their knobs; a PRE EFFECTS send mode.
  0.23.0: ROUTE MICS... (route instrument microphones): pick a drum, see its mics (close first, then bleed), send
  each to its own channel (AUTO), another channel, a NEW CHANNEL of its own, or none; up to 8 user channels, shown once
  used, each a full channel (fader, effects, sends, bus, output); right-click a user channel's name to rename it.
  0.26.0: SIM MICS... (simulated mics and room): the synth drums and single samples heard as a recorded kit, through
  simulated close mics, overheads, a room pair and a far ambience pair, each in its own channel, the other drums
  bleeding into each close mic; a ROOM (Small Room, Studio, Live Room, Big Hall) with DECAY and TONE; which drums go
  through them. The Djent Kit uses them.
- **DRUMGIZMO KITS** (0.27.0): LOAD... > A DRUMGIZMO KIT... loads a free multi-mic kit in the open DrumGizmo format
  (drumgizmo.org; GET FREE KITS... opens its page) onto every drum, its mics through the MIXER with their real bleed,
  STREAMED (0.28.0: every hit at full length, read from disk as it plays; the CrocellKit in 377 MB of memory), LIGHT
  (about 1 GB) or FULL IN MEMORY. Factory preset A07 Crocell Metal streams the CrocellKit (CC-BY 4.0, credited in ABOUT).
- **SAMPLE PACKS** (0.33.0): LOAD... > A SAMPLE PACK... reads a multi-mic library's folder by its file names (piece,
  articulation, mic, then _V<layer>_RR<round>, as Just Add Drums' libraries name them) and puts every sound on its drum
  and every mic on its MIXER channel (its extra mics on USER channels), streamed. Six more drums for kits that have
  them: TOM 4, the hi-hat's SHOULDER, and crash 1 / crash 2 / china / splash CHOKES (a choke stops its cymbal).
- **LAYER** (0.29.0): any drum plays a sample (or folder of takes) of yours with every hit, LOAD... > A LAYER WITH THIS
  DRUM...; LAYER level and LAYER OUT; through the mixer it gets its own USER channel ("KICK LAYER") and the drum's close
  mics come down 10 dB, so the sample leads and the real drum's room and overheads stay.
  - **VELOCITY** (0.18.0, beside ENVELOPE on the DRUM view): the chosen drum's velocity curve (drag the end points to
    limit, double-click to bend, SOFT, GATE) and its VELOCITY GATE (the range of hits that play it).
  - **MIDI IN / E-DRUMS** (0.18.0, SETTINGS): click a note to edit it: its drum and LEARN, CC ZONES for pads that sense
    where they're hit, one VELOCITY curve for everything coming in, the HI-HAT pedal's steps and CALIBRATE; the MIDI
    CHANNEL; presets of all of it for each kit you use; and the MIDI MONITOR (what came in and what it played).
  - **Which notes play them:** the MAP in SETTINGS. **Root** puts the 29 on the keys from **ROOT** (C2 at first);
    **Groove Library** and **General MIDI** follow those layouts, so grooves made for them play as written; **Custom**
    is your own, note by note.
  - **OUTPUT** sends a drum to one of the plugin's 16 stereo outputs (MAIN, OUT 2 to 16), to mix it on its own
    channel in your DAW (switch the output on there; until then it plays in MAIN). Insert buses and the DELAY and
    REVERB returns have their own OUTPUT. The routing is kept in your project, not in presets.
  - Click a row to choose and hear it; CTRL / SHIFT choose several. **MUTE**, **SOLO**, **CHOKE** groups (the
    closed hat stops the open one).
  - **The chosen drum:** PITCH, DECAY, TONE, SNAP, VOLUME, PAN, GAIN; where it goes (straight to the mix, or an
    insert bus; its DELAY and REVERB sends); MONO / POLY, DELAY (with an anchor for the sequencer), LP, HP; and a
    drawn **ENVELOPE** across its hit. A sample file shows its hits: click one to play just it, or drag S and E.
  - **KITS** and **SEQUENCES** save and load kits and beats (PR7-K's `.pr7kit` and `.pr7seq` files load here too).
- **GROOVES:** your own MIDI grooves. ADD FOLDER... links a folder (and its subfolders); choose a folder on the left
  to list its grooves, with their time signature, bars, tempo, power hand (what the riding hand plays) and intensity.
  Click one to play it through the kit (double-click: once), at your song's tempo or its own, 1/2x to 2x, its velocity
  scaled; drag it onto a DAW track or to a folder. Each folder's FILES' MAP says which drum each note is. Filter
  columns (TYPE, FEEL, TIME, POWER HAND, TEMPO: click to choose, right-click to exclude) narrow the list; TO SEQUENCER
  A-D makes the chosen groove a drum sequencer pattern. TAP TO FIND...: tap a rhythm on six pads (or your controller)
  over a two-bar loop with a metronome, and the closest grooves come first (MATCH); drop a MIDI file on the page, or
  right-click a groove (FIND SIMILAR), to search with theirs (0.31.0: retuned on two real groove packs: taps compared
  where you played them, a sketch of the main hits allowed, 40 ms of latency forgiven). RIFF MATCH (0.32.0): drop a
  recording of your guitar, bass or keys (or a MIDI file of one): the grooves whose kick locks to it come first, and
  you hear it with every groove you preview, in time. GROOVE PARTS: tick the parts of the chosen groove (kick,
  snare, hi-hat...) to hear, drag or LAYER ONTO a pattern just those, over the pattern's other drums.
- **SONG** (beside the on-screen keyboard: KEYS | SONG): grooves as blocks in a song. Drag a groove from the list (or a
  MIDI file) onto it, or right-click a groove: ADD TO SONG. Drag blocks to move them, their right edge to repeat the
  groove; right-click for DUPLICATE, REMOVE, MUTE, SONG PART, CHANGE TEMPO, COPY / PASTE (whole blocks, or some drums
  onto a block), REMOVE DRUMS, QUANTIZE, VELOCITY, SPLIT HERE and MERGE WITH NEXT. EDIT BLOCK... (or double-click a
  block): its notes drum by drum, to click in, drag louder or softer, remove, quantize, copy, paste or move to another
  drum. A groove's right-click COPY FOR SONG copies it (or its ticked parts) to paste onto blocks. CREATOR... (or a
  groove's USE WITH SONG CREATOR): the grooves of its song folder in columns by song part, and SONG STRUCTURES (ours,
  and yours saved from the song track): MAKE SONG builds a whole drum track from them. TEMPO and TIME (beside BLOCKS):
  the song's tempo changes (PLAY and EXPORT MIDI use them; while your DAW plays, its tempo leads) and time signature
  changes. TRACK... > IMPORT MIDI FILE...: a whole song's MIDI, cut into blocks at its markers. REC: what you play while
  the song plays goes into it (on top of its blocks). TRACK... > BOUNCE TO AUDIO...: the song as WAV files (one, or one
  per output), lined up with bar 1. FOLLOW DAW off: the song plays only with PLAY (for when its MIDI is on a DAW track). It plays with your DAW (bar 1 is the
  DAW's bar 1), or with PLAY when the DAW is stopped. EXPORT MIDI: the whole song as one file. Saved with the project.
- **SEQUENCER** (beside DRUM): four patterns, **A** to **D**, drawn on a grid level with the kit's rows. Each note
  has its own velocity, pan, pitch, gate, roll and reverse. **SONG** plays with your DAW; **KEY** plays a pattern while
  one of the four keys under the kit is held (with the Root map). **EXPORT MIDI**: click to save, or drag onto a track.
- **FX:** **INSERTS** (buses A to D: DRIVE, GLITCH, MOD, LO-FI, on the drums you route to them), **SENDS** (DELAY and
  REVERB, fed by each drum's sends), **MOD / LO-FI** on the whole kit, and **MASTER** (ECHELON EQ, DOMINION,
  CONSTRICTOR, the LIMITER).
- **The keyboard** along the bottom plays the kit with the mouse; the keys that play a drum are tinted. Your DAW's MIDI
  plays it too. Notes the map doesn't use do nothing.
- **Presets:** SAVE in the header; **<** and **>** step through them; a `*` means changed since loading. The
  **PRESETS** tab browses, files (banks A-E), tags and favourites them. **OUTPUT** stays put when you change presets.
- **MIDI learn:** right-click a knob. The links are listed in SETTINGS.
- **Undo / redo:** the header arrows, or Ctrl+Z / Ctrl+Shift+Z (Cmd on a Mac).

## Download

Get the latest version from the **[Releases](https://github.com/ARYEZ/Paragon-PR7-D/releases/latest)** page: download
the zip for your computer. (Builds of every push also appear under **Actions** → a run → **Artifacts**.)

Unzip it and copy `Paragon PR7-D.vst3` to your VST3 folder:

| OS | VST3 folder |
|---|---|
| Windows | `C:\Program Files\Common Files\VST3` |
| macOS | `~/Library/Audio/Plug-Ins/VST3` |
| Linux | `~/.vst3` |

**macOS:** the build isn't code-signed yet, so macOS will block it. After copying, run
`xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Paragon PR7-D.vst3"` once.

Then rescan plugins in your DAW: PR7-D shows up as an instrument (a drum instrument, where your DAW sorts them).

## Making a release

1. Set the new version in `CMakeLists.txt` (`project(ParagonX1 VERSION x.y.z)`).
2. Add a `## x.y.z` section to `CHANGELOG.md`.
3. Commit and push.

When a push carries a version that hasn't been released yet, GitHub Actions builds all three
platforms, tags the commit `vx.y.z` and publishes the release with notes from the changelog.

## Building from source

Requirements: CMake 3.24+, a C++20 compiler (Visual Studio 2022, Xcode 15+, or GCC 11+/Clang 14+), and Git.
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

Paragon PR7-D is free software under the [GNU Affero General Public License v3](LICENSE.md),
which is required by JUCE's open-source licence.

It uses:
- [JUCE](https://github.com/juce-framework/JUCE): AGPLv3
- The Inter typeface (in `resources/fonts`) by Rasmus Andersson, under the SIL Open Font License

PR7-D's drums, sequencer and effects are our own designs and code. Its groove player is inspired by the groove
browsers of commercial drum software, and reads only your own MIDI files from your computer: no third-party grooves,
sounds or tags ship with it.
