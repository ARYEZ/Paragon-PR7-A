# Changelog

Each version's notes appear on its GitHub Release page.

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
