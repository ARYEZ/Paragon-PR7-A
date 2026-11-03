# THE KIT VIEW's 3D kit (PR7-D 0.14.0): our own model, in the PR7 colours. Black drums lit by their own glowing rings
# (the "Tron" look), lathed and hammered onyx cymbals, braced stands, pedals, lugs and rods, a PR7-D logo on the kick.
# blender -b -P kit3d.py -- <out.png> [samples] [width] [height] [view] [pass] [region]: see README.md
import bpy, bmesh, math, sys
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = argv[0] if argv else "/tmp/kit.png"
SAMPLES = int(argv[1]) if len(argv) > 1 else 64
WIDTH = int(argv[2]) if len(argv) > 2 else 1600
HEIGHT = int(argv[3]) if len(argv) > 3 else 720
VIEW = argv[4] if len(argv) > 4 else "kit"
STYLE = argv[5] if len(argv) > 5 else "tron"   # "tron", "ink", "mask" (the click map), "lit:<piece>" (one piece glowing)
import os
LENS = float(os.environ.get("KIT_LENS", "74"))          # 0.37.0: the panel's lens (mm) and how far up it aims, for framing
TARGET_DZ = float(os.environ.get("KIT_TARGET_DZ", "-0.1"))

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

# ================================================================ materials
def material(name, colour, metallic=0.0, rough=0.4, coat=0.0, spec=0.5, emission=None, strength=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = colour
    b.inputs["Metallic"].default_value = metallic
    b.inputs["Roughness"].default_value = rough
    setin (b, ("Coat Weight", "Clearcoat"), coat)
    setin (b, ("Specular IOR Level", "Specular"), spec)
    if emission is not None:
        setin (b, ("Emission Color", "Emission"), emission)
        setin (b, ("Emission Strength",), strength)
    return m

def cymbal_material():
    # Near-black metal; light running round in circles (anisotropic, tangent round the cymbal's axis); lathe grooves
    # (rings) and hammer dents (cells) as bumps; the grooves also vary the roughness a little.
    m = material("onyx cymbal", srgb("1a161a"), metallic=1.0, rough=0.24)
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    setin (b, ("Anisotropic",), 0.85)
    tangent = nt.nodes.new("ShaderNodeTangent")
    tangent.direction_type = "RADIAL"
    tangent.axis = "Z"
    nt.links.new(tangent.outputs["Tangent"], b.inputs["Tangent"])
    coords = nt.nodes.new("ShaderNodeTexCoord")
    rings = nt.nodes.new("ShaderNodeTexWave")
    rings.wave_type = "RINGS"
    try:
        rings.rings_direction = "Z"
    except Exception:
        pass
    rings.inputs["Scale"].default_value = 110.0
    rings.inputs["Distortion"].default_value = 0.6
    rings.inputs["Detail"].default_value = 1.0
    nt.links.new(coords.outputs["Object"], rings.inputs["Vector"])
    cells = nt.nodes.new("ShaderNodeTexVoronoi")
    cells.inputs["Scale"].default_value = 38.0
    nt.links.new(coords.outputs["Object"], cells.inputs["Vector"])
    bump1 = nt.nodes.new("ShaderNodeBump")
    bump1.inputs["Strength"].default_value = 0.22
    bump1.inputs["Distance"].default_value = 0.0006
    nt.links.new(rings.outputs["Fac"], bump1.inputs["Height"])
    bump2 = nt.nodes.new("ShaderNodeBump")
    bump2.inputs["Strength"].default_value = 0.18
    bump2.inputs["Distance"].default_value = 0.002
    nt.links.new(cells.outputs["Distance"], bump2.inputs["Height"])
    nt.links.new(bump1.outputs["Normal"], bump2.inputs["Normal"])
    nt.links.new(bump2.outputs["Normal"], b.inputs["Normal"])
    ramp = nt.nodes.new("ShaderNodeMapRange")
    ramp.inputs["To Min"].default_value = 0.2
    ramp.inputs["To Max"].default_value = 0.32
    nt.links.new(rings.outputs["Fac"], ramp.inputs["Value"])
    nt.links.new(ramp.outputs["Result"], b.inputs["Roughness"])
    return m

def head_material(name, colour, rough):
    # A coated drumhead: a fine grain as a bump.
    m = material(name, colour, rough=rough, spec=0.35)
    nt = m.node_tree
    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 400.0
    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.06
    nt.links.new(noise.outputs["Fac"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], nt.nodes["Principled BSDF"].inputs["Normal"])
    return m

def glow_material(name, strength):
    # Light along the kit: pink on the drummer's left, red in the middle, orange on the right (the family's range),
    # picked by the world X of each point.
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
    rng.inputs["From Min"].default_value = 1.3
    rng.inputs["From Max"].default_value = -1.3
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    e = ramp.color_ramp.elements
    e[0].position, e[0].color = 0.0, srgb("ff2d6e")
    e[1].position, e[1].color = 1.0, srgb("ff6a24")
    mid = e.new(0.5)
    mid.color = srgb("ff2a2e")
    nt.links.new(geo.outputs["Position"], sep.inputs["Vector"])
    nt.links.new(sep.outputs["X"], rng.inputs["Value"])
    nt.links.new(rng.outputs["Result"], ramp.inputs["Fac"])
    nt.links.new(ramp.outputs["Color"], em.inputs["Color"])
    nt.links.new(em.outputs["Emission"], out.inputs["Surface"])
    return m

def grid_floor():
    # Gloss black with a faint red grid fading out from the kit.
    m = material("grid floor", srgb("070507"), rough=0.22, spec=0.5)
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    coords = nt.nodes.new("ShaderNodeTexCoord")
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    nt.links.new(coords.outputs["Object"], sep.inputs["Vector"])
    def line(axis):
        a = nt.nodes.new("ShaderNodeMath"); a.operation = "MULTIPLY"; a.inputs[1].default_value = 2.5
        f = nt.nodes.new("ShaderNodeMath"); f.operation = "FRACT"
        c = nt.nodes.new("ShaderNodeMath"); c.operation = "SUBTRACT"; c.inputs[1].default_value = 0.5
        ab = nt.nodes.new("ShaderNodeMath"); ab.operation = "ABSOLUTE"
        g = nt.nodes.new("ShaderNodeMath"); g.operation = "GREATER_THAN"; g.inputs[1].default_value = 0.49
        nt.links.new(sep.outputs[axis], a.inputs[0]); nt.links.new(a.outputs[0], f.inputs[0])
        nt.links.new(f.outputs[0], c.inputs[0]); nt.links.new(c.outputs[0], ab.inputs[0]); nt.links.new(ab.outputs[0], g.inputs[0])
        return g
    mx = nt.nodes.new("ShaderNodeMath"); mx.operation = "MAXIMUM"
    nt.links.new(line("X").outputs[0], mx.inputs[0]); nt.links.new(line("Y").outputs[0], mx.inputs[1])
    dist = nt.nodes.new("ShaderNodeVectorMath"); dist.operation = "LENGTH"
    nt.links.new(coords.outputs["Object"], dist.inputs[0])
    fade = nt.nodes.new("ShaderNodeMapRange")
    fade.inputs["From Min"].default_value = 0.4; fade.inputs["From Max"].default_value = 2.8
    fade.inputs["To Min"].default_value = 0.55; fade.inputs["To Max"].default_value = 0.0
    nt.links.new(dist.outputs["Value"], fade.inputs["Value"])
    k = nt.nodes.new("ShaderNodeMath"); k.operation = "MULTIPLY"
    nt.links.new(mx.outputs[0], k.inputs[0]); nt.links.new(fade.outputs["Result"], k.inputs[1])
    setin (b, ("Emission Color", "Emission"), srgb("e0103c"))
    nt.links.new(k.outputs[0], b.inputs["Emission Strength"])
    return m

ONYX_SHELL = material("onyx lacquer", srgb("0b090c"), rough=0.18, coat=1.0, spec=0.4)
PEARL_HEAD = head_material("smoked head", srgb("1c181c"), 0.45)
ONYX_HEAD = head_material("onyx head", srgb("1a171c"), 0.4)
CHROME = material("black chrome", srgb("2a2629"), metallic=1.0, rough=0.12)
SATIN = material("satin black", srgb("1e1b1e"), metallic=1.0, rough=0.35)
CYMBAL = cymbal_material()
RUBBER = material("rubber", srgb("0a0909"), rough=0.85, spec=0.2)
FELT = material("felt", srgb("2a2329"), rough=1.0, spec=0.1)
BEATER = material("beater felt", srgb("2a2428"), rough=0.95, spec=0.1)
GLOW = material("glow", srgb("ff4a3a"), emission=srgb("ff4a3a"), strength=6.0)
FLOOR = grid_floor()
NEON = glow_material("neon", 9.0)
NEON_SOFT = glow_material("neon soft", 3.0)

def assign(o, m):
    o.data.materials.clear()
    o.data.materials.append(m)

def smooth(o):
    for p in o.data.polygons:
        p.use_smooth = True

# ================================================================ building blocks
def cylinder(name, r, depth, loc, rot=(0, 0, 0), mat=CHROME, verts=48, caps=True):
    bpy.ops.mesh.primitive_cylinder_add(radius=r, depth=depth, location=loc, rotation=rot, vertices=verts,
                                        end_fill_type="NGON" if caps else "NOTHING")
    o = bpy.context.object
    o.name = name
    assign(o, mat)
    smooth(o)
    return o

def torus(name, major, minor, loc, rot=(0, 0, 0), mat=CHROME, seg=96):
    bpy.ops.mesh.primitive_torus_add(major_radius=major, minor_radius=minor, location=loc, rotation=rot,
                                     major_segments=seg, minor_segments=12)
    o = bpy.context.object
    o.name = name
    assign(o, mat)
    smooth(o)
    return o

def box(name, size, loc, rot=(0, 0, 0), mat=CHROME, bevel=0.003):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc, rotation=rot)
    o = bpy.context.object
    o.name = name
    o.scale = size
    assign(o, mat)
    if bevel > 0:
        m = o.modifiers.new("bevel", "BEVEL")
        m.width = bevel
        m.segments = 3
    return o

def sphere(name, r, loc, scale=(1, 1, 1), mat=CHROME):
    bpy.ops.mesh.primitive_uv_sphere_add(radius=r, location=loc, segments=24, ring_count=12)
    o = bpy.context.object
    o.name = name
    o.scale = scale
    assign(o, mat)
    smooth(o)
    return o

def rod(name, a, b, r=0.009, mat=CHROME):
    a, b = Vector(a), Vector(b)
    d = b - a
    o = cylinder(name, r, d.length, (a + b) / 2, mat=mat, verts=16)
    o.rotation_mode = "QUATERNION"
    o.rotation_quaternion = d.to_track_quat("Z", "Y")
    return o

def wing_nut(name, loc, r=0.008):
    # A small hub and two ears.
    cylinder(name + " hub", r, r * 1.2, loc, mat=CHROME, verts=16)
    box(name + " ears", (r * 4.2, r * 0.5, r * 1.6), (loc[0], loc[1], loc[2] + r * 0.4), mat=CHROME, bevel=0.002)

def collar(name, at, r, height=0.03, nut=True):
    cylinder(name + " collar", r * 1.45, height, at, mat=CHROME, verts=24)
    if nut:
        wing_nut(name + " clamp", (at[0] + r * 1.45 + 0.008, at[1], at[2]), r=0.006)

def tripod(name, cx, cy, top, spread=0.3, r=0.012, upper_r=0.0095, height_collar=None):
    # A braced tripod: the base tube, a collar at the legs, braces from a lower collar, rubber feet; the upper tube from a
    # height clamp to `top`.
    lower_top = height_collar if height_collar else top * 0.55
    rod(name + " tube", (cx, cy, 0.09), (cx, cy, lower_top), r=r)
    leg_top, brace_at = 0.34, 0.2
    collar(name + " legs", (cx, cy, leg_top), r, height=0.04, nut=False)
    collar(name + " braces", (cx, cy, brace_at), r, height=0.025, nut=False)
    for k in range(3):
        a = 2 * math.pi * k / 3 + 0.35
        foot = (cx + spread * math.cos(a), cy + spread * math.sin(a), 0.012)
        rod(name + " leg", (cx, cy, leg_top), foot, r=0.008)
        mid = (cx + spread * 0.55 * math.cos(a), cy + spread * 0.55 * math.sin(a), leg_top * 0.45 + 0.012 * 0.55)
        rod(name + " brace", (cx, cy, brace_at), mid, r=0.006)
        cylinder(name + " foot", 0.014, 0.02, foot, mat=RUBBER, verts=16)
        torus(name + " foot neon", 0.016, 0.0022, (foot[0], foot[1], 0.022), mat=NEON_SOFT, seg=24)
    collar(name + " height", (cx, cy, lower_top), r)
    rod(name + " upper", (cx, cy, lower_top), (cx, cy, top), r=upper_r)
    off = r + 0.0025
    rod(name + " strip", (cx, cy + off, 0.36), (cx, cy + off, lower_top - 0.02), r=0.0017, mat=NEON_SOFT)
    rod(name + " strip upper", (cx, cy + upper_r + 0.0022, lower_top + 0.03), (cx, cy + upper_r + 0.0022, top - 0.01), r=0.0015, mat=NEON_SOFT)

def lathe(name, profile, loc, tilt=(0, 0, 0), mat=CYMBAL, steps=128, thickness=0.0022):
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    rings = []
    for k in range(steps):
        a = 2 * math.pi * k / steps
        rings.append([bm.verts.new((r * math.cos(a), r * math.sin(a), z)) for r, z in profile])
    for k in range(steps):
        r0, r1 = rings[k], rings[(k + 1) % steps]
        for i in range(len(profile) - 1):
            bm.faces.new((r0[i], r0[i + 1], r1[i + 1], r1[i]))
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(o)
    o.location = loc
    o.rotation_euler = tilt
    assign(o, mat)
    smooth(o)
    s = o.modifiers.new("thick", "SOLIDIFY")
    s.thickness = thickness
    s.offset = -1.0
    return o

def cymbal_profile(r, bell_r=None, bell_h=0.032, bow_h=0.03, china=False, n=40):
    # A natural cymbal: a rounded bell, then a bow curving down to a thin edge (a china: its edge turned up).
    bell_r = bell_r or max(0.045, r * 0.24)
    pts = []
    for i in range(n + 1):
        x = r * i / n
        if x <= bell_r:
            t = x / bell_r
            z = bow_h + bell_h * (1 - t * t) ** 0.7
        else:
            t = (x - bell_r) / (r - bell_r)
            z = bow_h * max(0.0, 1 - t) ** 1.5   # (max: rounding can put the last point a hair past the rim)
            if china and t > 0.78:
                z += 0.022 * ((t - 0.78) / 0.22) ** 1.6
        pts.append((max(x, 0.0005), z))
    return pts

def neon_rings(name, centre, tilt, r, china=False, bell=True):
    from mathutils import Euler
    rot = Euler(tilt).to_matrix()
    c = Vector(centre)
    torus(name + " edge neon", r - 0.003, 0.0026, c + rot @ Vector((0, 0, 0.0035 + (0.022 if china else 0))), rot=tilt, mat=NEON, seg=160)
    if bell:
        br = max(0.045, r * 0.24)
        torus(name + " bell neon", br, 0.0018, c + rot @ Vector((0, 0, 0.032)), rot=tilt, mat=NEON_SOFT, seg=96)

def cymbal(name, diameter, centre, tilt=(0.3, 0, 0), china=False, stand=True, boom=None):
    # The cymbal (tilted towards the drummer), its felts, sleeve and wing nut; on a braced stand with a tilter.
    r = diameter / 2
    tilt = (-tilt[0], tilt[1], tilt[2])
    lathe(name, cymbal_profile(r, china=china), centre, tilt)
    neon_rings(name, centre, tilt, r, china)
    cx, cy, cz = centre
    cylinder(name + " felt top", 0.022, 0.012, (cx, cy, cz + 0.07), mat=FELT, verts=24)
    cylinder(name + " felt under", 0.022, 0.012, (cx, cy, cz - 0.004), mat=FELT, verts=24)
    rod(name + " sleeve", (cx, cy, cz - 0.02), (cx, cy, cz + 0.09), r=0.004)
    wing_nut(name + " nut", (cx, cy, cz + 0.085))
    if stand:
        box(name + " tilter", (0.024, 0.03, 0.035), (cx, cy, cz - 0.035), mat=SATIN)
        wing_nut(name + " tilter nut", (cx + 0.022, cy, cz - 0.035))
        tripod(name + " stand", cx, cy, cz - 0.055, spread=0.32)

def drum(name, diameter, depth, centre, tilt=(0, 0, 0), lugs=8, top=PEARL_HEAD, bottom=ONYX_HEAD, claws=False):
    # Shell, heads, hoops (chrome; a kick's onyx), rounded lugs, tension rods with their heads on the hoops; built
    # along Z under a pivot, then tilted into place.
    r = diameter / 2
    parts = [cylinder(name + " shell", r, depth, (0, 0, 0), mat=ONYX_SHELL, caps=False, verts=96),
             cylinder(name + " top head", r * 0.995, 0.002, (0, 0, depth / 2 + 0.003), mat=top, verts=96),
             cylinder(name + " bottom head", r * 0.995, 0.002, (0, 0, -depth / 2 - 0.003), mat=bottom, verts=96)]
    hoop = ONYX_SHELL
    for z in (depth / 2 + 0.008, -depth / 2 - 0.008):
        parts.append(cylinder(name + " hoop", r + 0.008, 0.02 if claws else 0.014, (0, 0, z), mat=hoop, caps=False, verts=96))
        parts.append(torus(name + " neon", r + 0.0125, 0.0032, (0, 0, z + (0.0075 if z > 0 else -0.0075)), mat=NEON, seg=128))
    parts.append(torus(name + " head ring", r * 0.8, 0.0018, (0, 0, depth / 2 + 0.0045), mat=NEON_SOFT, seg=128))
    parts.append(torus(name + " bottom glow", r + 0.003, 0.0022, (0, 0, -depth / 2 + 0.03), mat=NEON_SOFT, seg=128))
    for k in range(lugs):
        a = 2 * math.pi * (k + 0.5) / lugs
        x, y = (r + 0.014) * math.cos(a), (r + 0.014) * math.sin(a)
        for z in ((depth / 2 - 0.035, -depth / 2 + 0.035) if depth > 0.25 else (0.0,)):
            lug = sphere(name + " lug", 0.016, (x, y, z), scale=(0.7, 0.7, 1.6 if depth <= 0.25 else 1.2))
            parts.append(lug)
        for sign in (1, -1):
            hz = sign * (depth / 2 + 0.017)
            parts.append(rod(name + " rod", (x, y, hz), (x, y, sign * (depth / 2 - 0.035)), r=0.0028))
            parts.append(cylinder(name + " rod head", 0.0055, 0.008, (x, y, hz + sign * 0.004), mat=CHROME, verts=12))
            if claws:
                parts.append(box(name + " claw", (0.012, 0.02, 0.026), ((r + 0.006) * math.cos(a), (r + 0.006) * math.sin(a), sign * (depth / 2 + 0.006)),
                                 rot=(0, 0, a), mat=CHROME, bevel=0.002))
    pivot = bpy.data.objects.new(name, None)
    bpy.context.collection.objects.link(pivot)
    for p in parts:
        p.parent = pivot
    pivot.location = centre
    pivot.rotation_euler = tilt
    return pivot

# ================================================================ the kit (the drummer sits at y = +0.9, facing -y)
bpy.ops.mesh.primitive_plane_add(size=30, location=(0, 0, 0))
assign(bpy.context.object, FLOOR)

# Kick, on its side, the batter head (pearl) towards the drummer, the front head onyx with a glowing PR7 ring.
drum("KICK", 0.56, 0.45, (0, 0.05, 0.29), tilt=(math.pi / 2, 0, 0), lugs=10, top=ONYX_HEAD, bottom=PEARL_HEAD, claws=True)
for x in (-0.24, 0.24):
    rod("KICK spur", (x, -0.06, 0.2), (x * 1.45, -0.2, 0.012), r=0.009)
    cylinder("KICK spur foot", 0.012, 0.018, (x * 1.45, -0.2, 0.012), mat=RUBBER, verts=12)

def pedal(name, x, y, beater_to=None, pull_to=None):
    # A footboard and heel plate on a base, two posts, an axle across their tops, a cam, a chain to the board's toe;
    # a kick pedal's beater shaft and felt head, or a hi-hat pedal's pull rod.
    box(name + " base", (0.11, 0.3, 0.008), (x, y + 0.04, 0.006), mat=SATIN, bevel=0.002)
    box(name + " heel", (0.075, 0.07, 0.016), (x, y + 0.18, 0.016), mat=CHROME)
    box(name + " board", (0.075, 0.25, 0.01), (x, y + 0.03, 0.05), rot=(-0.22, 0, 0), mat=CHROME, bevel=0.004)
    from mathutils import Euler
    tip = Euler((-0.22, 0, 0)).to_matrix()
    centre = Vector((x, y + 0.03, 0.05))
    for side in (-1, 1):
        a = centre + tip @ Vector((side * 0.039, -0.122, 0.006))
        b = centre + tip @ Vector((side * 0.039, 0.122, 0.006))
        rod(name + " board neon", a, b, r=0.0022, mat=NEON)
        rod(name + " post neon", (x + side * 0.05, y - 0.08, 0.02), (x + side * 0.05, y - 0.08, 0.16), r=0.0016, mat=NEON_SOFT)
    rod(name + " heel neon", (x - 0.038, y + 0.216, 0.025), (x + 0.038, y + 0.216, 0.025), r=0.002, mat=NEON_SOFT)
    for side in (-1, 1):
        rod(name + " post", (x + side * 0.05, y - 0.09, 0.0), (x + side * 0.05, y - 0.09, 0.17), r=0.007)
    rod(name + " axle", (x - 0.06, y - 0.09, 0.17), (x + 0.06, y - 0.09, 0.17), r=0.006)
    cylinder(name + " cam", 0.022, 0.025, (x, y - 0.09, 0.17), rot=(0, math.pi / 2, 0), mat=SATIN, verts=24)
    toe = Vector((x, y - 0.085, 0.085))
    top = Vector((x, y - 0.11, 0.155))
    for i in range(6):
        p = toe.lerp(top, (i + 0.5) / 6)
        torus(name + " link", 0.006, 0.0018, p, rot=(math.pi / 2 * (i % 2), 0, 0), mat=CHROME, seg=12)
    if beater_to is not None:
        rod(name + " shaft", (x, y - 0.09, 0.17), beater_to, r=0.004)
        b = Vector(beater_to)
        cylinder(name + " beater", 0.028, 0.045, (b.x, b.y, b.z), rot=(0, math.pi / 2, 0), mat=BEATER, verts=24)
    if pull_to is not None:
        rod(name + " pull rod", (x, y - 0.09, 0.17), pull_to, r=0.003)

pedal("KICK pedal", 0.0, 0.42, beater_to=(0.0, 0.31, 0.36))

# 0.37.0: THE OWNER'S BIGGER KIT, laid out after their reference picture (its pieces' places worked out through a
# camera fitted to it: resources/kit/README.md): three rack toms and two floor toms, the hi-hat, six crashes, an FX crash
# with holes, two chinas, a splash and the ride; no tambourine. Sizes in metres (inches x 0.0254).

# Snare on its stand (a basket on a braced tripod), tilted towards the drummer, a strainer on its side.
sn = (-0.26, 0.61, 0.61)
drum("SNARE", 0.356, 0.14, sn, tilt=(-0.2, 0.1, 0), lugs=10)
box("SNARE strainer", (0.03, 0.022, 0.06), (sn[0] - 0.19, sn[1] + 0.02, sn[2]), mat=CHROME)
for k in range(3):
    a = 2 * math.pi * k / 3
    rod("SNARE basket", (sn[0], sn[1], sn[2] - 0.11), (sn[0] + 0.15 * math.cos(a), sn[1] + 0.15 * math.sin(a), sn[2] - 0.08), r=0.006)
tripod("SNARE stand", sn[0], sn[1], sn[2] - 0.11, spread=0.26)

# Three rack toms (10", 12", 13") on posts from a rack bar behind the kick, the high one on the drummer's left.
for nm, d, dep, c, t in (("TOM 1", 0.254, 0.20, (-0.51, -0.06, 0.76), (-0.5, 0.25, 0)),
                         ("TOM 2", 0.305, 0.22, (-0.12, -0.13, 0.79), (-0.5, 0.0, 0)),
                         ("TOM 3", 0.330, 0.24, (0.33, -0.12, 0.76), (-0.5, -0.25, 0))):
    drum(nm, d, dep, c, tilt=t, lugs=6)
    sphere(nm + " clamp", 0.022, (c[0] * 0.85, c[1] - 0.12, 0.62), mat=SATIN)
    rod(nm + " arm", (c[0] * 0.85, c[1] - 0.12, 0.62), (c[0], c[1] - 0.05, c[2] - 0.08), r=0.009)
    rod(nm + " post", (c[0] * 0.85, c[1] - 0.12, 0.02), (c[0] * 0.85, c[1] - 0.12, 0.62), r=0.014)
    cylinder(nm + " post foot", 0.03, 0.02, (c[0] * 0.85, c[1] - 0.12, 0.012), mat=RUBBER, verts=16)
rod("TOM rack bar", (-0.51 * 0.85, -0.18, 0.5), (0.33 * 0.85, -0.24, 0.5), r=0.014)

# Two floor toms (14", 16") on legs with brackets, on the drummer's right.
def floor_tom(name, d, dep, c):
    drum(name, d, dep, c, lugs=8)
    r = d / 2
    for k in range(3):
        a = 2 * math.pi * k / 3 + 0.5
        bx, by = c[0] + (r + 0.03) * math.cos(a), c[1] + (r + 0.03) * math.sin(a)
        box(name + " bracket", (0.03, 0.03, 0.05), (bx, by, c[2] + 0.08), rot=(0, 0, a), mat=CHROME)
        rod(name + " leg", (bx, by, c[2] + 0.1), (c[0] + (r + 0.065) * math.cos(a), c[1] + (r + 0.065) * math.sin(a), 0.012), r=0.0085)
        cylinder(name + " foot", 0.013, 0.02, (c[0] + (r + 0.065) * math.cos(a), c[1] + (r + 0.065) * math.sin(a), 0.012), mat=RUBBER, verts=12)
floor_tom("FLOOR", 0.356, 0.36, (0.5, 0.36, 0.38))
floor_tom("FLOOR 2", 0.406, 0.40, (0.68, 0.86, 0.34))

# Hi-hat: two plates (the top on a clutch), its braced stand, the pull rod down to its pedal.
hh = (-0.58, 0.68, 0.92)
lathe("HI-HAT top", cymbal_profile(0.178, bow_h=0.022), (hh[0], hh[1], hh[2] + 0.014), (0, 0, 0))
lathe("HI-HAT bottom", cymbal_profile(0.178, bow_h=0.022), (hh[0], hh[1], hh[2] - 0.004), (math.pi, 0, 0))
neon_rings("HI-HAT", (hh[0], hh[1], hh[2] + 0.014), (0, 0, 0), 0.178)
box("HI-HAT clutch", (0.022, 0.022, 0.03), (hh[0], hh[1], hh[2] + 0.07), mat=SATIN)
wing_nut("HI-HAT clutch nut", (hh[0] + 0.02, hh[1], hh[2] + 0.07))
rod("HI-HAT rod", (hh[0], hh[1], hh[2] - 0.02), (hh[0], hh[1], hh[2] + 0.12), r=0.0035)
tripod("HI-HAT stand", hh[0], hh[1], hh[2] - 0.03, spread=0.3, height_collar=0.5)
pedal("HI-HAT pedal", hh[0] + 0.03, hh[1] + 0.2, pull_to=(hh[0], hh[1], 0.3))

# The FX crash's holes: six round holes through its bow (a boolean cut, after the cymbal is made).
def holes(name, centre, tilt, r, count=6):
    from mathutils import Euler
    target = bpy.data.objects[name]
    rot = Euler(tilt).to_matrix()
    for k in range(count):
        a = 2 * math.pi * (k + 0.5) / count
        at = Vector(centre) + rot @ Vector((0.62 * r * math.cos(a), 0.62 * r * math.sin(a), 0.01))
        bpy.ops.mesh.primitive_cylinder_add(radius=0.13 * r, depth=0.2, location=at, rotation=tilt, vertices=32)
        cutter = bpy.context.object
        cutter.name = name + " hole cutter"
        mod = target.modifiers.new("hole %d" % k, "BOOLEAN")
        mod.operation = "DIFFERENCE"
        mod.object = cutter
        cutter.hide_render = True
        cutter.hide_viewport = True
        torus(name + " hole neon", 0.13 * r, 0.0016, at + rot @ Vector((0, 0, 0.004)), rot=tilt, mat=NEON_SOFT, seg=48)

# Cymbals on boom stands: each stand's tripod out from the kit's middle, its boom reaching in under the cymbal.
def boom_cymbal(name, diameter, centre, tilt=(0.25, 0, 0), china=False, base=(0.0, -0.6), drop=0.32):
    # `base`: where its stand stands (x, y), out of the way of the drums (the back crashes' behind the toms).
    cymbal(name, diameter, centre, tilt=tilt, china=china, stand=False)
    cx, cy, cz = centre
    bx, by = base
    out = Vector((bx - cx, by - cy, 0.0))
    out = out.normalized() if out.length > 1e-3 else Vector((0, -1, 0))
    top = cz - drop
    tripod(name + " stand", bx, by, top, spread=0.3)
    box(name + " tilter", (0.024, 0.03, 0.035), (cx, cy, cz - 0.035), mat=SATIN)
    wing_nut(name + " tilter nut", (cx + 0.022, cy, cz - 0.035))
    rod(name + " stand boom", (bx, by, top), (cx, cy, cz - 0.05), r=0.0085)
    collar(name + " stand boom", (bx, by, top), 0.0095)
    rod(name + " stand boom counterweight", (bx, by, top), (bx + 0.08 * out.x, by + 0.08 * out.y, top - 0.03), r=0.012)

# Left of the hi-hat, low to high: the FX crash (holes), crash 1, crash 3; the splash over tom 1; crash 4 high behind;
# crash 2 in the middle over the toms; crash 5 and 6 to the right; the china, the ride over the floor toms, china 2.
boom_cymbal("FX CRASH", 0.457, (-0.98, 0.72, 1.08), tilt=(0.1, -0.15, 0), base=(-1.32, 0.86))
holes("FX CRASH", (-0.98, 0.72, 1.08), (-0.1, -0.15, 0), 0.457 / 2)
boom_cymbal("CRASH", 0.457, (-1.0, 0.51, 1.26), tilt=(0.12, -0.2, 0), base=(-1.32, 0.42))
boom_cymbal("CRASH 3", 0.432, (-0.87, 0.24, 1.4), tilt=(0.12, -0.15, 0), drop=0.45, base=(-1.12, -0.12))
boom_cymbal("SPLASH", 0.254, (-0.64, 0.07, 1.16), tilt=(0.12, -0.1, 0), base=(-0.92, -0.32))
boom_cymbal("CRASH 4", 0.508, (-0.46, -0.05, 1.48), tilt=(0.12, -0.08, 0), drop=0.5, base=(-0.6, -0.6))
boom_cymbal("CRASH 2", 0.508, (0.08, 0.11, 1.42), tilt=(0.12, 0.0, 0), drop=0.5, base=(0.08, -0.62))
boom_cymbal("CRASH 5", 0.483, (0.61, -0.06, 1.48), tilt=(0.12, 0.1, 0), drop=0.5, base=(0.68, -0.55))
boom_cymbal("CRASH 6", 0.457, (0.77, 0.37, 1.34), tilt=(0.12, 0.15, 0), base=(1.12, 0.22))
boom_cymbal("CHINA", 0.457, (0.99, 0.69, 1.22), tilt=(0.1, 0.2, 0), china=True, base=(1.32, 0.62))
boom_cymbal("RIDE", 0.533, (0.81, 0.95, 1.06), tilt=(0.1, 0.15, 0), base=(1.12, 1.18))
boom_cymbal("CHINA 2", 0.406, (1.09, 1.11, 0.96), tilt=(0.1, 0.2, 0), china=True, base=(1.42, 1.28))

# A right-handed kit: built with the hi-hat on the drummer's right, mirrored (x -> -x).
for o in list(bpy.data.objects):
    if o.parent is not None or o.type in ("LIGHT", "CAMERA") or o.name.startswith("Plane"):
        continue
    o.location.x = -o.location.x
    if o.rotation_mode == "QUATERNION":
        q = o.rotation_quaternion.copy()
        o.rotation_quaternion = (q.w, q.x, -q.y, -q.z)
    else:
        e = o.rotation_euler
        o.rotation_euler = (e.x, -e.y, -e.z)

if STYLE == "ink":
    # Flat colours (diffuse only), the glow lines kept as flat colour, ink outlines (Freestyle), banded light.
    flat = {}
    def flat_material(m):
        if m.name in flat:
            return flat[m.name]
        n = bpy.data.materials.new(m.name + " ink")
        n.use_nodes = True
        b = n.node_tree.nodes["Principled BSDF"]
        if m.name.startswith("neon"):
            n.node_tree.nodes.clear()
            out = n.node_tree.nodes.new("ShaderNodeOutputMaterial")
            em = n.node_tree.nodes.new("ShaderNodeEmission")
            em.inputs["Color"].default_value = srgb("ff3d5a")
            em.inputs["Strength"].default_value = 1.6
            n.node_tree.links.new(em.outputs[0], out.inputs[0])
        else:
            tone = {"onyx lacquer": "2a2329", "smoked head": "d9cfd3", "onyx head": "3a3236", "black chrome": "8a8085",
                    "satin black": "5a5156", "onyx cymbal": "6b6569", "rubber": "141012", "felt": "6b5a60",
                    "beater felt": "d9cfd3", "grid floor": "d6c6ce"}.get(m.name, "5a5156")
            b.inputs["Base Color"].default_value = srgb(tone)
            b.inputs["Roughness"].default_value = 1.0
            setin (b, ("Specular IOR Level", "Specular"), 0.0)
        flat[m.name] = n
        return n
    for o in bpy.data.objects:
        if o.type == "MESH":
            for i, slot in enumerate(o.material_slots):
                if slot.material:
                    o.material_slots[i].material = flat_material(slot.material)
    scene.render.use_freestyle = True
    scene.view_layers[0].use_freestyle = True
    scene.render.film_transparent = False
    scene.render.line_thickness_mode = "ABSOLUTE"
    scene.render.line_thickness = 1.1
    ls = scene.view_layers[0].freestyle_settings.linesets[0]
    ls.select_by_visibility = True
    ls.select_silhouette = ls.select_border = ls.select_crease = True
    if ls.linestyle is None:
        ls.linestyle = bpy.data.linestyles.new("ink")
    ls.linestyle.color = (0.02, 0.01, 0.015)

# The PR7-D logo on the kick's front head (built after the mirror, so it reads the right way round from the front):
# Logo.cpp's polygons, 1 unit tall, slanted 0.3, the two speed cuts left as gaps, each edge a glowing tube; the 7 red,
# the rest pearl, as the plugin's own logo.
PEARL_NEON = bpy.data.materials.new("pearl neon")
PEARL_NEON.use_nodes = True
_nt = PEARL_NEON.node_tree
_nt.nodes.clear()
_em = _nt.nodes.new("ShaderNodeEmission")
_em.inputs["Color"].default_value = srgb("f2e8ec")
_em.inputs["Strength"].default_value = 6.0
_nt.links.new(_em.outputs[0], _nt.nodes.new("ShaderNodeOutputMaterial").inputs[0])
RED_NEON = material("red neon", srgb("ff2a2e"), emission=srgb("ff2a2e"), strength=10.0)

def logo_polygons():
    sp = 0.1
    bowl = [[(0, 0), (0.64, 0), (0.8, 0.15), (0.8, 0.45), (0.64, 0.6), (0.24, 0.6), (0.24, 1), (0, 1)],
            [(0.24, 0.2), (0.56, 0.2), (0.56, 0.4), (0.24, 0.4)]]
    letters, seven = [], []
    x = 0.0
    letters += [[(px + x, py) for px, py in p] for p in bowl]
    x += 0.8 + sp
    letters += [[(px + x, py) for px, py in p] for p in bowl]
    letters.append([(px + x, py) for px, py in [(0.4, 0.6), (0.64, 0.6), (0.84, 1), (0.6, 1)]])
    x += 0.84 + sp
    seven.append([(px + x, py) for px, py in [(0, 0), (0.8, 0), (0.8, 0.2), (0.44, 1), (0.18, 1), (0.5, 0.2), (0, 0.2)]])
    x += 0.8 + sp
    letters.append([(px + x, py) for px, py in [(0, 0.42), (0.4, 0.42), (0.4, 0.6), (0, 0.6)]])
    x += 0.4 + sp
    letters.append([(px + x, py) for px, py in [(0, 0), (0.64, 0), (0.82, 0.18), (0.82, 0.82), (0.64, 1), (0, 1)]])
    letters.append([(px + x, py) for px, py in [(0.24, 0.2), (0.58, 0.2), (0.58, 0.8), (0.24, 0.8)]])
    lean = lambda pt: (pt[0] - 0.3 * pt[1] + 0.3, pt[1])
    return [[lean(q) for q in p] for p in letters], [[lean(q) for q in p] for p in seven]

def logo(centre_x, face_y, centre_z, height):
    cuts = ((0.63, 0.68), (0.79, 0.83))
    width = 4.36
    def place(pt):   # logo units (y down) -> the front head; seen from the front, +x is on the viewer's right
        return (centre_x + (pt[0] - width / 2) * height, face_y, centre_z - (pt[1] - 0.5) * height)
    def pieces(a, b):
        ts = [0.0, 1.0]
        for top, bottom in cuts:
            for edge in (top, bottom):
                if (a[1] - edge) * (b[1] - edge) < 0:
                    ts.append((edge - a[1]) / (b[1] - a[1]))
        ts.sort()
        for t0, t1 in zip(ts, ts[1:]):
            mid = a[1] + (b[1] - a[1]) * (t0 + t1) / 2
            if any(top < mid < bottom for top, bottom in cuts) or t1 - t0 < 1e-6:
                continue
            yield ((a[0] + (b[0] - a[0]) * t0, a[1] + (b[1] - a[1]) * t0), (a[0] + (b[0] - a[0]) * t1, a[1] + (b[1] - a[1]) * t1))
    letters, seven = logo_polygons()
    for polys, mat in ((letters, PEARL_NEON), (seven, RED_NEON)):
        for poly in polys:
            for i in range(len(poly)):
                for a, b in pieces(poly[i], poly[(i + 1) % len(poly)]):
                    rod("LOGO", place(a), place(b), r=height * 0.018, mat=mat)
                    sphere("LOGO joint", height * 0.018, place(a), mat=mat)

front_y = 0.05 - 0.225 - 0.012
logo(0.0, front_y, 0.29, 0.075)
torus("KICK logo ring", 0.205, 0.003, (0, front_y, 0.29), rot=(math.pi / 2, 0, 0), mat=NEON_SOFT, seg=160)

# ================================================================ passes for the plugin (0.14.0)
# Which piece an object belongs to (its name's start), longest names first so "CRASH 2" isn't "CRASH".
PIECES = ["kick:KICK", "kick:LOGO", "snare:SNARE", "tom1:TOM 1", "tom2:TOM 2", "tom3:TOM 3", "floor:FLOOR",
          "floor2:FLOOR 2", "hihat:HI-HAT", "crash:CRASH", "crash2:CRASH 2", "crash3:CRASH 3", "crash4:CRASH 4",
          "crash5:CRASH 5", "crash6:CRASH 6", "fxcrash:FX CRASH", "ride:RIDE", "china:CHINA", "china2:CHINA 2",
          "splash:SPLASH"]   # (0.37.0: the owner's bigger kit; no tambourine)
PIECES.sort(key=lambda e: -len(e.split(":")[1]))

def piece_of(name):
    for e in PIECES:
        key, prefix = e.split(":")
        if name == prefix or name.startswith(prefix + " ") or name.startswith(prefix + "."):
            return key, name[len(prefix):].strip()
    return None, ""

def zone_of(name):
    # The slot a click there chooses (synth::drums::slotDefaults' order), or -1. Stands and legs choose nothing.
    piece, rest = piece_of(name)
    if piece is None or "stand" in rest or "leg" in rest or "foot" in rest or "bracket" in rest or "basket" in rest:
        return -1
    if piece == "snare":
        if rest.startswith("top head") or rest.startswith("head ring"):
            return 2                                  # SD: the head
        if rest.startswith(("hoop", "neon", "rod")):
            return 4                                  # SD2: the rimshot, on the hoop
        return 1                                      # RIM: the sidestick, on the shell
    if piece == "hihat":
        return 8 if rest.startswith("pedal") else 6   # PH on the pedal, CH on the cymbals
    if piece == "ride":
        if rest.startswith(("felt", "nut", "sleeve", "bell")):
            return 13
        return 12                                     # RD (the lathe itself splits at the bell, below)
    # 0.37.0: the rack toms HT, MT, LT (high to low, left to right), the floor toms T4 and T5; crashes 1 to 6, the FX
    # crash, china 2 on 0.36.0's slots.
    return {"kick": 0, "tom1": 9, "tom2": 7, "tom3": 5, "floor": 23, "floor2": 35, "crash": 11, "crash2": 14,
            "crash3": 29, "crash4": 30, "crash5": 31, "crash6": 32, "fxcrash": 33, "china": 15, "china2": 34,
            "splash": 16}[piece]

def flat_emission(name, value, ride_split=False):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs["Strength"].default_value = 1.0
    if ride_split:
        # The bell (within 1.15 x its radius of the centre, in the cymbal's own plane) is RB, the rest RD.
        coords = nt.nodes.new("ShaderNodeTexCoord")
        sep = nt.nodes.new("ShaderNodeSeparateXYZ")
        comb = nt.nodes.new("ShaderNodeCombineXYZ")
        length = nt.nodes.new("ShaderNodeVectorMath"); length.operation = "LENGTH"
        less = nt.nodes.new("ShaderNodeMath"); less.operation = "LESS_THAN"
        less.inputs[1].default_value = max(0.045, 0.533 / 2 * 0.24) * 1.15   # (0.37.0: a 21" ride)
        mix = nt.nodes.new("ShaderNodeMix"); mix.data_type = "RGBA"
        nt.links.new(coords.outputs["Object"], sep.inputs[0])
        nt.links.new(sep.outputs["X"], comb.inputs["X"]); nt.links.new(sep.outputs["Y"], comb.inputs["Y"])
        nt.links.new(comb.outputs[0], length.inputs[0]); nt.links.new(length.outputs["Value"], less.inputs[0])
        nt.links.new(less.outputs[0], mix.inputs["Factor"])
        mix.inputs["A"].default_value = value[0]
        mix.inputs["B"].default_value = value[1]
        nt.links.new(mix.outputs["Result"], em.inputs["Color"])
    else:
        em.inputs["Color"].default_value = value
    nt.links.new(em.outputs[0], out.inputs[0])
    return m

def zone_colour(slot):
    v = (slot + 1) * 7 / 255.0 if slot >= 0 else 0.0      # the red channel: (slot + 1) x 7 (0.37.0: 36 slots; 12 before); 0: nothing
    return (v, 0.0, 0.0, 1.0)

if STYLE == "mask":
    mats = {}
    for o in bpy.data.objects:
        if o.type != "MESH":
            continue
        z = zone_of(o.name)
        piece, rest = piece_of(o.name)
        if piece == "ride" and rest == "":
            m = flat_emission("zone ride", (zone_colour(12), zone_colour(13)), ride_split=True)
        else:
            m = mats.get(z) or flat_emission("zone %d" % z, zone_colour(z))
            mats[z] = m
        o.data.materials.clear()
        o.data.materials.append(m)
elif STYLE.startswith("lit:"):
    lit_piece = STYLE.split(":")[1]
    NEON_LIT = glow_material("neon lit", 60.0)
    NEON_SOFT_LIT = glow_material("neon soft lit", 30.0)
    for o in bpy.data.objects:
        if o.type != "MESH" or piece_of(o.name)[0] != lit_piece or zone_of(o.name) < 0:
            continue   # the piece itself, not its stand
        for i, slot in enumerate(o.material_slots):
            if slot.material == NEON:
                o.material_slots[i].material = NEON_LIT
            elif slot.material == NEON_SOFT:
                o.material_slots[i].material = NEON_SOFT_LIT

# ================================================================ light and camera
world = bpy.data.worlds.new("dark")
scene.world = world
world.use_nodes = True
world.node_tree.nodes["Background"].inputs["Color"].default_value = srgb("060407")
world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.15

def light(name, kind, loc, rot, energy, colour, size=1.0, spot=None):
    data = bpy.data.lights.new(name, kind)
    data.energy = energy
    data.color = colour[:3]
    if kind == "AREA":
        data.size = size
    if kind == "SPOT" and spot:
        data.spot_size = spot
        data.spot_blend = 0.6
    o = bpy.data.objects.new(name, data)
    bpy.context.collection.objects.link(o)
    o.location = loc
    o.rotation_euler = rot
    return o

light("key", "AREA", (-0.4, 2.6, 3.2), (math.radians(-40), 0, math.radians(-8)), 70, (1.0, 0.97, 0.95), size=3.0)
light("rim red", "SPOT", (-1.9, -1.9, 2.4), (math.radians(55), 0, math.radians(-40)), 700, srgb("ff3b3b"), spot=math.radians(38))
light("rim orange", "SPOT", (1.9, -1.9, 2.4), (math.radians(55), 0, math.radians(40)), 700, srgb("ff5a2c"), spot=math.radians(38))
light("cymbal sheen", "AREA", (0, -0.6, 3.4), (math.radians(20), 0, 0), 110, (1.0, 0.95, 0.94), size=2.5)
light("floor pool", "SPOT", (0, 0.2, 4.2), (0, 0, 0), 90, (1.0, 0.93, 0.92), spot=math.radians(50))

cam_data = bpy.data.cameras.new("camera")
cam = bpy.data.objects.new("camera", cam_data)
bpy.context.collection.objects.link(cam)
if VIEW == "cymbal":       # a close-up of the ride (after the mirror: at x = -0.85)
    cam_data.lens = 70
    cam.location = (-0.55, 1.3, 1.85)
    target = Vector((-0.85, 0.15, 1.12))
elif VIEW == "front":     # from the audience: the kick's front head
    cam_data.lens = 38
    cam.location = (0.0, -3.2, 1.25)
    target = Vector((0.0, 0.1, 0.6))
elif VIEW == "logo":
    cam_data.lens = 70
    cam.location = (0.0, -1.6, 0.42)
    target = Vector((0.0, 0.0, 0.29))
elif VIEW == "pedal":      # a close-up of the kick pedal and beater
    cam_data.lens = 60
    cam.location = (0.35, 1.25, 0.6)
    target = Vector((0.0, 0.35, 0.15))
elif VIEW == "panel":     # the plugin's KIT view: the drummer's view, framed for its panel
    # 0.37.0: the owner's reference picture's viewpoint (higher and steeper than before: fitted to it), framed for the
    # panel's 1280 x 924.
    cam_data.lens = LENS
    cam.location = (-0.486, 6.8, 3.862)
    target = Vector((-0.01, 0.115, 0.734 + TARGET_DZ))
else:
    cam_data.lens = 29
    if STYLE == "ink":
        for l in bpy.data.lights:
            l.energy *= 2.0
        world.node_tree.nodes["Background"].inputs["Color"].default_value = srgb("e6dade")
        world.node_tree.nodes["Background"].inputs["Strength"].default_value = 1.2
    cam.location = (0.0, 3.35, 2.55)    # behind and above the drum stool: the drummer's view
    target = Vector((0.0, 0.05, 0.74))
cam.rotation_euler = (target - cam.location).to_track_quat("-Z", "Y").to_euler()
scene.camera = cam

# ================================================================ render
scene.render.engine = "CYCLES"
scene.cycles.device = "CPU"
scene.cycles.samples = SAMPLES
scene.cycles.use_denoising = False   # (this build has no denoiser: more samples instead)
scene.render.resolution_x = WIDTH
scene.render.resolution_y = HEIGHT
enum = [v.identifier for v in scene.view_settings.bl_rna.properties["view_transform"].enum_items]
scene.view_settings.view_transform = "Filmic" if "Filmic" in enum else "Standard"
scene.cycles.seed = 7
if STYLE == "mask":
    scene.view_settings.view_transform = "Raw" if "Raw" in enum else "Standard"
    scene.view_settings.look = "None"
    scene.cycles.samples = 1
    scene.cycles.filter_width = 0.01
    scene.render.dither_intensity = 0.0
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.0
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
last = glare
if STYLE == "ink":
    glare.mix = -1.0
    post = tree.nodes.new("CompositorNodePosterize")
    post.inputs["Steps"].default_value = 4
    last = glare
comp = tree.nodes.new("CompositorNodeComposite")
tree.links.new(layers.outputs["Image"], glare.inputs["Image"])
tree.links.new(last.outputs["Image"], comp.inputs["Image"])
if STYLE == "mask":
    scene.use_nodes = False
if len(argv) > 6:
    # Only this region (x0,y0,x1,y1 in pixels from the top left) is rendered; the rest stays black.
    x0, y0, x1, y1 = (int(v) for v in argv[6].split(","))
    scene.render.use_border = True
    scene.render.use_crop_to_border = False
    scene.render.border_min_x, scene.render.border_max_x = x0 / WIDTH, x1 / WIDTH
    scene.render.border_min_y, scene.render.border_max_y = 1 - y1 / HEIGHT, 1 - y0 / HEIGHT
scene.render.filepath = OUT
bpy.ops.render.render(write_still=True)
print("RENDERED", OUT)
