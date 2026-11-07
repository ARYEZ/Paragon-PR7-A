# THE GUITAR VIEW's picture (mock-up stage, for 0.4.0)

Our own 3D 7-string, modelled and rendered in the PR7 family's "Tron" look the way PR7-D's kit was
(`resources/kit/` in PR7-D's history): gloss black body with carved facets and a glowing edge, an ebony neck with
glowing frets and inlays, strings lit in the family's string colours, two humbuckers, a fixed bridge, on a gloss floor
with a red grid. Not built into the plugin yet: the owner is approving the mock-up.

- **Scale:** the owner's reference (their guitar instrument's Main tab, mirrored): the neck about 11-12% of the
  window's width and nearly parallel, frets 0 to 24 across nearly the whole window, the body at its real size beside
  the neck, so mostly off the window with only its horns showing. 3000 px a metre at the plugin's 1x, the scale 91% of
  a real 26.5" so the frets fit as the reference's do.
- **View:** the owner's choice: the headstock to the right, the low A on top (as a guitar seen from the front).
- **Body:** a superstrat (offset double cutaway, pointed horns), our own outline, no maker's name or logo; the owner
  turned down a first body drawn from another guitar's photo. Its points are in `OUTLINE_PARTS`, in millimetres from
  the bridge. `GUITAR_VIEW=full` renders the whole guitar, to check its shape.
- `blender -b -P guitar3d.py -- <out.png> [samples] [width] [height] [geometry.json]` (Blender 4.0). 1600 x 900 is
  the window at 1x; the plugin's picture is 3200 x 1800 (about 8 minutes at 64 samples on 4 cores). `geometry.json`
  says where each string meets the nut and the bridge, each fret and the board's edges land, in window pixels, for the
  code that lights the notes on top.
