# THE GUITAR VIEW's 3D guitar (PR7-I): our own 7-string, modelled and rendered in the PR7 family's "Tron" look, as
# PR7-D's kit was. Gloss black body with carved facets and a glowing edge, an ebony neck with glowing frets and
# inlays, strings lit in the family's string colours, two humbuckers, a fixed bridge, on a gloss floor with a red grid.
# The body's outline is our own drawing inspired by a guitar the owner likes (traced by eye, no maker's name or logo).
# blender -b -P guitar3d.py -- <out.png> [samples] [width] [height] [geometry.json]
import bpy, bmesh, math, sys, json, os
from mathutils import Vector
from bpy_extras.object_utils import world_to_camera_view

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = argv[0] if argv else "/tmp/guitar.png"
SAMPLES = int(argv[1]) if len(argv) > 1 else 32
WIDTH = int(argv[2]) if len(argv) > 2 else 1576
HEIGHT = int(argv[3]) if len(argv) > 3 else 530
GEOMETRY = argv[4] if len(argv) > 4 else None

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene

def srgb(h):
    c = [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    return tuple(((x + 0.055) / 1.055) ** 2.4 if x > 0.04045 else x / 12.92 for x in c) + (1.0,)

def setin(node, names, value):
    for n in names:
        if n in node.inputs:
            node.inputs[n].default_value = value
            return

def material(name, colour, metallic=0.0, rough=0.4, coat=0.0, spec=0.5, emission=None, strength=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = colour
    b.inputs["Metallic"].default_value = metallic
    b.inputs["Roughness"].default_value = rough
    setin(b, ("Coat Weight", "Clearcoat"), coat)
    setin(b, ("Coat Roughness",), 0.09)
    setin(b, ("Specular IOR Level", "Specular"), spec)
    if emission is not None:
        setin(b, ("Emission Color", "Emission"), emission)
        setin(b, ("Emission Strength",), strength)
    return m

def emission(name, colour, strength):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs["Color"].default_value = colour
    em.inputs["Strength"].default_value = strength
    nt.links.new(em.outputs["Emission"], out.inputs["Surface"])
    return m

def neon(name, strength):
    # Light across the guitar: pink at the low strings' side (top), red in the middle, orange at the high E's side,
    # as the strings' own colours, picked by each point's world Y.
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs["Strength"].default_value = strength
    geo = nt.nodes.new("ShaderNodeNewGeometry")
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    rng = nt.nodes.new("ShaderNodeMapRange")
    rng.inputs["From Min"].default_value = 0.15
    rng.inputs["From Max"].default_value = -0.15
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    e = ramp.color_ramp.elements
    e[0].position, e[0].color = 0.0, srgb("ff2d6e")
    e[1].position, e[1].color = 1.0, srgb("ff6a24")
    mid = e.new(0.5)
    mid.color = srgb("ff2a2e")
    nt.links.new(geo.outputs["Position"], sep.inputs["Vector"])
    nt.links.new(sep.outputs["Y"], rng.inputs["Value"])
    nt.links.new(rng.outputs["Result"], ramp.inputs["Fac"])
    nt.links.new(ramp.outputs["Color"], em.inputs["Color"])
    nt.links.new(em.outputs["Emission"], out.inputs["Surface"])
    return m

def grid_floor():
    # Gloss black with a faint red grid (one line every 24 mm: 48 px at the plugin's 2x), fading out from the guitar.
    m = material("grid floor", srgb("030203"), rough=0.25, spec=0.5)
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    coords = nt.nodes.new("ShaderNodeTexCoord")
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    nt.links.new(coords.outputs["Object"], sep.inputs["Vector"])
    def line(axis):
        a = nt.nodes.new("ShaderNodeMath"); a.operation = "MULTIPLY"; a.inputs[1].default_value = 3000.0 / 48   # a line every 48 px at 1x (PX_PER_M below)
        f = nt.nodes.new("ShaderNodeMath"); f.operation = "FRACT"
        c = nt.nodes.new("ShaderNodeMath"); c.operation = "SUBTRACT"; c.inputs[1].default_value = 0.5
        ab = nt.nodes.new("ShaderNodeMath"); ab.operation = "ABSOLUTE"
        g = nt.nodes.new("ShaderNodeMath"); g.operation = "GREATER_THAN"; g.inputs[1].default_value = 0.482
        nt.links.new(sep.outputs[axis], a.inputs[0]); nt.links.new(a.outputs[0], f.inputs[0])
        nt.links.new(f.outputs[0], c.inputs[0]); nt.links.new(c.outputs[0], ab.inputs[0]); nt.links.new(ab.outputs[0], g.inputs[0])
        return g
    mx = nt.nodes.new("ShaderNodeMath"); mx.operation = "MAXIMUM"
    nt.links.new(line("X").outputs[0], mx.inputs[0]); nt.links.new(line("Y").outputs[0], mx.inputs[1])
    dist = nt.nodes.new("ShaderNodeVectorMath"); dist.operation = "LENGTH"
    nt.links.new(coords.outputs["Object"], dist.inputs[0])
    fade = nt.nodes.new("ShaderNodeMapRange")
    fade.inputs["From Min"].default_value = 0.1; fade.inputs["From Max"].default_value = 0.75
    fade.inputs["To Min"].default_value = 0.24; fade.inputs["To Max"].default_value = 0.04
    nt.links.new(dist.outputs["Value"], fade.inputs["Value"])
    k = nt.nodes.new("ShaderNodeMath"); k.operation = "MULTIPLY"
    nt.links.new(mx.outputs[0], k.inputs[0]); nt.links.new(fade.outputs["Result"], k.inputs[1])
    setin(b, ("Emission Color", "Emission"), srgb("c0102c"))
    nt.links.new(k.outputs[0], b.inputs["Emission Strength"])
    return m

LACQUER = material("onyx lacquer", srgb("0a080b"), rough=0.22, coat=1.0, spec=0.5)
FACET = material("carved facet", srgb("2a1418"), rough=0.32, coat=0.6, spec=0.7)
EBONY = material("ebony", srgb("0b0909"), rough=0.5, spec=0.35)
NECKWOOD = material("neck", srgb("0d0a0c"), rough=0.3, coat=0.8, spec=0.4)
CHROME = material("black chrome", srgb("2a2629"), metallic=1.0, rough=0.16)
POLE = material("pole pieces", srgb("8a8084"), metallic=1.0, rough=0.2)
PICKUP = material("pickup cover", srgb("0c0b0c"), rough=0.35, spec=0.4)
PEARL = material("bone", srgb("e8e0e3"), rough=0.35, emission=srgb("f4eef1"), strength=0.6)
FRETGLOW = emission("fret glow", srgb("e0202c"), 2.2)
INLAY = emission("inlay glow", srgb("ff3b4a"), 3.0)
NEON = neon("neon edge", 9.0)
NEON_SOFT = neon("neon soft", 4.0)
FLOOR = grid_floor()

def link(o):
    bpy.context.collection.objects.link(o)
    return o

def assign(o, m):
    o.data.materials.clear()
    o.data.materials.append(m)

def smooth(o, angle=35):
    # smooth the rounded parts, keep flat faces flat (no shading bands across big flat caps)
    for p in o.data.polygons:
        p.use_smooth = True
    if hasattr(o.data, "use_auto_smooth"):
        o.data.use_auto_smooth = True
        o.data.auto_smooth_angle = math.radians(angle)

def mesh_from(name, verts, faces, mat):
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    bm = bmesh.new(); bm.from_mesh(me)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me); bm.free()
    me.update()
    o = link(bpy.data.objects.new(name, me))
    assign(o, mat)
    return o

def box(name, size, loc, mat, bevel=0.0, segs=3, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc, rotation=rot)
    o = bpy.context.object
    o.name = name
    o.scale = size
    bpy.ops.object.transform_apply(scale=True)
    if bevel:
        mod = o.modifiers.new("bevel", "BEVEL")
        mod.width = bevel
        mod.segments = segs
        bpy.ops.object.modifier_apply(modifier="bevel")
        smooth(o)
    assign(o, mat)
    return o

def cylinder(name, r, depth, loc, mat, rot=(0, 0, 0), verts=32):
    bpy.ops.mesh.primitive_cylinder_add(radius=r, depth=depth, location=loc, rotation=rot, vertices=verts)
    o = bpy.context.object
    o.name = name
    smooth(o)
    assign(o, mat)
    return o

def tube(name, pts, r, mat, closed=False):
    cu = bpy.data.curves.new(name, "CURVE")
    cu.dimensions = "3D"
    cu.bevel_depth = r
    cu.bevel_resolution = 3
    sp = cu.splines.new("POLY")
    sp.points.add(len(pts) - 1)
    for i, p in enumerate(pts):
        sp.points[i].co = (p[0], p[1], p[2], 1)
    sp.use_cyclic_u = closed
    o = link(bpy.data.objects.new(name, cu))
    o.data.materials.append(mat)
    return o

# 0.4.4: how bright the lights are (0.4.0 had them 5x brighter: its floor and body came out dark grey, about 27 of 255 against PR7-D's
# kit at about 5; the owner: "too bright, it looks more grey than black"). The neon is emission and stays as it was.
LIGHT = float(os.environ.get("GUITAR_LIGHT", "0.2"))
RIM = float(os.environ.get("GUITAR_RIM", "0.4"))
# A headless guitar (no headstock past the nut: the neck runs on a little to a string clamp): PR7-A 1, PR7-I 0.
HEADLESS = os.environ.get("GUITAR_HEADLESS", "0") == "1"

# ================================================================ dimensions (metres)
PX_PER_M = 3000.0                # the plugin's 1x pixels per metre
WIDTH_1X, HEIGHT_1X = 1600, 900   # the plugin window this picture fills (rendered at 2x for the plugin)
NECK_Y_1X = 550                  # the neck's centre line, pixels from the window's top
L = 0.613                        # scale length: 91% of a real 26.5", so frets 0-24 fit the window as the reference's do
X_BRIDGE = (1560 - L * PX_PER_M) / PX_PER_M   # the nut at x = 1560 px; the bridge off the left edge               # where the strings break over the saddles; the stage's left edge is x = 0
X_NUT = X_BRIDGE + L
# the neck nearly parallel, as the reference draws it (a real 7-string tapers more): the strings evenly spaced
NUT_SPREAD, BRIDGE_SPREAD = 0.051, 0.057  # low A to high E
NECK_NUT, NECK_END = 0.059, 0.064
TOP = 0.022                      # body top (the body is 44 mm thick, centred on z = 0)
BEVEL = 0.009

def fret_x(n):
    return X_BRIDGE + L * 2 ** (-n / 12)

def string_y(s, x):
    # string 0 (low A) on top (+y), fanning out from the nut to the bridge
    t = (X_NUT - x) / L
    spread = NUT_SPREAD + (BRIDGE_SPREAD - NUT_SPREAD) * t
    return spread / 2 - s * spread / 6

def neck_half(x):
    t = (X_NUT - x) / (X_NUT - fret_x(24))
    return (NECK_NUT + (NECK_END - NECK_NUT) * t) / 2

# ================================================================ the body
# A superstrat (offset double cutaway, pointed horns, the bass side's horn longer), our own outline, in millimetres
# from the bridge (x toward the neck, y toward the low strings: up in the picture).
def B(x, y):
    return (X_BRIDGE + x / 1000.0, y / 1000.0)

def catmull(pts, n=14):
    out = []
    for i in range(len(pts) - 1):
        p0, p1, p2, p3 = pts[max(0, i - 1)], pts[i], pts[i + 1], pts[min(len(pts) - 1, i + 2)]
        for k in range(n):
            t = k / n
            t2, t3 = t * t, t * t * t
            out.append(tuple(0.5 * ((2 * p1[j]) + (-p0[j] + p2[j]) * t + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t2
                                     + (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * t3) for j in range(2)))
    return out

OUTLINE_PARTS = [
    [(285, 104), (270, 98), (245, 86), (215, 68), (192, 50), (177, 36), (171, 32)],    # upper horn's inner curve, to the neck
    [(171, 32), (173, 0), (176, -32)],                                                  # (under the neck)
    [(176, -32), (184, -48), (200, -66), (222, -80), (245, -88)],                       # lower cutaway, to the horn's tip
    [(245, -88), (238, -98), (215, -104), (185, -106), (150, -112), (115, -128), (75, -152), (25, -170),
     (-35, -174), (-92, -162), (-132, -128), (-152, -72), (-157, 0), (-150, 66), (-126, 118), (-84, 150),
     (-26, 160), (30, 150), (80, 132), (125, 124), (170, 124), (215, 124), (255, 120), (278, 112), (285, 104)],
]
# 0.4.0 mock-up 3: the outline traced off a photo of a 7-string superstrat at the owner's ask (body-outline.json, in mm
# from the fretboard's end and the neck's centre line); OUTLINE_PARTS above is the drawn one it replaced
BOARD_END = X_BRIDGE + L * 2 ** (-24 / 12) - 0.007
# (GUITAR_OUTLINE: another body's outline, from trace_body.py; a sibling plugin's guitar differs only in this)
with open(os.environ.get("GUITAR_OUTLINE", os.path.join(os.path.dirname(os.path.abspath(__file__)), "body-outline.json"))) as f:
    outline = [(BOARD_END + x / 1000.0, y / 1000.0) for x, y in json.load(f)["points"]]
# the outline as a slab: one flat face on top and one below (n-gons, so the lacquer shades clean), the edge rounded
# by a bevel
n_out = len(outline)
verts = [(p[0], p[1], TOP) for p in outline] + [(p[0], p[1], -TOP) for p in outline]
faces = [tuple(range(n_out)), tuple(range(2 * n_out - 1, n_out - 1, -1))]
faces += [(i, i + n_out, (i + 1) % n_out + n_out, (i + 1) % n_out) for i in range(n_out)]
body = mesh_from("body", verts, faces, LACQUER)
mod = body.modifiers.new("round", "BEVEL")
mod.width = BEVEL
mod.segments = 6
mod.limit_method = "ANGLE"
mod.angle_limit = math.radians(60)
bpy.context.view_layer.objects.active = body
bpy.ops.object.modifier_apply(modifier="round")
smooth(body, 50)
# body faces after the cut: the facet floors keep the lacquer but read as lighter through their angle
# the neon edge: a thin tube round the body's widest line (its rounded side), lighting the floor and the edge
tube("edge glow", [(p[0], p[1], 0.0) for p in outline], 0.0011, NEON, closed=True)
# the edge tube sits on the outline itself; push it out to the rounded side (the bevel widens the outline by BEVEL)
edge = bpy.data.objects["edge glow"]
area = sum(outline[i][0] * outline[(i + 1) % len(outline)][1] - outline[(i + 1) % len(outline)][0] * outline[i][1] for i in range(len(outline)))
pts = edge.data.splines[0].points
n = len(pts)
for i in range(n):
    p, q, r = outline[i - 1], outline[i], outline[(i + 1) % n]
    tx, ty = r[0] - p[0], r[1] - p[1]
    ln = math.hypot(tx, ty) or 1
    nx, ny = (ty / ln, -tx / ln) if area > 0 else (-ty / ln, tx / ln)
    pts[i].co = (q[0] + nx * 0.0012, q[1] + ny * 0.0012, 0.0, 1)

# ================================================================ the neck
x_end = fret_x(24) - 0.007
neck_top = TOP + 0.004
fb_top = neck_top + 0.006
def tapered(name, x0, x1, z0, z1, extra, mat, bevel=0.0015):
    v = []
    for x in (x0, x1):
        h = neck_half(x) + extra
        v += [(x, -h, z0), (x, h, z0), (x, h, z1), (x, -h, z1)]
    faces = [(0, 1, 2, 3), (4, 7, 6, 5), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)]
    o = mesh_from(name, v, faces, mat)
    if bevel:
        mod = o.modifiers.new("bevel", "BEVEL"); mod.width = bevel; mod.segments = 3
        bpy.context.view_layer.objects.active = o
        bpy.ops.object.modifier_apply(modifier="bevel")
        smooth(o)
    return o
tapered("neck", x_end - 0.03, X_NUT + (0.024 if HEADLESS else 0.004), -0.004, neck_top, 0.0012, NECKWOOD, 0.003)
tapered("fretboard", x_end, X_NUT, neck_top - 0.001, fb_top, 0.0, EBONY, 0.0008)
# a soft neon line along both edges of the fretboard
for side in (1, -1):
    tube("board edge", [(x_end, side * (neck_half(x_end) + 0.0007), fb_top - 0.001), (X_NUT, side * (neck_half(X_NUT) + 0.0007), fb_top - 0.001)], 0.0004, NEON_SOFT)
# frets: glowing wires
for f in range(1, 25):
    x = fret_x(f)
    h = neck_half(x) - 0.0006
    cylinder("fret %d" % f, 0.00085, 2 * h, (x, 0, fb_top + 0.0002), FRETGLOW, rot=(math.pi / 2, 0, 0), verts=12)
# the nut: bone
nh = neck_half(X_NUT)
box("nut", (0.005, 2 * nh, 0.006), (X_NUT + 0.0025, 0, fb_top + 0.0015), PEARL, bevel=0.001)
# inlays: small glowing rings, doubles at 12 and 24
for f in (3, 5, 7, 9, 12, 15, 17, 19, 21, 24):
    x = (fret_x(f - 1) + fret_x(f)) / 2
    for y in ((0.0,) if f % 12 else (0.016, -0.016)):
        bpy.ops.mesh.primitive_torus_add(major_radius=0.0028, minor_radius=0.0005, location=(x, y, fb_top + 0.0001))
        assign(bpy.context.object, INLAY)
if HEADLESS:
    # a headless guitar's string clamp just past the nut: a black-chrome block across the neck, a soft neon line along it
    nh2 = neck_half(X_NUT)
    box("string clamp", (0.012, 2 * nh2 + 0.004, 0.009), (X_NUT + 0.014, 0, neck_top + 0.0045), CHROME, bevel=0.0015)
    tube("clamp edge", [(X_NUT + 0.0075, -nh2 - 0.002, neck_top + 0.0085), (X_NUT + 0.0075, nh2 + 0.002, neck_top + 0.0085)], 0.0005, NEON_SOFT)
else:
    # the headstock's start, past the nut (off the stage's right edge soon after)
    hv = [(X_NUT + 0.004, -nh - 0.001), (X_NUT + 0.004, nh + 0.001), (X_NUT + 0.03, nh + 0.009), (X_NUT + 0.12, nh + 0.013),
          (X_NUT + 0.12, -nh + 0.004), (X_NUT + 0.03, -nh - 0.004)]
    cu = bpy.data.curves.new("headstock", "CURVE"); cu.dimensions = "2D"; cu.fill_mode = "BOTH"; cu.extrude = 0.004; cu.bevel_depth = 0.0015
    sp = cu.splines.new("POLY"); sp.points.add(len(hv) - 1)
    for i, p in enumerate(hv):
        sp.points[i].co = (p[0], p[1], 0, 1)
    sp.use_cyclic_u = True
    hs = link(bpy.data.objects.new("headstock", cu)); hs.location.z = neck_top - 0.006; hs.data.materials.append(NECKWOOD)
    tube("headstock edge", [(p[0], p[1], neck_top - 0.002) for p in hv[1:] + hv[:1]], 0.0006, NEON_SOFT)

# ================================================================ pickups, bridge, controls
def humbucker(name, X):
    x, _ = B(X, 0)
    w, h = 0.036, 0.080
    box(name, (w, h, 0.007), (x, 0, TOP + 0.0025), PICKUP, bevel=0.0025, segs=4)
    for s in range(7):
        y = string_y(s, x)
        for dx in (-0.0085, 0.0085):
            cylinder(name + " pole", 0.0016, 0.002, (x + dx, y, TOP + 0.0062), POLE, verts=16)
    # a glowing ring round the pickup (the kit's rim light)
    r = 0.0035
    ring = []
    for k in range(64):
        a = 2 * math.pi * k / 64
        cxp, cyp = (w / 2 - r + 0.0015) * (1 if math.cos(a) >= 0 else -1), (h / 2 - r + 0.0015) * (1 if math.sin(a) >= 0 else -1)
        ring.append((x + cxp + r * math.cos(a), cyp + r * math.sin(a), TOP + 0.0006))
    tube(name + " ring", ring, 0.0006, NEON, closed=True)
humbucker("neck pickup", 126)
humbucker("bridge pickup", 42)
# fixed bridge: a black chrome plate, a saddle per string where the string breaks
box("bridge plate", (0.034, 0.094, 0.003), (X_BRIDGE - 0.010, 0, TOP + 0.0015), CHROME, bevel=0.0012)
for s in range(7):
    y = string_y(s, X_BRIDGE)
    box("saddle %d" % s, (0.014, 0.0085, 0.013), (X_BRIDGE - 0.004, y, TOP + 0.0075), CHROME, bevel=0.0012)
# a volume knob and the pickup switch
if not HEADLESS:   # (the RG's knob and switch: off the window either way; a headless body is smaller there)
    kx, ky = B(-62, -118)
    cylinder("knob", 0.010, 0.016, (kx, ky, TOP + 0.008), CHROME, verts=48)
    bpy.ops.mesh.primitive_torus_add(major_radius=0.0085, minor_radius=0.0006, location=(kx, ky, TOP + 0.0162))
    assign(bpy.context.object, NEON)
    sx, sy_ = B(14, -112)
    cylinder("switch base", 0.0045, 0.003, (sx, sy_, TOP + 0.0015), CHROME)
    cylinder("switch tip", 0.0022, 0.012, (sx + 0.003, sy_ - 0.003, TOP + 0.007), PEARL, rot=(0.5, 0.5, 0))

# ================================================================ strings
STRING_RGB = ["ff2d6e", None, None, "ff2a2e", None, None, "ff6a24"]
def string_colour(s):
    def mix(a, b, t): return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))
    pink, red, orange = (255, 45, 110), (255, 42, 46), (255, 106, 36)
    t = s / 6
    c = mix(pink, red, t * 2) if t < 0.5 else mix(red, orange, (t - 0.5) * 2)
    return "%02x%02x%02x" % c
z_string = fb_top + 0.0025
for s in range(7):
    m = material("string %d" % s, srgb("6a6266"), metallic=1.0, rough=0.25, emission=srgb(string_colour(s)), strength=3.0)
    r = 0.00034 - s * 0.00003
    a = Vector((X_BRIDGE - 0.004, string_y(s, X_BRIDGE), TOP + 0.0145))
    b = Vector((X_NUT + 0.009, string_y(s, X_NUT), z_string)) if HEADLESS else Vector((X_NUT + 0.03, string_y(s, X_NUT) * 0.9, z_string))
    d = b - a
    cylinder("string %d" % s, r, d.length, (a + b) / 2, m, verts=10)
    o = bpy.context.object
    o.rotation_euler = d.to_track_quat("Z", "Y").to_euler()

# ================================================================ floor, light, camera
bpy.ops.mesh.primitive_plane_add(size=3.0, location=(0.29, 0, -TOP - 0.001))
assign(bpy.context.object, FLOOR)

world = bpy.data.worlds.new("dark")
scene.world = world
world.use_nodes = True
world.node_tree.nodes["Background"].inputs["Color"].default_value = srgb("060407")
world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.02 * LIGHT

def light(name, kind, loc, rot, energy, colour, size=1.0, size_y=None, spot=None):
    data = bpy.data.lights.new(name, kind)
    data.energy = energy
    data.color = colour[:3]
    if kind == "AREA":
        data.size = size
        if size_y:
            data.shape = "RECTANGLE"
            data.size_y = size_y
    if kind == "SPOT" and spot:
        data.spot_size = spot
        data.spot_blend = 0.7
    o = link(bpy.data.objects.new(name, data))
    o.location = loc
    o.rotation_euler = rot
    return o

# a long strip light above, at an angle across the body: the gloss streak along the lacquer
light("sheen", "AREA", (0.10, 0.25, 1.1), (math.radians(-14), math.radians(-10), 0), 2.2 * LIGHT, (1.0, 0.95, 0.96), size=0.35, size_y=0.9)
light("key", "AREA", (-0.3, 0.6, 0.9), (math.radians(-35), math.radians(-25), 0), 1.2 * LIGHT, (1.0, 0.96, 0.95), size=0.6)
light("rim pink", "AREA", (0.29, 0.7, 0.18), (math.radians(-75), 0, 0), 9 * RIM, srgb("ff3b6a"), size=1.4, size_y=0.12)
light("rim orange", "AREA", (0.29, -0.7, 0.18), (math.radians(75), 0, 0), 9 * RIM, srgb("ff5a2c"), size=1.4, size_y=0.12)

for name in ("sheen", "key"):
    bpy.data.objects[name].visible_glossy = False
# a softbox above, invisible to the camera: its reflection is the lacquer's soft sheen, bright at the top left
# fading out across the body (a gradient, so the gloss reads as curved light rather than a hard-edged band)
bpy.ops.mesh.primitive_plane_add(size=1.0, location=(0.30, 0.05, 0.75))
box_ = bpy.context.object
box_.scale = (3.5, 2.6, 1)
box_.visible_camera = False
box_.visible_shadow = False
box_.visible_diffuse = False   # 0.4.4: only seen in reflections (the gloss on the lacquer), never lighting the floor grey
# ...and it lights the guitar only (Blender 4.0's light linking): the floor stays black, as under PR7-D's kit
receivers = bpy.data.collections.new("sheen receivers")
for name in ("body", "neck", "fretboard", "headstock"):
    if name in bpy.data.objects:
        receivers.objects.link(bpy.data.objects[name])
if hasattr(box_, "light_linking"):
    box_.light_linking.receiver_collection = receivers
sm = bpy.data.materials.new("softbox"); sm.use_nodes = True
nt = sm.node_tree; nt.nodes.clear()
out = nt.nodes.new("ShaderNodeOutputMaterial"); em = nt.nodes.new("ShaderNodeEmission")
tc = nt.nodes.new("ShaderNodeTexCoord"); grad = nt.nodes.new("ShaderNodeTexGradient"); grad.gradient_type = "QUADRATIC_SPHERE"
mp = nt.nodes.new("ShaderNodeMapping"); mp.inputs["Location"].default_value = (0.18, -0.15, 0); mp.inputs["Scale"].default_value = (4.2, 5.0, 1)
ramp = nt.nodes.new("ShaderNodeValToRGB"); el = ramp.color_ramp.elements
el[0].position, el[0].color = 0.0, (0, 0, 0, 1); el[1].position, el[1].color = 0.9, (1.0, 0.93, 0.95, 1)
nt.links.new(tc.outputs["Object"], mp.inputs["Vector"]); nt.links.new(mp.outputs["Vector"], grad.inputs["Vector"])
nt.links.new(grad.outputs["Fac"], ramp.inputs["Fac"]); nt.links.new(ramp.outputs["Color"], em.inputs["Color"])
em.inputs["Strength"].default_value = float(os.environ.get("GUITAR_SHEEN", "0.15"))   # the gloss on the lacquer (the softbox lights only the guitar)
nt.links.new(em.outputs["Emission"], out.inputs["Surface"])
assign(box_, sm)

cam_data = bpy.data.cameras.new("camera")
cam_data.type = "ORTHO"
cam_data.ortho_scale = WIDTH / 2000.0 * (1576 / WIDTH) if WIDTH else 0.788    # 2000 px a metre at the plugin's 1x
cam_data.ortho_scale = WIDTH_1X / PX_PER_M
cam = link(bpy.data.objects.new("camera", cam_data))
# straight down over the stage's centre, tilted a few degrees toward the player so the sides show
TILT = math.radians(float(os.environ.get("GUITAR_TILT", "5")))
centre = Vector((0.5 * WIDTH_1X / PX_PER_M, (NECK_Y_1X - 0.5 * HEIGHT_1X) / PX_PER_M / math.cos(TILT), 0.0))
cam.location = centre + Vector((0, -math.sin(TILT), math.cos(TILT))) * 1.5
cam.rotation_euler = (TILT, 0, 0)
cam_data.clip_end = 10
scene.camera = cam
if os.environ.get("GUITAR_VIEW") == "full":    # the whole guitar, for checking its shape
    cam_data.ortho_scale = 1.15
    cam.location.x = X_BRIDGE + 0.33
    cam.location.y -= (NECK_Y_1X - 0.5 * HEIGHT_1X) / PX_PER_M

scene.render.engine = "CYCLES"
scene.cycles.device = "CPU"
scene.cycles.samples = SAMPLES
scene.cycles.use_denoising = False
scene.render.resolution_x = WIDTH
scene.render.resolution_y = HEIGHT
enum = [v.identifier for v in scene.view_settings.bl_rna.properties["view_transform"].enum_items]
scene.view_settings.view_transform = "Filmic" if "Filmic" in enum else "Standard"
scene.cycles.seed = 7
scene.use_nodes = True
tree = scene.node_tree
tree.nodes.clear()
layers = tree.nodes.new("CompositorNodeRLayers")
glare = tree.nodes.new("CompositorNodeGlare")
glare.glare_type = "FOG_GLOW"
glare.quality = "HIGH"
glare.threshold = 0.9
glare.size = 8
glare.mix = -0.35
comp = tree.nodes.new("CompositorNodeComposite")
tree.links.new(layers.outputs["Image"], glare.inputs["Image"])
tree.links.new(glare.outputs["Image"], comp.inputs["Image"])

# where the strings, frets and nut land in the picture, for the code that lights the notes on top
if GEOMETRY:
    bpy.context.view_layer.update()
    def px(x, y, z):
        v = world_to_camera_view(scene, cam, Vector((x, y, z)))
        return [round(v.x * WIDTH, 2), round((1 - v.y) * HEIGHT, 2)]
    geo = {"width": WIDTH, "height": HEIGHT,
           "strings": [{"bridge": px(X_BRIDGE, string_y(s, X_BRIDGE), z_string), "nut": px(X_NUT, string_y(s, X_NUT), z_string)} for s in range(7)],
           "frets": [px(fret_x(f), 0, fb_top) for f in range(0, 25)],
           "boardTop": [px(x, neck_half(x), fb_top) for x in (x_end, X_NUT)],
           "boardBottom": [px(x, -neck_half(x), fb_top) for x in (x_end, X_NUT)]}
    with open(GEOMETRY, "w") as f:
        json.dump(geo, f)

scene.render.filepath = OUT
bpy.ops.render.render(write_still=True)
print("RENDERED", OUT)
