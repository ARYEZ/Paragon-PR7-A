# Traces a guitar body's outline off a photo, for guitar3d.py (PR7-I 0.4.0's body came from the owner's photo this way).
# The photo: the guitar straight on, neck up, on a plain light background. Writes the outline in millimetres from the
# fretboard's end (toward the neck +x) and the neck's centre line (toward the low strings +y), as body-outline.json.
#   python3 trace_body.py <photo> <out.json> [board end row] [neck width mm]
#   board end row: the photo's pixel row where the fretboard ends (default: asked of you by looking at the preview)
#   neck width mm: the neck's width there, which scales the photo (default 64, PR7-I's neck)
# It also writes <out>.png, the outline drawn on the photo, to check by eye. Needs OpenCV (pip install opencv-python-headless).
# The photo itself is never committed: only the outline is.
import cv2, numpy as np, sys, json

src, out = sys.argv[1], sys.argv[2]
board_end = int(sys.argv[3]) if len(sys.argv) > 3 else None
neck_mm = float(sys.argv[4]) if len(sys.argv) > 4 else 64.0
im = cv2.imread(src)
g = cv2.cvtColor(im, cv2.COLOR_BGR2GRAY)
h, w = g.shape
# the guitar: everything not joined to the background at the photo's corners
fg = (g < 235).astype(np.uint8)
ff = (1 - fg).copy()
mask = np.zeros((h + 2, w + 2), np.uint8)
for corner in ((0, 0), (w - 1, h - 1)):   # (as PR7-I 0.4.0's trace: the outline must come out the same)
    if ff[corner[1], corner[0]]:
        cv2.floodFill(ff, mask, corner, 2)
guitar = (ff != 2).astype(np.uint8)
# the neck: the run of pixels round the centre column in the top rows, its edges fitted as straight lines
rows = range(20, 300, 10)   # (the photo's top rows: only the neck there)
cx = None; L = []; R = []
for y in rows:
    xs = np.where(guitar[y])[0]
    runs = np.split(xs, np.where(np.diff(xs) > 1)[0] + 1)
    run = max(runs, key=len) if cx is None else min(runs, key=lambda r: abs((r[0] + r[-1]) / 2 - cx))
    cx = (run[0] + run[-1]) / 2; L.append(run[0]); R.append(run[-1])
fl = np.polyfit(list(rows), L, 1); fr = np.polyfit(list(rows), R, 1)
if board_end is None:
    preview = im.copy()
    for y in range(0, h, 50):
        cv2.line(preview, (0, y), (40, y), (0, 0, 255), 1); cv2.putText(preview, str(y), (44, y + 4), 0, 0.4, (0, 0, 255), 1)
    cv2.imwrite(out.replace('.json', '-rows.png'), preview)
    sys.exit("Look at " + out.replace('.json', '-rows.png') + " and give the row where the fretboard ends as the third argument")
# the neck cut away down to the board's end (under the neck, nothing shows: the 3D neck covers it)
for y in range(0, board_end):
    a, b = int(np.polyval(fl, y)) - 3, int(np.polyval(fr, y)) + 3
    guitar[y, max(0, a):min(w, b + 1)] = 0
# strap buttons and other small bits off
guitar = cv2.morphologyEx(guitar, cv2.MORPH_OPEN, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (23, 23)))
contours, _ = cv2.findContours(guitar, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
c = max(contours, key=cv2.contourArea)
neck_px = float(np.polyval(fr, board_end) - np.polyval(fl, board_end))
centre = float(np.polyval(fl, board_end) + np.polyval(fr, board_end)) / 2
k = neck_mm / neck_px
simple = cv2.approxPolyDP(c, 1.2, True)[:, 0, :]
points = [[round((board_end - py) * k, 2), round((centre - px) * k, 2)] for px, py in simple]
json.dump({"about": "The body's outline in millimetres: x from where the fretboard ends (toward the neck +), y from the neck's "
                    "centre line (toward the low strings +). Traced off a photo (trace_body.py); no maker's name or logo used.",
           "points": points}, open(out, "w"))
vis = im.copy()
cv2.polylines(vis, [c], True, (0, 0, 255), 3)
cv2.line(vis, (0, board_end), (w, board_end), (0, 200, 0), 1)
cv2.imwrite(out.replace('.json', '.png'), vis)
xs = [p[0] for p in points]; ys = [p[1] for p in points]
print(f"{len(points)} points; {max(xs) - min(xs):.0f} mm long, {max(ys) - min(ys):.0f} mm wide; check {out.replace('.json', '.png')}")
