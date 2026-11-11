# Changelog

Each version's notes appear on its GitHub Release page.

## 0.3.4

PR7-I 0.6.4's change.

- When it imports a render, PR7-A leaves out takes that are copies of one it already has. It counted two takes as copies
  when they were **99% alike**; now it takes **99.9%**. On PR7-I's Jupiter imports the thin strings kept far fewer
  different palm mutes than the thick ones, and the reference's most alike *different* takes measured 98.8%, right under
  the old line: some real, different takes were likely being thrown away. Real copies (100% alike) are still left out.
- Import your Rogue renders with this version or later, so they keep every different take.

## 0.3.3

PR7-I 0.6.3's changes, with PR7-A's own body.

- **The full-size guitar is back**, as it was in 0.1: the neck across the whole window, the headless body's long horns
  big beside it.
- **HIDE RACK** (bottom right of the GUITAR page): slides the rack away so the whole guitar shows; **SHOW RACK** brings it
  back when you want to change a setting. The button sits where the rack never covers it.
- PR7-A **remembers the rack hidden or shown with your project**. Presets don't change it, and projects saved before this
  version open with the rack shown.

## 0.3.2

**As PR7-I 0.6.2: new palm-mute lists ("v2") that capture more different palm mutes, and re-imports that clear old takes.**

- **The v2 lists** play each note's 8 down strokes in a row, then its 8 ups. The first lists alternated down, up, and your guitar instrument only rotates through about 4 palm mutes per stroke when strokes alternate; when a stroke repeats it plays them all. They're named "PR7-A Mute v2 - String 7 (low A)" and so on, about 53 minutes per string, the same settings as before. Renders of the first palm-mute lists won't import correctly: render the v2 lists through Rogue.
- **Re-importing a string now clears its old takes first**, so earlier ones can't stay behind and play as if they were new.
- Sustain lists, the sound and your presets are unchanged.

## 0.3.1

**As PR7-I 0.6.1: the palm-mute rows say what's really there, and a palm mute check for the thin strings.**

- **The rows no longer say "N of 2000 takes".** Each palm-mute list renders 8 takes of every stroke, more than your guitar instrument has, and the importer leaves out exact copies. The rows now say, for example, "494 different takes (the other 1506 notes rendered were copies of these, left out)". Everything imported was loading all along.
- **The palm mute check** (`PR7-A Palm Mute Check.mid`, about 4.3 minutes): five palm-muted notes from the high E down to the low A, 16 downs and 16 ups each, to show how many different palm mutes Rogue has on the thin strings.
- Nothing about the sound changed.

## 0.3.0

**The noises, as PR7-I 0.6.0: release noises, scrapes and fret noise, from your guitar instrument.** Everything works exactly as in PR7-I's new version, with PR7-A's own list names.

- **Releases:** on every note-off of a sustained note, the finger leaving the string at that note's pitch. A touch louder for harder notes; full for notes held up to a second, fading after, gone at 2 seconds; none for palm mutes.
- **Power chords** get their own release, one per chord (each string's own if those aren't imported yet).
- **Scrapes:** now and then an unpitched scrape is mixed in, about 1 release in 8, never twice in a row.
- **Fret noise on F#0**, its recordings in turn, velocity setting how loud.
- **The rack's new NOISES page:** RELEASE NOISE, SCRAPES and FRET NOISE, and a screen of what's imported. They're on by default at their rendered level.
- **RENDERS has a NOISES tab** with a row per noise list (PR7-A's own: "PR7-A Release - String 7 (low A)" and so on). Render them through Rogue the way PR7-I's go through Jupiter, then import each into PR7-A's guitar folder. The importer refuses renders made the wrong way, or dropped on the wrong string's row.
- **What's a guess:** where between 1 and 2 seconds of holding the releases stop. How it sounds next to the original is for your ears.

## 0.2.0

**The GUITAR page rebuilt, as PR7-I 0.5.0: a family rack unit across the top, a smaller guitar.** Everything is the same as PR7-I's new page except the guitar's shape.

- **The rack.** All of the page's controls now sit on one rack unit across the top, built from the same parts as PR7-G's units: the black pearl chassis with the red glow, the PARAGON PR7-A badge, stamped lettering, orange LED screens, big knobs with their values on small screens, and small keys with neon stripes.
- **Two pages**, switched by the PAGE keys under the badge:
  - **PLAYING:** TONE, BITE, ATTACK, TRANSPOSE, VEL > VOL, RELEASE, POLY WINDOW and VARIATION.
  - **FRETTING:** PICKING MODE, FRETTING MODE and PERFORMANCE; FORCE STRING and HAND POSITION; CAPO, HAND SPAN, HAND RESET and MONO (drag up or down to change one, click MONO, double-click for the default).
- **On both pages:** the screen (the key map, what's latched with its keyswitch, and the last note), the KEY MAP and VARIATION keys, and OUTPUT (an LED meter, LEVEL, OUTPUT; the status light goes out while bypassed).
- **NOW PLAYING is gone.** The rack's screen shows the same things.
- **The headless guitar is a third smaller and lower**, re-rendered with the camera pulled back so the grid floor still fills the window. More of the body and the string clamp past the nut now show. It has no headstock, so PR7-I's new headstock and its PARAGON lettering aren't on this one.
- **Nothing about the sound changed.** Presets, projects and imported renders work as before.

## 0.1.2

**Smoother body edges** (as PR7-I 0.4.7). The headless body's outline is one smooth curve now: the long horn sweeps without little corners, the tips round off as a real body's do, and the straight runs are straight.

- **Why it wasn't smooth:** the trace was about 100 short straight lines, with some raggedness from the photo. It's now resampled and gently smoothed when the guitar is built.

## 0.1.1

**A clean join where the body meets the neck** (as PR7-I 0.4.6). The body's glowing edge met the neck at a slant; now the long horn curves straight down onto the neck at the 17th fret, and the edge runs parallel to the fretboard.

- **Why it slanted:** the trace followed the neck in your photo, which leans slightly and has fanned frets. So beside our straight neck, the body's edge ran at a shallow angle over several frets.
- **Now** along the neck, the body follows our own neck's edge, tucked just under the fretboard.

## 0.1.0

**Paragon PR7-A: your second guitar instrument.** Everything works exactly as PR7-I 0.4.4 does: the same page,
controls, engine, render lists and importing. Only the guitar is different, so the two don't look like the same
plugin.

- **The guitar:** a headless 7-string, traced from your photo with no maker's name or logo, in the same Tron look:
  gloss black with a neon edge, its long swept horn reaching up over the high frets, and a string clamp where a
  headstock would be.
- **Its own plugin:** it's called Paragon PR7-A, with its own plugin code, so it installs beside PR7-I. It has its own
  logo ("PR7-A"), presets folder, settings and guitar folder.
- **Its own takes:** the render lists are named "PR7-A ...". Render them through the guitar you want here (your
  other guitar instrument) and import them on the RENDERS page, as in PR7-I.
- **Readable over the guitar:** this body's long horn runs right through the knob row, so every floating control now
  sits on a soft dark backing. The guitar shows through, but its glowing edge can't cut across a label. PR7-I gets the
  same, so the two pages stay identical.
- **What's carried over:** everything up to PR7-I 0.4.4, including palm mutes, round-robins in turn, notes landing
  on time and the darker page.
