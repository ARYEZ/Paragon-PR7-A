# Turns the guitar's render (guitar3d.py) into THE GUITAR VIEW's picture (PR7-I 0.4.0): guitar.jpg, every hue pulled
# into the family's range (pink through red to orange; the render's tone mapping burns the brightest orange toward
# yellow), as PR7-D's kit pictures were.
# python3 process_guitar.py <render.png> <out.jpg>
import sys, os
import numpy as np
from PIL import Image, ImageFilter

SRC, OUT = sys.argv[1], sys.argv[2]
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

picture, moved = family_hues(np.array(Image.open(SRC).convert("RGB")))
print("guitar: %d pixels turned into the family's hues" % moved)
# (JPEG moves a few colours a few degrees: GuitarView pulls hues in again as it loads.)
Image.fromarray(picture).save(OUT, quality=88, subsampling=0, optimize=True)
