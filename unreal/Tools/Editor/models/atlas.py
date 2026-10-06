"""Atlas: the heavy assault walker (docs/new-units.md, "Looks"; concept art/models/atlas/concepts/atlas-c1-foundry.jpg).
Blockout refined at step 5 (2026-10-05) to painted-v3 and the foundry concept: rivet bands, hazard stripes, slit glass
and brow, bezelled searchlight, ringed shoulder pods, cannon vents and rings, soot-topped stacks, framed glowing back
slats, cable clamps, leg pistons, amber lamps, three-toed feet with a heel toe. Part names and pivots are the blockout's.

    UnrealEditorBG <abs>/unreal/Autocraft.uproject -run=pythonscript \
        -script=<abs>/unreal/Tools/Editor/models/atlas.py -unattended -nullrhi -nosplash -nosound -abslog=<log>

Writes /Game/Models/atlas/SM_<part>__<material> and the catalog entry `atlas_blue`.

UE cm, +X forward, +Y right, Z up, the soles on Z = 0. About 285 cm to the top of the exhaust stacks, twice a
Longbow (135). Legs bend the way the concept's do: the knee forward of the line hip to ankle. `_0` is the left
(-Y), `_1` the right (+Y).

Tree: root > body (pelvis: walk bob) > torso (yaw) > cannons_0/1 (pitch, recoil), vents, searchlight;
body > hips_i > knees_i > ankles_i > feet_i, with kneeLamps_i under knees_i.
"""
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from ac_modelkit import Model, box, cyl, cone, sphere, bar, loft, ring_box  # noqa: E402

WALK = "AcPoseVehicles.cpp:PoseAtlas"

m = Model("atlas", category="unit", notes=[
    "Heavy walker, about 285 cm to the stack tops. Pose: PoseAtlas (to write): walk cycle from distance walked, torso yaw, "
    "cannon pitch and recoil, vents glow after each volley, quake stomp on one leg.",
    "Parts as in docs/new-units.md, 'Looks' (hips, knees, ankles, feet, torso, cannons, vents, searchlight, kneeLamps); smoke_0/1 are the stacks' particle sockets."])
for name in ("hullLight", "hullMid", "hullDark", "team", "hazard", "chrome", "glowOrange", "glowBlue"):
    m.material(name)
m.material("ventGlow")
m.material("floodGlow")
m.material("canopy", parent="hull", color=(0.012, 0.018, 0.03), metallic=0.9, roughness=0.08)
m.material("soot", parent="hull", color=(0.008, 0.008, 0.009), metallic=0.1, roughness=0.95)


def d(what, channels=("rotation",)):
    return dict(what=what, channels=list(channels), functions=[WALK])


# --- pelvis ---------------------------------------------------------------------------------------------
body = m.part("body", at=(0, 0, 160), driver=d("walk bob and sway of the whole body", ("position", "rotation")))
m.mesh(body, "hullMid", [box((74, 112, 30), at=(0, 0, 160)), box((50, 70, 12), at=(-10, 0, 142))])
m.mesh(body, "hullDark", box((30, 70, 16), at=(-44, 0, 160)))

# --- torso: the boiler, yaws on the pelvis -------------------------------------------------------------------
torso = m.part("torso", parent=body, at=(0, 0, 182), driver=d("torso yaw toward the target (3 rad/s)"))
trunk = [(58, 52, 30, 2.6), (30, 60, 36, 3.0), (0, 64, 38, 3.2), (-30, 62, 38, 3.2), (-58, 56, 34, 2.8)]
m.mesh(torso, "hullLight", loft([ring_box(x, w, h, 0, 212, 14, p) for x, w, h, p in trunk]))


def trunk_at(x):
    for (x0, w0, h0, p0), (x1, w1, h1, p1) in zip(trunk, trunk[1:]):
        if x1 <= x <= x0:
            t = (x0 - x) / (x0 - x1)
            return w0 + (w1 - w0) * t, h0 + (h1 - h0) * t, p0 + (p1 - p0) * t
    raise ValueError(x)


m.mesh(torso, "team", [box((12, 92, 52), at=(58, 0, 214)),          # front armour plate
                       box((64, 84, 7), at=(8, 0, 249))])            # top plate
m.mesh(torso, "hullDark", [box((5, 54, 11), at=(64, 0, 226)),       # the window slit
                           cyl(9, 44, at=(-36, -30, 270), base=False), cyl(9, 44, at=(-36, 30, 270), base=False),
                           # ring mounts for the cannons
                           cyl(22, 8, at=(10, -42, 254)), cyl(22, 8, at=(10, 42, 254)),
                           # cable bundles from the chest down to the hips
                           bar((46, -26, 200), (34, -14, 178), 6, shape="cyl"), bar((46, 26, 200), (34, 14, 178), 6, shape="cyl"),
                           bar((44, -16, 202), (30, -4, 176), 5, shape="cyl"), bar((44, 16, 202), (30, 4, 176), 5, shape="cyl")])
m.mesh(torso, "hullMid", [cyl(11, 6, at=(-36, -30, 294)), cyl(11, 6, at=(-36, 30, 294))])
m.mesh(torso, "soot", [cyl(9.4, 0.7, at=(-36, s * 30, 297.2)) for s in (-1, 1)], solid=False)
m.mesh(torso, "hazard", [box((34, 40, 2.5), at=(-14, -34, 252.5)), box((34, 40, 2.5), at=(-14, 34, 252.5))])
m.mesh(torso, "hazard", [box((30, 4, 2.4), at=(-44, 0, 249.2))], solid=False)
# cockpit slit: smoked glass in a heavy gunmetal brow and sill
m.mesh(torso, "canopy", box((2.5, 50, 7), at=(66.8, 0, 226)), solid=False)
m.mesh(torso, "hullDark", [box((9, 62, 6), at=(64, 0, 232.5)), box((7, 60, 3.5), at=(63.5, 0, 220)),
                           box((7, 4, 14), at=(64, -29, 226)), box((7, 4, 14), at=(64, 29, 226))], solid=False)
# riveted bands round the boiler: two ring bands and a row of rivets on each
for bx in (36, -22):
    w, h, p = trunk_at(bx)
    m.mesh(torso, "hullDark", loft([ring_box(bx - 3, w * 1.025 + 0.4, h * 1.025 + 0.4, 0, 212, 14, p),
                                    ring_box(bx + 3, w * 1.025 + 0.4, h * 1.025 + 0.4, 0, 212, 14, p)]), solid=False)
    for k in range(0, 14):
        a_ = 2 * math.pi * (k + 0.5) / 14
        c_, s_ = math.cos(a_), math.sin(a_)
        px = math.copysign(abs(c_) ** (2 / p), c_) * (w * 1.03 + 0.6)
        pz = math.copysign(abs(s_) ** (2 / p), s_) * (h * 1.03 + 0.6)
        m.mesh(torso, "chrome", sphere(1.7, at=(bx, px, 212 + pz), steps=6), solid=False)
# round blue shoulder pods, a gunmetal swivel disc inside, and a light ring round each equator
for s in (-1, 1):
    m.mesh(torso, "team", sphere(31, at=(0, s * 82, 228), squash=(1.05, 1.0, 0.95)))
    m.mesh(torso, "hullDark", cyl(15, 8, at=(0, s * 54, 228), axis="y"), solid=False)
    m.mesh(torso, "glowBlue", cyl(30.7, 1.8, at=(0, s * 82, 231), sides=28), solid=False)
    m.mesh(torso, "hullMid", [bar((8, s * 62, 258), (8, s * 82, 258), 5, shape="cyl")], solid=False)
# cable bundles: clamps and a second pair along the flanks
m.mesh(torso, "hullMid", [cyl(4.2, 3, at=(40.5 - 4 * k, s * (20 - 5 * k), 192 - 4 * k), axis="x", rot=(0, 0, 0), sides=10)
                          for k in range(2) for s in (-1, 1)], solid=False)
m.mesh(torso, "hullDark", [bar((50, s * 40, 196), (36, s * 52, 170), 5.5, shape="cyl") for s in (-1, 1)], solid=False)
# hazard wedge on the brow of the front plate
m.mesh(torso, "hazard", [box((10, 22, 2.4), at=(58, s * 32, 241.2)) for s in (-1, 1)], solid=False)

vents = m.part("vents", parent=torso, at=(-58, 0, 222),
               driver=d("heat vents: glow after each volley", ("emission",)))
# two framed vents on the back: glowing slats in a dark frame
m.mesh(torso, "hullDark", [box((3.2, 38, 33), at=(-59.2, s * 26, 224)) for s in (-1, 1)] +
       [box((3.2, 4, 33), at=(-59.2, s * 26 + o, 224)) for s in (-1, 1) for o in (-17, 17)], solid=False)
m.mesh(vents, "ventGlow", [box((3, 30, 3), at=(-60.2, s * 26, 212.4 + 5.2 * k)) for s in (-1, 1) for k in range(6)] +
       [box((14, 3, 18), at=(30, s * 67, 214)) for s in (-1, 1)], solid=False)
m.mesh(torso, "hullDark", [box((18, 2.5, 22), at=(30, s * 66.4, 214)) for s in (-1, 1)], solid=False)
# stacks' smoke sockets
for i, s in enumerate((-1, 1)):
    m.part("smoke_%d" % i, parent=torso, at=(-36, s * 30, 298),
           driver=dict(what="stack smoke socket: a thin grey plume while it walks and fires", channels=["particles"],
                       functions=[WALK]),
           particles=[dict(name="smoke", kind="niagara", direction=[0, 0, 1])])

lamp = m.part("searchlight", parent=torso, at=(60, 0, 202),
              driver=d("searchlight: on at night, swings with the torso", ("emission", "rotation")))
m.mesh(lamp, "hullDark", [cyl(14, 6, at=(60, 0, 202), axis="x"), cyl(15.6, 3, at=(58.5, 0, 202), axis="x")])
m.mesh(lamp, "chrome", cyl(12.2, 1.2, at=(63.2, 0, 202), axis="x"), solid=False)
m.mesh(lamp, "floodGlow", cyl(10.5, 4, at=(64, 0, 202), axis="x"))

# --- cannons: housing and barrel pitch together about the trunnion --------------------------------------------
for i, s in enumerate((-1, 1)):
    c = m.part("cannons_%d" % i, parent=torso, at=(0, s * 42, 270),
               driver=d("cannon pitch and recoil along the barrel", ("rotation", "position")))
    m.mesh(c, "hullDark", [box((54, 32, 30), at=(4, s * 42, 270)),
                           cyl(7, 112, at=(31, s * 42, 270), axis="x", base=True)])
    m.mesh(c, "hullMid", [box((24, 38, 12), at=(-4, s * 42, 290)),
                          cyl(9.5, 16, at=(129, s * 42, 270), axis="x", base=True)])
    m.mesh(c, "chrome", cyl(6, 6, at=(145, s * 42, 270), axis="x", base=True))
    # barrel rings, vent slats on the housing's top and flanks, a muzzle bore and ring
    m.mesh(c, "hullMid", [cyl(9.0, 4, at=(x, s * 42, 270), axis="x") for x in (62, 84, 106)], solid=False)
    m.mesh(c, "hullMid", [box((16, 1.6, 1.4), at=(8 + 0 * k, s * 42 + (k - 2) * 5.4, 286)) for k in range(5)] +
           [box((20, 1.2, 1.6), at=(10, s * 42 + 16.2 * t, 270 + o)) for t in (-1, 1) for o in (-6, 0, 6)], solid=False)
    m.mesh(c, "hullDark", [box((8, 30, 3), at=(-28, s * 42, 282)), cyl(5.4, 0.7, at=(150.8, s * 42, 270), axis="x")], solid=False)
    m.mesh(c, "chrome", cyl(10.4, 2.4, at=(139, s * 42, 270), axis="x"), solid=False)

# --- legs: hip, knee (forward bend), ankle, foot ----------------------------------------------------------------
HIP, KNEE, ANKLE = (0.0, 150.0), (26.0, 88.0), (-2.0, 24.0)   # (x, z) in the leg's plane
for i, s in enumerate((-1, 1)):
    y = s * 60
    hip = m.part("hips_%d" % i, parent=body, at=(HIP[0], y, HIP[1]), driver=d("hip swing of the walk cycle"))
    m.mesh(hip, "hullDark", [cyl(16, 30, at=(HIP[0], y, HIP[1]), axis="y"), sphere(18, at=(HIP[0], y, HIP[1]))])
    m.mesh(hip, "hullLight", bar((HIP[0], y, HIP[1]), (KNEE[0], y, KNEE[1]), 28, 24))
    m.mesh(hip, "team", box((8, 30, 34), at=(KNEE[0] - 10 + 12, y, 118), rot=(-20, 0, 0)))
    # hydraulic piston on the thigh's inner side: sleeve and rod
    m.mesh(hip, "hullDark", bar((-12, y, 144), (10, y, 104), 6.4, shape="cyl"), solid=False)
    m.mesh(hip, "chrome", bar((10, y, 104), (17, y, 93), 3.4, shape="cyl"), solid=False)

    knee = m.part("knees_%d" % i, parent=hip, at=(KNEE[0], y, KNEE[1]), driver=d("knee bend of the walk cycle"))
    m.mesh(knee, "hullDark", cyl(15, 32, at=(KNEE[0], y, KNEE[1]), axis="y"))
    m.mesh(knee, "hullLight", bar((KNEE[0], y, KNEE[1]), (ANKLE[0], y, ANKLE[1] + 4), 26, 22))
    m.mesh(knee, "team", [box((14, 32, 22), at=(KNEE[0] + 14, y, KNEE[1] + 2)),       # knee guard
                          box((8, 30, 38), at=(KNEE[0] + 8, y, 56), rot=(18, 0, 0))])   # shin plate
    m.mesh(knee, "hullDark", bar((12, y, 80), (-4, y, 46), 6.0, shape="cyl"), solid=False)
    m.mesh(knee, "chrome", bar((-4, y, 46), (-8, y, 37), 3.2, shape="cyl"), solid=False)
    m.mesh(knee, "hullMid", [cyl(8, 3, at=(KNEE[0], y + s * 17, KNEE[1]), axis="y", sides=12)], solid=False)
    kl = m.part("kneeLamps_%d" % i, parent=knee, at=(KNEE[0], y + s * 18, KNEE[1]),
                driver=d("amber knee lamps: pulse with the step and the stomp", ("emission",)))
    m.mesh(kl, "glowOrange", [box((7, 5, 11), at=(KNEE[0] + 2, y + s * 19, KNEE[1])),
                              box((7, 4, 8), at=(KNEE[0] + 6, y + s * 17, 42))])

    ankle = m.part("ankles_%d" % i, parent=knee, at=(ANKLE[0], y, ANKLE[1]), driver=d("ankle counter-turn of the walk cycle"))
    m.mesh(ankle, "hullDark", cyl(12, 28, at=(ANKLE[0], y, ANKLE[1]), axis="y"))
    m.mesh(ankle, "hullMid", box((30, 30, 12), at=(ANKLE[0], y, ANKLE[1] - 10)))

    foot = m.part("feet_%d" % i, parent=ankle, at=(ANKLE[0], y, 14), driver=d("foot lift and plant of the walk cycle"))
    m.mesh(foot, "hullMid", [box((26, 36, 12), at=(-13, y, 6))])        # heel
    m.mesh(foot, "hullDark", [box((7, 12, 8), at=(-28.5, y, 4)), box((16, 40, 3), at=(-14, y, 12.8))], solid=False)   # the heel toe, ankle plate
    toes = []
    for yaw in (-24, 0, 24):
        cx, cy = 8 + 24 * math.cos(math.radians(yaw)), y + 24 * math.sin(math.radians(yaw))
        toes.append(box((48, 16, 10), at=(cx, cy, 5), rot=(0, yaw, 0)))
    m.mesh(foot, "hullMid", toes)
    m.mesh(foot, "hullDark", [box((10, 14, 10), at=(8 + 50 * math.cos(math.radians(yaw)), y + 50 * math.sin(math.radians(yaw)), 5),
                                  rot=(0, yaw, 0)) for yaw in (-24, 0, 24)])

m.write()
