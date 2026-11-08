# Changelog

Each version's notes appear on its GitHub Release page.

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
