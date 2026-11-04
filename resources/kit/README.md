# THE KIT VIEW's pictures (0.14.0; the kit and camera redone in 0.37.0)

Our own 3D drum kit, modelled and rendered for PR7-D in the PR7 family's colours, built into the plugin as
`KitImages::` binary data (see `CMakeLists.txt`) and drawn by `source/ui/KitView.cpp`.

| File | What |
|---|---|
| `kit.jpg` | The kit from the drummer's seat, 1896 x 744 (0.42.0: the reference picture's shape) |
| `kitzones.png` | The click map: each pixel's value (all channels) is the slot a click there plays + 1 (0: nothing) |
| `lit<piece>.png` | Each piece glowing brighter, cut to the area it changes; its alpha fades where the glow fades (19 pieces in 0.37.0, 12 since 0.43.0) |
| `kitlayout.txt` | `<piece> <x> <y> <width> <height>`: where each glow goes on `kit.jpg` |

## Making them again

Needs Blender 4.0 (`apt install blender`) and Python 3 with numpy and Pillow. In an empty folder with these scripts:

```bash
mkdir -p raw
blender -b -P kit3d.py -- $PWD/raw/mask.png 1 1896 744 panel mask
blender -b -P kit3d.py -- $PWD/raw/base.png 160 1896 744 panel tron
python3 regions.py raw/mask.png > raw/regions.txt
while read piece region; do blender -b -P kit3d.py -- $PWD/raw/lit-$piece.png 160 1896 744 panel lit:$piece $region; done < raw/regions.txt
python3 process_assets.py raw <the repo>/resources/kit
```

- `kit3d.py`: the scene. Arguments: output, samples, width, height, view (`panel` is the plugin's; `kit`, `front`,
  `logo`, `cymbal` and `pedal` were for the mock-ups), pass (`tron`: the kit; `mask`: the click map, each part a flat
  colour; `lit:<piece>`: that piece's glow about 6 times brighter) and, for a lit pass, the region to render (`x0,y0,x1,y1`).
- `regions.py`: each piece's region (its parts on the click map, with room for its halo and its reflection).
- `process_assets.py`: every hue into the family's range (330-30 degrees: the render's tone mapping burns the
  brightest orange towards yellow and the bloom turns pink towards purple), the click map decoded from the sRGB curve,
  each glow cut out with its alpha, and the layout. JPEG still moves a few colours a few degrees (and rings beside
  bright glints), so `KitView` pulls every hue into 318-42 degrees once more as it loads the pictures.

The base render takes about 8 minutes on 4 CPU cores; the glows about a minute each.

Which part plays which slot is in `kit3d.py`'s `zone_of` and `KitView.cpp`'s `slotPieces`: keep them together.

## 0.37.0: the owner's bigger kit

The owner sent a picture of the kit they wanted (from other drum software: only used as a layout to match, never
traced or shipped): three rack toms, two floor toms, the hi-hat, six crashes, an FX crash with holes, two chinas, a
splash and a ride. Its pieces were placed from that picture:

- a camera was fitted to it (least squares on six pieces whose places in a kit are known, the two pedals, the snare,
  the front floor tom, the middle rack tom and the kick's front, plus how round four round things look: a disc's height
  over width is about the cosine between its axis and the view to it); the fit leaves the lens open, and the sizes
  settle it (the front 16" floor tom shows 1.5 times the 12" rack tom's width: the camera about 6-7 m away);
- each piece's centre in the picture, through that camera, at a height we chose, gives its place; the whole scene was
  moved 0.19 m to put the kick where ours was;
- `panel` is now that camera (`KIT_LENS` 74 mm and `KIT_TARGET_DZ` -0.1 framed it for 1280 x 924; since 0.42.0, 46 mm
  and -0.11 for 1896 x 744, the reference picture's own shape, with the KIT VIEW across the whole page).

The click map's step went from 12 to 7 a slot (36 slots fit 252). The rack toms are HT, MT, LT left to right, the floor
toms T4 and T5; the tambourine isn't in this kit. The cymbals stand on boom stands (`boom_cymbal`, each with its stand's
place), the back ones behind the toms; the FX crash's holes are boolean cuts with a glowing ring each.

## 0.43.0: the owner's 12-piece kit

The owner sent a picture of another drum app's kit (its layout only matched, never traced, shipped or committed) and
asked for "exactly this layout ... how tight they are packed ... how straight and level". The kit is now the 12 pieces
their sample pack records: three rack toms on the kick, one floor tom, the hi-hat, snare, kick, crashes 1 and 2, a splash,
the ride and a china.

- A camera was chosen to match the picture: far back (about 15 m) with a long lens, about 38 degrees down (the drum heads'
  roundness), the pieces' sizes then agreeing with the picture within a few percent;
- each piece's centre in the picture, through that camera, at a height we chose, gives its place;
- then moved with the owner: toms 2 and 3 back so the kick's top shows, all toms straight, ~6 cm between the rack toms,
  crash 2 and the splash further back, the second floor tom gone and the floor tom back and right, every stand on clear
  floor (checked against each drum and stand leg);
- `panel` is that camera (`KIT_LENS` 134, `KIT_TARGET_DZ` 0.08, for 1896 x 744).
