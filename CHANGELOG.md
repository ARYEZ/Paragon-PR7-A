# Changelog

Each version's notes appear on its GitHub Release page.

## 0.4.0

**The new GUITAR page.** The guitar fills the window in the same Tron look as PR7-D's drums, laid out the way your guitar instrument's main page works, mirrored: headstock on the right, heaviest string on top. Every note lights up on its string and fret as you play.

- **The guitar:** our own 3D model, rendered the same way as PR7-D's drum kit. The body is traced from your RG photo (no name or logo on it), gloss black with a neon edge that runs pink on top to orange underneath. The fretboard has glowing frets and inlays, and the strings glow in their colours. Only the horns and the top of the body show, as on your guitar instrument's page.
- **On the neck:** sustained notes are solid dots and palm mutes are rings, and each string lights up from the note to the bridge. The hand's reach is a lit window with a bracket underneath (HAND 1–4 · AUTO). A forced string glows along its length. **Drag the capo** along the neck from the nut, and double-click it to take it off.
- **New controls** (each is our own version of what your guitar instrument has):
  - **CAPO:** nothing plays below it, and its fret rings like an open string.
  - **HAND SPAN:** how many frets the hand reaches. Auto keeps 4, or 5 in polyphonic mode.
  - **HAND RESET:** how long the hand waits, with nothing held, before going back down the neck. It was a fixed second before.
  - **MONO:** one note at a time.
  - **ECONOMY picking:** alternate picking, except that crossing to the next string in the direction the pick is already moving keeps the same stroke.
  - **PERFORMANCE:** Rhythm, Lead (mono) or Chords (polyphonic) sets the fretting controls in one go. The menu shows Custom once you change them.
  - **POLY WINDOW:** notes played that close together are fretted as one chord. It delays the guitar by the same amount, and your DAW makes up for that, so notes still land on time.
  - **BITE:** brighter, with less low-mid.
  - **ATTACK:** the first few milliseconds of each note louder, more so the harder you play. It boosts the take's own pick sound; it isn't a separate pick noise.
- **NOW PLAYING:** shows what the last note did: sustain, palm mute or power chord, how muted it was, down or up stroke, which take, the note, its velocity, string and fret.
- **DOUBLE TRACKING** has its place on the page but comes in a later release.
- **The render lists and imports** moved to their own **RENDERS** tab.
- **Presets** saved before 0.4.0 load with the new controls at their defaults, so they sound the same.
- **What can't be checked here:** how ATTACK and BITE sound on your real renders, and how the page feels in use. Everything was tested with made-up tones, plus a check that every coloured pixel of the page stays in the family's colours.

## 0.3.0

**Palm mute.** Import your palm-mute renders and play them the way your own map works: C-1 at velocities 1-59 plays palm mute, 60-119 plays sustain.

- **Importing palm mutes.** RENDERS on the GUITAR page now has **SUSTAIN** and **PALM MUTE**. Choose PALM MUTE, then drop each string's palm-mute WAV on its row. Each string's list has 2,000 notes: every fret, Jupiter's five degrees of muting, and eight takes each of a down and an up stroke. Copies of the same take are left out (Jupiter has 6 different up strokes), and the row says how many.
- **Velocity sets how muted the note is**, as in your guitar instrument. Velocity picks one of the five degrees, from very muted to half muted.
- **KEY MAP** (under THE GUITAR):
  - **Split** (the default, your own map): C-1 plays palm mute at 1-59 and sustain at 60-119. Each zone is spread over its whole range, the way your SCALING setting does, so 1-59 reaches all five degrees of muting and 60-119 all three sustain dynamics. Velocities 120-127 play the hardest sustain, because rake and pinch harmonics aren't rendered yet. C#-1 does the same as power chords.
  - **Factory:** C-1 sustain, C#-1 palm mute, D#-1 and E-1 power chords.
- **Power chords** (C#-1 in your map) play the root, the fifth and the octave as three strings fretted together, so D5 is a barre. This is our stand-in until power chords are rendered.
- **Palm mutes in turn.** Each stroke's palm-mute takes play in turn, just as sustain's do: down 1, up 1, through to down 8, up 8.
- **The fretboard** shows palm-muted notes as rings and sustained notes as solid dots.
- **Until a string's palm mutes are imported,** its palm-muted notes play the softest sustain takes instead.
- **SETTINGS** lists the keyswitches for both maps. SAVE ALL LISTS saves the palm-mute lists too.
- **What can't be checked here:** how your real palm mutes sound in PR7-I. Everything was tested with made-up tones; the first listen is yours. The Tron-look GUITAR page comes next, after you approve a mock-up.

## 0.2.2

**0.2.0's sound back: the takes in turn again.** You heard 0.2.1 as all machine gun and 0.2.0 as far better. You were right, and this explains why and puts it back.

- **Why 0.2.1 machine-gunned.** Your guitar instrument has two takes of each stroke. 0.2.0 plays them in turn: down 1, up 1, down 2, up 2. That's the widest spacing two takes allow, so the same recording comes back no sooner than every fourth note. 0.2.1 picked them at random instead, and a coin toss between two often gives the same take every other note. Its rule against repeats compared each note with the last note on the string, which with alternate picking is always the other stroke, so it never did anything.
- **The takes in turn again**, exactly as 0.2.0 played them. A test now holds that order, so it can't change unnoticed.
- **A released note rings out under the next one again**, as in 0.2.0. 0.2.1 cut it short when the string was picked again.
- **VARIATION is off (0%) by default**, which gives you exactly 0.2.0's sound. Projects and presets saved with 0.2.1 load with it off too, because 0.2.1's 50% was never something you chose.
- **VARIATION is now an experiment to judge by ear.** Turned up, it now and then swaps the in-turn take for a neighbouring dynamic's take (turned to the note's level) or a neighbouring fret's take (a semitone away, played at the note's pitch). It never repeats the take that stroke played last on that string. It also makes each note a touch different in timing, level and tuning. If it doesn't help, leave it at 0.
- Still in from 0.2.1: imports leave out takes that are copies of other takes.

## 0.2.1

**Ready for your renders: more variety from the two takes per stroke, and imports that leave out copies.** Your guitar instrument turned out to have two different sustain takes per stroke (the round-robin check you rendered), so PR7-I now makes the most of them.

- **No pattern on repeated notes.** Each stroke picks one of its takes at random, never the one that just played on that string. In 0.2.0 they went round in a fixed order.
- **Borrowing.** A note can also use its neighbouring dynamic's takes, turned to its own level, and the neighbouring frets' takes, shifted a semitone to its pitch. This is the same idea as your instrument's own "anti-repetition". A chugged note now draws on up to ten recordings per stroke instead of two, with its own takes still the most likely.
- **VARIATION** (new, on SOUND, 50% by default): each note comes out a touch different, as no two picks are. Up to 3 ms later into its take, up to half a dB louder or quieter, a few cents sharp or flat. Fully down gives you the takes exactly as recorded, with no borrowing.
- **Re-picking a string stops it completely.** In 0.2.0, a note released and picked again straight away could overlap its own fade-out, which a single string can't do.
- **Imports leave out copies.** When two takes of the same note, dynamic and stroke are the same recording (an instrument with fewer takes than the list asks for), the copy isn't saved, and the row says how many were left out.
- **What can't be checked here:** how the borrowed takes blend in by ear. If anything sounds off, turn VARIATION down.

## 0.2.0

**PR7-I plays a guitar now: your renders from your guitar instrument, on the strings and frets a player would use.** The drums are gone. This is the first half of the playing engine (sustain); palm mute and the Tron-look guitar page come in 0.3.0.

- **Render lists.** On the new GUITAR page, each of the seven strings has a MIDI file to render through your guitar instrument. Click MIDI to save it, drag it straight onto a track, or use SAVE ALL LISTS. Each file holds every fret from open to the 24th, at three dynamics, with two takes each of a down and an up stroke: 300 notes and about 28 minutes per string. The files set your instrument's own keyswitches themselves (force string, sustain, picking direction, legato off), so you just load one and bounce it.
- **Importing a render.** Drop the bounced WAV on its string's row (or click IMPORT...). PR7-I finds where your DAW actually started the notes, cuts every take at its pick, checks each one's pitch, and saves them in the guitar's folder.
  - It refuses a file that's clearly wrong: another string's render, one that stopped early, or silence. In those cases nothing is saved, and the row says what's wrong.
  - The guitar's folder can be anywhere (CHOOSE FOLDER...). The takes stay on your computer; none ship with the plugin.
- **The playing engine.**
  - Each note goes on the string and fret a player would choose. A virtual hand stays where you're playing, open strings are used low on the neck, and a string already ringing is avoided. A chord's lowest note moves the hand first, so drop A's D5 is a one-finger barre at the 5th fret.
  - One note per string: a new note on a ringing string stops it, as re-picking does. Releasing a note damps its string over RELEASE, and other strings ring on.
  - Picking alternates down and up, and each stroke cycles its two takes, so the same take never plays twice in a row.
  - Velocity picks the dynamic, and VEL > VOL sets how much it changes the level within that dynamic.
- **Controls:** FRETTING MODE (Natural or Polyphonic), PICKING MODE (Alternate, Down, Up), FORCE STRING, HAND POSITION (Auto or a fret), TRANSPOSE, VEL > VOL, RELEASE, TONE, and the preset's LEVEL.
- **Your instrument's default keyswitches work the same way here:** force string C-2 to F#-2 (G-2: off), hand position A#-2 (by velocity, B-2: Auto), sustain C-1, picking C7 / C#7 / D7. SETTINGS lists them in your note-name style.
- **The fretboard** along the bottom of the window lights each note on its string and fret, shades the hand's reach, and highlights a forced string. Each string has its colour from PR7-D's Tron kit: pink on the low A, through red, to orange on the high E. It's a plain first version; 0.3.0 gives it the full Tron look.
- **Gaps are filled.** Until every string is imported, missing notes are played by the nearest takes, shifted to the right pitch, so you can try it after rendering a single string.
- **Removed:** the drum kit and the DRUMS page, the mixer, and the whole effects rack. PR7-G after PR7-I does the amp and effects, as you do now.
- **New presets:** Init, Tight Rhythm, Chords, Lead Up The Neck, Down Picked. These are ways of playing; the sound itself comes from your renders.
- **What can't be checked here:** how it sounds with your real renders. Everything was tested with made-up tones at the right pitches, so the first listen is yours.

## 0.1.0

**PR7-I's first version: its own plugin, made from PR7-D's code.** This is the starting point for the guitar
instrument, not a guitar yet: until the guitar engine arrives (the next stage), it still plays PR7-D's drum kit.

- **Its own plugin.** It's called Paragon PR7-I, with its own plugin ID, so it installs beside PR7-D and every other
  sibling without replacing any of them. It keeps its presets in its own folder ("Paragon PR7-I") and its own
  settings, so nothing mixes with PR7-D's.
- **Its own logo.** The header reads PR7-I. The I is built from the P's own parts, a thick stem with a bar at the top
  and bottom, so it doesn't read as a 1 next to the slanted 7.
- **The groove player is gone.** The GROOVES tab, with tap-to-find and riff match, belonged to the drums. Five tabs
  are left: DRUMS, MIXER, FX, PRESETS and SETTINGS.
- **The song track is gone**, along with the song creator, the grid editor, edit play style and bounce. The strip
  along the bottom of the window is empty for now. It's kept for the guitar's note view, which will show the string
  and fret each note plays on. The on-screen keyboard stays off, as you asked in PR7-D.
- **Still there until the guitar engine replaces them:** the DRUMS page (kit, kit view, drum sequencer, drum
  machine), the MIXER and the effects. They're the only sound source for now, and they hold the parts the guitar will
  reuse: the sample engine (velocity layers, round-robins, streaming from disk), MIDI handling and velocity curves,
  and IMPORT A RENDER.
- **Use PR7-G for the amp.** PR7-I will put out a clean DI guitar; put PR7-G after it for amps, cabinets and pedals.
