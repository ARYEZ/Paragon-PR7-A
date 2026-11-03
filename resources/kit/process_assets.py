# Turns the kit's renders (kit3d.py) into THE KIT VIEW's pictures (0.14.0):
#   kit.jpg          the kit, every hue pulled into the family's range (pink through red to orange)
#   kitzones.png     the click map: each pixel's value (all three channels) the slot it plays + 1, 0 for nothing
#   lit<piece>.png   each piece glowing, cut to the area that changes, its alpha fading where the glow fades
#   kitlayout.txt    "<piece> <x> <y> <width> <height>": where each cut goes on kit.jpg
# python3 process_assets.py <raw folder> <out folder>
import sys, os
import numpy as np
from PIL import Image, ImageFilter

RAW, OUT = sys.argv[1], sys.argv[2]
PIECES = ["kick", "snare", "tom1", "tom2", "tom3", "floor", "floor2", "hihat", "crash", "crash2", "crash3", "crash4",
          "crash5", "crash6", "fxcrash", "ride", "china", "china2", "splash"]   # (0.37.0: the owner's bigger kit)
LOW, HIGH = 330.0, 30.0     # the hues kept (degrees, through 0), inside the family's 318-42: JPEG drifts a few degrees

def family_hues(rgb):
    # Any pixel whose hue is outside LOW..HIGH (through red) is turned to the nearer end, keeping its saturation and
    # brightness: a yellow glint becomes orange, a purple haze pink.
    f = rgb.astype(np.float64) / 255.0
    mx, mn = f.max(axis=2), f.min(axis=2)
    c = mx - mn
    r, g, b = f[..., 0], f[..., 1], f[..., 2]
    h = np.zeros_like(mx)
    nz = c > 1e-9
    rm = nz & (mx == r)
    gm = nz & (mx == g) & ~rm
    bm = nz & ~rm & ~gm
    h[rm] = ((g[rm] - b[rm]) / c[rm]) % 6
    h[gm] = (b[gm] - r[gm]) / c[gm] + 2
    h[bm] = (r[bm] - g[bm]) / c[bm] + 4
    h *= 60.0
    out = (h > HIGH) & (h < LOW) & nz
    h2 = np.where(h <= 180.0, HIGH, LOW)
    hh = np.where(out, h2, h) / 60.0
    x = c * (1 - np.abs(hh % 2 - 1))
    zeros = np.zeros_like(c)
    sector = np.floor(hh).astype(int) % 6
    rr = np.select([sector == 0, sector == 1, sector == 2, sector == 3, sector == 4, sector == 5], [c, x, zeros, zeros, x, c])
    gg = np.select([sector == 0, sector == 1, sector == 2, sector == 3, sector == 4, sector == 5], [x, c, c, x, zeros, zeros])
    bb = np.select([sector == 0, sector == 1, sector == 2, sector == 3, sector == 4, sector == 5], [zeros, zeros, x, c, c, x])
    rebuilt = np.stack([rr + mn, gg + mn, bb + mn], axis=2)
    result = np.where(out[..., None], rebuilt, f)
    return np.clip(np.round(result * 255.0), 0, 255).astype(np.uint8), int(out.sum())

os.makedirs(OUT, exist_ok=True)
base, moved = family_hues(np.array(Image.open(os.path.join(RAW, "base.png")).convert("RGB")))
print("kit: %d pixels turned into the family's hues" % moved)
# (JPEG moves a few colours a few degrees, and rings near bright glints: KitView pulls hues in again as it loads.)
Image.fromarray(base).save(os.path.join(OUT, "kit.jpg"), quality=90, subsampling=0, optimize=True)

# The click map: Blender wrote (slot + 1) x 7 / 255 through the sRGB curve (0.37.0: 7, for 36 slots; 12 before); undo it.
m = np.array(Image.open(os.path.join(RAW, "mask.png")).convert("RGB"))[..., 0] / 255.0
linear = np.where(m <= 0.04045, m / 12.92, ((m + 0.055) / 1.055) ** 2.4) * 255.0
zones = np.clip(np.round(linear / 7.0), 0, 255).astype(np.uint8)
print("zones:", sorted(int(v) - 1 for v in np.unique(zones) if v > 0))
Image.fromarray(np.stack([zones] * 3, axis=2)).save(os.path.join(OUT, "kitzones.png"), optimize=True)

# Each piece was rendered lit only inside its region (regions.txt, "<piece> x0,y0,x1,y1"): its glow is looked for
# there, fading to nothing over the region's outer 30 pixels (where the lit render lacks the halo of lights outside it).
regions = {}
for line in open(os.path.join(RAW, "regions.txt")):
    piece, box = line.split()
    regions[piece] = [int(v) for v in box.split(",")]
layout = []
for piece in PIECES:
    lit, moved = family_hues(np.array(Image.open(os.path.join(RAW, "lit-%s.png" % piece)).convert("RGB")))
    rx0, ry0, rx1, ry1 = regions[piece]
    yy, xx = np.mgrid[0:base.shape[0], 0:base.shape[1]]
    edge = np.minimum.reduce([xx - rx0, rx1 - 1 - xx, yy - ry0, ry1 - 1 - yy]).astype(float)
    ramp = np.clip((edge - 4.0) / 26.0, 0.0, 1.0)
    diff = np.abs(lit.astype(int) - base.astype(int)).max(axis=2) * ramp
    ys, xs = np.nonzero(diff > 3)
    pad = 14
    x0, y0 = max(0, xs.min() - pad), max(0, ys.min() - pad)
    x1, y1 = min(base.shape[1], xs.max() + pad + 1), min(base.shape[0], ys.max() + pad + 1)
    alpha = Image.fromarray(np.clip(diff[y0:y1, x0:x1] * (255.0 / 20.0), 0, 255).astype(np.uint8))
    alpha = alpha.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.GaussianBlur(3))
    alpha = Image.fromarray((np.array(alpha) * ramp[y0:y1, x0:x1]).astype(np.uint8))
    rgba = np.dstack([lit[y0:y1, x0:x1], np.array(alpha)])
    Image.fromarray(rgba, "RGBA").save(os.path.join(OUT, "lit%s.png" % piece), optimize=True)
    layout.append("%s %d %d %d %d" % (piece, x0, y0, x1 - x0, y1 - y0))
    print(piece, layout[-1], "moved", moved)
open(os.path.join(OUT, "kitlayout.txt"), "w").write("\n".join(layout) + "\n")
