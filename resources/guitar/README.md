# THE GUITAR page's picture (PR7-I 0.4.0)

Our own 3D 7-string, modelled and rendered in the PR7 family's "Tron" look the way PR7-D's kit was
(`resources/kit/` in PR7-D's history): gloss black body with a glowing edge, an ebony neck with
glowing frets and inlays, strings lit in the family's string colours, two humbuckers, a fixed bridge, on a gloss floor
with a red grid. Built into the plugin as `guitar.jpg` (`process_guitar.py`: the hues kept in the family's range) and `guitar-geometry.json`, drawn by `source/ui/GuitarView.cpp`.

- **Scale:** the owner's reference (their guitar instrument's Main tab, mirrored): the neck about 11-12% of the
  window's width and nearly parallel, frets 0 to 24 across nearly the whole window, the body at its real size beside
  the neck, so mostly off the window with only its horns showing. 3000 px a metre at the plugin's 1x, the scale 91% of
  a real 26.5" so the frets fit as the reference's do.
- **0.5.0, a third smaller:** the rack now runs across the top of the window, so the camera is pulled back
  (`GUITAR_ZOOM` 0.67: 2000 px a metre), the nut at x 1090 (`GUITAR_NUT_X`) and the neck's centre line at y 670
  (`GUITAR_NECK_Y`): frets 0-24 span ~920 px and the whole headstock shows. These are the defaults; 0.4's picture was
  `GUITAR_ZOOM=1 GUITAR_NUT_X=1560 GUITAR_NECK_Y=550`. The floor's grid is in world units, so it is finer too.
- **0.6.3, full size again** (the owner: "the bigger guitar looks a lot nicer"; the rack now hides with HIDE RACK):
  the defaults are 0.4's again, `GUITAR_ZOOM=1 GUITAR_NUT_X=1560 GUITAR_NECK_Y=550` (the geometry identical to 0.4.7's).
  The headstock is off the window's right edge at this size (still modelled: `GUITAR_VIEW=full` shows it). 0.5.0-0.6.2's
  smaller guitar: `GUITAR_ZOOM=0.67 GUITAR_NUT_X=1090 GUITAR_NECK_Y=670`.
- **Headstock (0.5.0):** `headstock-outline.json`, traced off the owner's photo of a 7-string's headstock (OpenCV: the
  dark headstock against a pale wall, the tuners taken off by erosion, the bevel stripe's slot closed, the thin pointed
  tip put back from the unprocessed mask), in mm from where it leaves the nut (+y the low strings' side), scaled so the
  photo's 107 px neck is our 59 mm, with the seven string posts (low A nearest the nut). Smoothed along its length as
  the body is (3 mm). On it: a neon edge, seven tuners (posts with neon rings, buttons with neon outlines off the low
  strings' edge: black chrome alone vanishes in the dark scene), the strings over the nut to their posts, and PARAGON
  in the plugin's own InterDisplay Black Italic (`resources/fonts`, the rack badge's lettering), soft neon, 27 mm
  (`GUITAR_HEAD_NAME_SIZE`), where the maker's name was. No maker's name or logo; the photo isn't committed.
  `GUITAR_HEADSTOCK` reads another outline.
- **View:** the owner's choice: the headstock to the right, the low A on top (as a guitar seen from the front).
- **Body:** traced off a photo of a 7-string superstrat at the owner's ask, after two drawn bodies didn't convince
  them (no maker's name or logo used anywhere; the photo itself is not in the repository). `body-outline.json` holds
  the outline in millimetres from the fretboard's end and the neck's centre line (traced with OpenCV: everything not
  joined to the photo's white background, the neck cut off down to the board's end, strap buttons smoothed off,
  simplified to 121 points, scaled so the photo's neck is our 64 mm). Only the horns and the top of the body show
  in the window. `GUITAR_VIEW=full` renders the whole guitar, to check its shape.
- `blender -b -P guitar3d.py -- <out.png> [samples] [width] [height] [geometry.json]` (Blender 4.0). 1600 x 900 is
  the window at 1x; the plugin's picture is 3200 x 1800 (about 8 minutes at 64 samples on 4 cores). `geometry.json`
  says where each string meets the nut and the bridge, each fret and the board's edges land, in window pixels, for the
  code that lights the notes on top.

## Another guitar (a sibling plugin, PR7-A: everything the same but the body's shape)

1. `python3 trace_body.py <photo> <out.json>` (the photo: straight on, neck up, plain light background): it writes a
   preview with the rows numbered; give the row where the fretboard ends: `python3 trace_body.py <photo> <out.json> <row>`.
   Check `<out>.png` (the outline on the photo). It reproduces this guitar's `body-outline.json` exactly from the RG
   photo with row 640.
2. `GUITAR_OUTLINE=<out.json> blender -b -P guitar3d.py -- guitar.png 64 3200 1800` (~8 min), then
   `python3 process_guitar.py guitar.png guitar.jpg`. The neck, strings and frets don't move, so
   `guitar-geometry.json` stays the same.
