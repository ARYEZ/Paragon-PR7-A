# Each piece's region to render lit: its clickable parts' box from the click map, with room for the glow's halo and
# its reflection on the floor below.
import sys, numpy as np
from PIL import Image
# (0.37.0: the owner's bigger kit; the click map's step is 7 now, for 36 slots)
PIECE_SLOTS = {"kick": [0], "snare": [1, 2, 4], "tom1": [9], "tom2": [7], "tom3": [5], "floor": [23],
               "hihat": [6, 8], "crash": [11], "crash2": [14], "ride": [12, 13], "china": [15], "splash": [16]}  # (0.43.0)
m = np.array(Image.open(sys.argv[1]).convert("RGB"))[..., 0] / 255.0
z = np.round(np.where(m <= 0.04045, m / 12.92, ((m + 0.055) / 1.055) ** 2.4) * 255.0 / 7.0).astype(int) - 1
H, W = z.shape
for piece, slots in PIECE_SLOTS.items():
    ys, xs = np.nonzero(np.isin(z, slots))
    x0, x1, y0, y1 = xs.min() - 70, xs.max() + 70, ys.min() - 70, ys.max() + 130
    print(piece, "%d,%d,%d,%d" % (max(0, x0), max(0, y0), min(W, x1), min(H, y1)))
