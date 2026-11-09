# Changelog

Each version's notes appear on its GitHub Release page.

## 0.6.3

**The guitar back to full size, and a HIDE RACK button.**

- **The full-size guitar is back**, as it was in 0.4: the neck across the whole window, the body's horns big beside it. The
  headstock is off the right edge again at this size (the smaller guitar was what made room for it).
- **HIDE RACK** (bottom right of the GUITAR page): slides the rack away so the whole guitar shows; **SHOW RACK** brings it
  back when you want to change a setting. The button sits where the rack never covers it.
- PR7-I **remembers the rack hidden or shown with your project**. Presets don't change it, and projects saved before this
  version open with the rack shown.

## 0.6.2

**New palm-mute lists ("v2") that get about half as many again different palm mutes from your guitar instrument.**

- **What your checks found:** your guitar instrument has 8 different down-stroke palm mutes and 6 up-strokes for every note, on every string. But it only plays all of them when the same stroke repeats. When down and up strokes take turns, it rotates through just 4 of each. The first palm-mute lists alternated down, up, down, up, so they captured only about 4 different palm mutes per stroke (fewer on the thin strings).
- **The v2 lists** play each note's 8 downs in a row, then its 8 ups, the way your palm mute check did. That should capture about 6 different downs and 6 different ups per note instead of 4. PR7-I then plays them in turn, so fast chugs have more variety than before, and more than your guitar instrument itself uses when alternate-picking.
- **Same length:** about 53 minutes per string, the same keyswitches and settings as before (the factory articulation map, DI, effects off, one guitar). They're named "PR7-I Mute v2 - String 7 (low A)" and so on, so they can't be mixed up with the first ones. **Renders of the first palm-mute lists won't import correctly against the new layout.** Render the v2 lists and import those.
- **Re-importing a string now clears its old takes first.** Before, a re-import only wrote the takes it kept, so earlier ones could stay behind and play as if they were new. Importing the v2 palm mutes replaces the old ones cleanly.
- Sustain lists are unchanged (byte for byte), and so are the sound and your presets.

## 0.6.1

**The palm-mute rows say what's really there, and a check for the thin strings' palm mutes.**

- **"494 of 2000 takes" looked like a failed load, but it wasn't.** Each palm-mute list renders 8 takes of every stroke, more than your guitar instrument has, so that none are missed. The importer leaves out the renders that are exact copies of an earlier take (playing a copy as if it were a different take is what made 0.2.1 sound like a machine gun). The rows now say, for example, "494 different takes (the other 1506 notes rendered were copies of these, left out)". Everything you imported was loading and playing all along.
- **The palm mute check** (`PR7-I Palm Mute Check.mid`, about 4.3 minutes): five palm-muted notes from the high E down to the low A, each played 16 times down and 16 times up. Your imports kept about 4 different palm mutes a stroke on the low A but only about 2 on the high E. Rendered once, this check shows whether your guitar instrument really has fewer on the thin strings, or whether the copy test is merging takes that are only nearly the same: it now says how alike the "copies" are (the original's true repeats are identical: 1.0000).
- Nothing about the sound changed.

## 0.6.0

**The noises: release noises, scrapes and fret noise, from your guitar instrument.** This is the biggest thing your renders were missing next to the original: the small sounds a real guitar makes between notes.

- **Releases.** Every time you let go of a sustained note, PR7-I plays the sound of the finger leaving the string, at that note's pitch. It works the way your noise check measured the original:
  - it starts the instant the key is let go;
  - it's a touch louder for harder notes;
  - it's full for notes held up to a second, fades for longer ones, and is gone for notes held 2 seconds or more;
  - palm mutes don't get one;
  - a note cut short by the next note on its string, or by MONO, doesn't get one either.
- **Power chords** get their own release, one per chord, from their own renders. If those aren't imported yet, each string plays its own.
- **Scrapes.** Now and then (about 1 release in 8, like the original) an unpitched scrape is mixed in, the next one in turn, and never on two releases in a row.
- **Fret noise on F#0**, the same key as your guitar instrument, so your MIDI parts work unchanged. Its recordings play in turn, as the original's 13 do, and velocity sets how loud.
- **A third page on the rack, NOISES:**
  - **RELEASE NOISE** and **FRET NOISE** levels (all the way down is off);
  - **SCRAPES**, how often a release gets one;
  - a screen showing what's been imported.

  The noises are on by default at the level they were rendered, the way you compare against the original.
- **Importing them.** The RENDERS page has a new **NOISES** tab with a row per noise list (each string's releases, power chords, unpitched, fret noise). Click MIDI to save a list or drag it into your DAW; IMPORT... the render or drop it on the row. SAVE ALL LISTS now saves the noise lists too.
- **The importer checks each render** so a wrong one can't fill your guitar with the wrong sounds:
  - it lines the render up with the list by itself;
  - it leaves out silent hits and exact copies;
  - it refuses a render where the notes can still be heard (the notes' Volume wasn't turned down);
  - it refuses one with no releases, or with a "scrape" on almost every note (Pitched Release wasn't turned down);
  - it refuses a string's releases dropped on another string's row (their pitches are wrong).
- **What's a guess:** where between 1 and 2 seconds of holding the releases stop. The original gave a release at 1 second and none at 2; PR7-I fades between the two. How it all sounds next to the original is for your ears.

## 0.5.0

**The GUITAR page rebuilt: a family rack unit across the top, a smaller guitar with its whole headstock.** This is the mock-up you approved, now working.

- **The rack.** All of the page's controls now sit on one rack unit across the top of the window, built from the same parts as PR7-G's units: the black pearl chassis with the red glow, the PARAGON PR7-I badge, stamped lettering, orange LED screens, big knobs with their values on small screens, and small keys with neon stripes. It's always the black finish, whatever finish you've chosen elsewhere, because it sits on the dark guitar page.
- **Two pages**, switched by the PAGE keys under the badge (the lit stripe shows which page is open):
  - **PLAYING:** TONE, BITE, ATTACK, TRANSPOSE, VEL > VOL, RELEASE, POLY WINDOW and VARIATION.
  - **FRETTING:** PICKING MODE, FRETTING MODE and PERFORMANCE on screens; FORCE STRING and HAND POSITION as knobs; CAPO, HAND SPAN, HAND RESET and MONO as small screens (drag up or down to change one, click MONO, double-click for the default).
- **On both pages:**
  - the screen, showing the key map, what's latched (SUSTAIN or PALM MUTE, POWER for power chords) with its keyswitch, and the last note: its note, velocity, take, string, fret and dynamic;
  - the KEY MAP keys (SPLIT, FACTORY) and the VARIATION on/off key;
  - OUTPUT: an LED meter, LEVEL and OUTPUT. The status light top right goes out while the plugin is bypassed.
- **NOW PLAYING is gone.** The rack's screen shows the same things. Nothing sits under the neck any more.
- **The guitar is a third smaller and lower**, re-rendered with the camera pulled back so the red grid floor still fills the window. Frets 0 to 24 now span about 920 pixels, and both pickups and both horns show. The note dots on the neck are sized to fit the closer strings.
- **The headstock.** The whole headstock is now in the picture, traced from your photo of the 7-string's headstock: smoothed like the body, with a neon edge and seven tuners. Each tuner has a post with a glowing ring on the face and a button outlined in neon along the top edge. The strings run over the nut to their own posts: the low A to the post nearest the nut, the high E to the one at the tip.
- **PARAGON on the headstock**, where the maker's name sits. It glows in the soft neon of the guitar's edges and is written in the rack badge's own lettering (InterDisplay Black Italic).
- **Nothing about the sound changed.** Your presets, projects and imported renders work as before.

## 0.4.7

**Smoother body edges.** The guitar body's outline flows as one smooth curve now, with no small corners, and the straight runs stay straight.

- **Why it wasn't smooth:** the body's outline was traced from your photo and stored as about 100 short straight lines, so every curve was a chain of tiny corners. The trace also picked up a little raggedness from the photo's wood grain and compression.
- **Now** the outline is resampled every millimetre and gently smoothed along its length when the guitar is built. The horn tips round off the way a real body's edges do. The neck join from 0.4.6 stays clean and parallel.

## 0.4.6

**A clean join where the body meets the neck.** You spotted that the body's glowing edge met the neck at a slant. That's fixed.

- **Why it slanted:** the body is traced from your photo, and along the neck the trace followed the photo's neck. That neck leans very slightly, so beside our perfectly straight neck the body's edge ran at a shallow angle from the 24th fret up to the horn.
- **Now** along the neck the body follows our own neck's edge, tucked just under the fretboard. The horn curves straight down onto the neck at the 19th fret, and the edge runs parallel to the fretboard. Nothing else in the picture changed.

## 0.4.5

**The page's controls stay readable over the guitar.** Every floating control (the knobs, menus and boxes) now sits on a soft dark backing, so a glowing body edge behind one can't cut across its label. The guitar still shows through.

- **Why now:** your second guitar instrument, PR7-A, has a long horn that runs right through the knob row. The backing fixes that there, and PR7-I gets it too so the two pages stay identical. Here it mostly shows behind TONE, where the RG's horn passes.
- **For PR7-A:** the 3D guitar can now be headless, and the body tracer handles photos with a drop shadow. Neither changes PR7-I's guitar.

## 0.4.4

**The GUITAR page is black again, not grey.** You were right that it looked greyer than PR7-D, so I measured both.

- **What was wrong:** the floor and the body in the guitar picture sat at a brightness of about 27 out of 255. The same parts of PR7-D's drum kit picture sit at about 5. The glowing parts matched; only the blacks had come out dark grey.
- **Why:** the lights in the guitar's 3D scene were about five times brighter than they needed to be, and the light that gives the body its gloss was also lighting the floor.
- **Now** the lights are at a fifth of what they were, and the gloss light only touches the guitar. The floor is black (about 1, the kit's is 3), and the body keeps a faint gloss (about 24, a kit drum shell is about 30). The neon edges, frets, strings and grid are unchanged.

## 0.4.3

**Notes land on time.** Your A/B render showed PR7-I's picks landing about 15 ms after your guitar instrument's. This fixes it.

- **What your two renders showed:** I lined up the same chug riff from both plugins. The pitches, levels, decay and tone match closely, within about 1 dB in every frequency band. But PR7-I's picks came 15 ms later by one measure and 22 ms later by another.
- **Why:** when PR7-I imports a take, it keeps 30 ms of sound from before the pick, and it used to play every take from the very start of that. So each pick landed 30 ms after its note-on, where your guitar instrument's land about 15 ms after.
- **Now** each note starts 15 ms into that lead-in, with a 1 ms fade so there's no click, and the pick lands 15 ms after the note-on. On 16ths at 120 BPM, 15 ms is an eighth of the gap between notes, so tight riffs should sit noticeably closer to the drums.
- **What can't be checked here:** whether it feels tighter to you. Tested to the sample with made-up takes.

## 0.4.2

**Repeated palm mutes no longer machine-gun.** On a chug, the same palm-mute take kept coming back several times in a row. That was a bug, and this fixes it.

- **What was wrong:** your palm-mute import kept only the different takes. That's about 3 or 4 per stroke on most notes; the rest were exact copies and left out. But PR7-I still counted through all 8 take slots, and an empty slot fell back to the first take. So eight chugs on one note played take 1, 2, 3, then take 1 five times running.
- **Now** PR7-I steps through only the takes a note actually has, in turn: 1, 2, 3, 1, 2, 3. That's the same in-turn order you liked in 0.2.0, just without the empty slots. Sustain sounds exactly as before, because both of its takes are always there.
- **NOW PLAYING** counts the takes that are there ("take 2 of 3"), not the 8 slots.
- **What can't be checked here:** how much closer this gets your chugs to your guitar instrument. Tested with made-up takes only.

## 0.4.1

**A fix for the tabs.** Clicking a tab could leave the page you came from lit as well, as in your screenshot where GUITAR and RENDERS were both lit on the RENDERS page.

- **Why:** when you click a tab, the old tab is switched off, and the plugin counted that as a click on the old tab too. So it briefly went back to the old page, and that tab stayed lit next to the new one. It's been there since the first tabs.
- **Now** only the tab you clicked lights up, and a test checks every switch between the four tabs.

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
