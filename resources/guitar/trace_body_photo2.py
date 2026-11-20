# PR7-A 0.4.2's body: traced off a product photo of a 7-string the owner chose (straight on, neck up, white background,
# a shop's badge top right), as trace_body.py traces, with what that photo needed: the neck measured from its own edges
# (rows 75-160, above both horns; the pale fret wires kept in it), the badge's corner left out, the strap button and
# the jack cut off along the body's edge rather than by a wide opening (which rounded every horn tip off: the owner
# asked for them sharp), 3 px of smoothing. The photo is never committed; the numbers below are that photo's.
#   python3 trace_body_photo2.py <photo> <out folder> [smoothing px]   ->  <out>/body-outline.json, <out>/traced-small.png
import cv2, numpy as np, json, sys
S = sys.argv[2]
im = cv2.imread(sys.argv[1]); h, w = im.shape[:2]
g = cv2.cvtColor(im, cv2.COLOR_BGR2GRAY)
spread = im.max(axis=2).astype(int) - im.min(axis=2).astype(int)
# the neck: rows 75-160 (above both horns), the run of non-white pixels nearest x 700
L, R, rows = [], [], []
for y in range(75, 161, 3):
    xs = np.nonzero(g[y, 500:950] < 246)[0] + 500   # (loose: the pale fret wires stay part of the neck)
    runs = np.split(xs, np.where(np.diff(xs) > 2)[0] + 1)
    run = min(runs, key=lambda r: abs((r[0] + r[-1]) / 2 - 700))
    rows.append(y); L.append(run[0]); R.append(run[-1])
def robust(ys, xs):
    ys, xs = np.array(ys, float), np.array(xs, float); keep = np.ones(len(ys), bool)
    for _ in range(5):
        f = np.polyfit(ys[keep], xs[keep], 1); r = np.abs(np.polyval(f, ys) - xs); keep = r < max(1.5, 2.5 * np.median(r[keep]))
    return f
fl, fr = robust(rows, L), robust(rows, R)
print("neck left x = %.4f y + %.1f, right x = %.4f y + %.1f; width %.1f px at row 160" % (*fl, *fr, np.polyval(fr, 160) - np.polyval(fl, 160)))
board_end = 636
# the guitar: tinted or dark (the background is white, the badge blue: kept apart by taking the guitar's own blob)
fg = ((spread > 18) | (g < 200)).astype(np.uint8)
fg[:560, 1040:] = 0                                  # the badge (its own rows and columns only: the body reaches under it)
for y in range(0, board_end):                        # the neck cut away down to the board's end
    a, b = int(np.polyval(fl, y)) - 3, int(np.polyval(fr, y)) + 3
    fg[y, max(0, a):min(w, b + 1)] = 0
fg = cv2.morphologyEx(fg, cv2.MORPH_CLOSE, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (9, 9)))
cnts, _ = cv2.findContours(fg, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
fill = np.zeros_like(fg); cv2.drawContours(fill, [max(cnts, key=cv2.contourArea)], -1, 1, -1)
# the strap button and the jack cut off along the body's edge (a straight line between the edge either side of each),
# not by a wide opening, which would round every horn tip off too
def lowest(x): return int(np.nonzero(fill[:, x])[0].max())
for a, b in ((640, 708), (885, 950)):
    ya, yb = lowest(a), lowest(b)
    for x in range(a, b + 1):
        fill[int(round(ya + (yb - ya) * (x - a) / (b - a))) + 1:, x] = 0
fill = cv2.morphologyEx(fill, cv2.MORPH_OPEN, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5)))
cnts, _ = cv2.findContours(fill, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
c = max(cnts, key=cv2.contourArea)[:, 0, :].astype(float)
n = int(sys.argv[3]) if len(sys.argv) > 3 else 3; wrap = np.concatenate([c[-n:], c, c[:n]]); k = np.ones(n) / n
c = np.stack([np.convolve(wrap[:, 0], k, "same"), np.convolve(wrap[:, 1], k, "same")], 1)[n:-n]
simple = cv2.approxPolyDP(np.round(c).astype(np.int32).reshape(-1, 1, 2), 0.8, True)[:, 0, :]
neck_px = np.polyval(fr, board_end) - np.polyval(fl, board_end); centre = (np.polyval(fl, board_end) + np.polyval(fr, board_end)) / 2
kmm = 64.0 / neck_px
# neck up in the photo, our neck runs to +x; low strings on the photo's left (a player's view: low E on the left with the
# neck up) go to +y in ours (low A on top)
pts = [[round((board_end - py) * kmm, 2), round((centre - px) * kmm, 2)] for px, py in simple]
xs, ys = [p[0] for p in pts], [p[1] for p in pts]
print(f"{len(pts)} points; {min(xs):.0f} to {max(xs):.0f} mm along (from the board's end), {min(ys):.0f} to {max(ys):.0f} across; {kmm:.3f} mm a px")
json.dump({"about": "The body's outline in millimetres: x from where the fretboard ends (toward the neck +), y from the neck's "
                    "centre line (toward the low strings +). Traced off a product photo of a 7-string the owner chose (straight on, "
                    "neck up); no maker's name or logo used.", "points": pts}, open(S + "/body-outline.json", "w"))
vis = im.copy(); cv2.polylines(vis, [simple.reshape(-1, 1, 2)], True, (0, 0, 255), 3)
cv2.line(vis, (0, board_end), (w, board_end), (0, 200, 0), 1)
cv2.imwrite(S + "/traced-small.png", cv2.resize(vis, None, fx=0.5, fy=0.5))
