"""Scorpion: the burrowing mine (docs/new-units.md, "Looks"; concept art/models/scorpion/concepts/scorpion-c1-scorpion.jpg).
Blockout refined at step 5 (2026-10-05) to painted-v1 and the scorpion concept: the painting is fatter than the
blockout everywhere (tail, launcher, legs, belly), so every limb is thicker; curved overlapping back plates with
light strips, head with four red eyes and mandibles, jointed legs with steel knobs, pincers with claws, ringed
tail, launcher with muzzle ring and red lamps. Part names and pivots are the blockout's.

    UnrealEditorBG <abs>/unreal/Autocraft.uproject -run=pythonscript \
        -script=<abs>/unreal/Tools/Editor/models/scorpion.py -unattended -nullrhi -nosplash -nosound -abslog=<log>

Writes /Game/Models/scorpion/SM_<part>__<material> and the catalog entry `scorpion_blue`.

UE cm, +X forward, +Y right, Z up, the feet on Z = 0; the size of a Prospector (about 150 long with the pincers, 135
wide over the feet, 135 high at the launcher). The legs are numbered front to back, 0..2 on the left (-Y) and
3..5 on the right; `shins_i` is the lower leg under `legs_i`. The tail's four segments chain (tail_0 > tail_1 >
tail_2 > tail_3 > launcher > tailLamp). `mound` is the heap of soil that shows when it is buried: hidden at rest.
"""
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from ac_modelkit import Model, box, cyl, cone, sphere, bar, loft, ring_box, prism  # noqa: E402

POSE = "AcPoseVehicles.cpp:PoseScorpion"

m = Model("scorpion", category="unit", notes=[
    "Burrowing mine, Prospector-sized. Pose: PoseScorpion (to write): insect gait three legs at a time, burying (body sinks, "
    "plates slide, mound shows), tail curls up to aim and snaps to fire, tail lamp blinks while reloading.",
    "Parts: legs_0..5 + shins_0..5, tail_0..3 + launcher + tailLamp, plates_0..4, pincers_0/1, eyes, mound (hidden)."])
for name in ("hullLight", "hullMid", "hullDark", "team", "chrome", "glowRed", "glowBlue"):
    m.material(name)
m.material("soil", parent="hull", color=(0.17, 0.10, 0.055), metallic=0.0, roughness=0.95)


def d(what, channels=("rotation",)):
    return dict(what=what, channels=list(channels), functions=[POSE])


Z = 40.0  # the body axis
# --- body: flat, segmented back --------------------------------------------------------------------------------
body = m.part("body", at=(0, 0, Z), driver=d("sinks into the ground when it buries; bob of the gait", ("position", "rotation")))
sections = [(64, 15, 8), (46, 26, 12), (4, 31, 13), (-40, 27, 12), (-58, 15, 8)]
m.mesh(body, "hullDark", loft([ring_box(x, w, h, 0, Z, 12, 3.0) for x, w, h in sections]))
m.mesh(body, "hullDark", box((24, 30, 14), at=(66, 0, Z - 2)))                       # head block
m.mesh(body, "hullMid", [bar((70, -9, Z - 6), (84, -5, Z - 14), 6, shape="cyl"),     # mandibles
                         bar((70, 9, Z - 6), (84, 5, Z - 14), 6, shape="cyl")])
m.mesh(body, "hullMid", [box((6, 26, 3), at=(77, 0, Z + 6.5)), box((4, 8, 5), at=(78.5, 0, Z - 5))], solid=False)  # brow, snout
# rim of the belly and a rear hatch, rivets along the flanks
m.mesh(body, "hullMid", [box((70, 2, 3), at=(-6, s * 29.6, Z - 5)) for s in (-1, 1)], solid=False)
m.mesh(body, "chrome", [sphere(1.5, at=(x, s * 31.4, Z - 1), steps=6) for x in (30, 14, -2, -18, -34) for s in (-1, 1)], solid=False)
m.mesh(body, "hullDark", cyl(13, 7, at=(-52, 0, Z + 10)), solid=False)               # tail socket

# plates: five overlapping segments of the armoured back
PLATES = [(40, 44, 20), (20, 52, 22), (0, 56, 22), (-20, 52, 22), (-40, 42, 20)]  # x, width, length
for i, (x, w, ln) in enumerate(PLATES):
    p = m.part("plates_%d" % i, parent=body, at=(x - ln / 2, 0, Z + 9),
               driver=d("back plate: slides and lifts when it buries or digs out", ("position", "rotation")))
    # a curved shell: rounded in section, thinner at the edges, the rear edge lifted over the next plate
    m.mesh(p, "team", loft([ring_box(x + ln / 2, w * 0.9, 3.4, 0, Z + 11.6, 12, 2.4), ring_box(x + ln * 0.15, w * 0.99, 4.4, 0, Z + 12.4, 12, 2.4),
                            ring_box(x - ln / 2, w, 3.4, 0, Z + 12.6, 12, 2.4)]))
    m.mesh(p, "hullLight", box((2.6, w - 6, 7.2), at=(x - ln / 2 + 0.6, 0, Z + 12.2)))
    m.mesh(p, "glowBlue", box((1.7, w * 0.72, 1.2), at=(x + ln * 0.18, 0, Z + 16.9)), solid=False)

eyes = m.part("eyes", parent=body, at=(76, 0, Z + 4), driver=d("sensor eyes: red, brighter when it hunts", ("emission",)))
m.mesh(eyes, "glowRed", [sphere(3.6, at=(77.5, s * 8, Z + 3)) for s in (-1, 1)] + [sphere(2.4, at=(77, s * 3.5, Z + 8)) for s in (-1, 1)])

# --- six legs, three a side, upper and lower pair ---------------------------------------------------------------------
for k in range(6):
    s = -1 if k < 3 else 1
    row = k % 3
    x = (30, 0, -30)[row]
    lean = (16, 2, -14)[row]             # the feet reach forward in front, back behind
    hip = (x, s * 24, Z + 4)
    knee = (x + lean * 0.5, s * 50, 62)
    foot = (x + lean, s * 72, 0)
    leg = m.part("legs_%d" % k, parent=body, at=hip, driver=d("leg swing of the insect gait (three legs at a time)"))
    m.mesh(leg, "hullDark", bar(hip, knee, 14, 12))
    m.mesh(leg, "chrome", sphere(9, at=hip, steps=10))
    sh = m.part("shins_%d" % k, parent=leg, at=knee, driver=d("lower leg: knee bend of the gait"))
    m.mesh(sh, "chrome", sphere(8.5, at=knee, steps=10))
    m.mesh(sh, "hullDark", bar(knee, (knee[0] + (foot[0] - knee[0]) * 0.22, knee[1] + (foot[1] - knee[1]) * 0.22, knee[2] * 0.78), 14, 12))
    m.mesh(sh, "hullMid", bar(knee, foot, 13.5, 11, taper=0.3))

# --- pincers: arm and claw ----------------------------------------------------------------------------------------------
for i, s in enumerate((-1, 1)):
    sh = (52, s * 22, Z + 2)
    elbow = (72, s * 42, Z - 2)
    wrist = (92, s * 30, Z - 4)
    p = m.part("pincers_%d" % i, parent=body, at=sh, driver=d("pincer: opens, closes and digs", ("rotation",)))
    m.mesh(p, "hullDark", [bar(sh, elbow, 12, 11, shape="cyl"), sphere(8, at=elbow)])
    m.mesh(p, "chrome", sphere(8.5, at=sh, steps=10))
    m.mesh(p, "team", box((26, 19, 13), at=(wrist[0] - 4, wrist[1], wrist[2]), rot=(0, -s * 10, 0)))
    m.mesh(p, "hullMid", [bar((wrist[0] + 4, wrist[1], wrist[2]), (wrist[0] + 28, wrist[1] - s * 12, wrist[2] - 2), 7, 11, taper=0.3),
                          bar((wrist[0] + 4, wrist[1], wrist[2]), (wrist[0] + 28, wrist[1] + s * 9, wrist[2] - 2), 7, 11, taper=0.3)])
    m.mesh(p, "chrome", [sphere(3.0, at=(wrist[0] + 28, wrist[1] - s * 12, wrist[2] - 2)),
                         sphere(3.0, at=(wrist[0] + 28, wrist[1] + s * 9, wrist[2] - 2))])

# --- the tail: four segments chained, a launcher at the tip, a red lamp -------------------------------------------------
P = [(-52, 0, Z + 12), (-72, 0, 84), (-64, 0, 114), (-38, 0, 130), (-14, 0, 123)]
parent = body
for k in range(4):
    t = m.part("tail_%d" % k, parent=parent, at=P[k],
               driver=d("tail: curls up to aim and snaps forward to fire", ("rotation",)))
    w = 25 - 2 * k
    seg = (P[k + 1][0] - P[k][0], 0, P[k + 1][2] - P[k][2])
    ln = math.hypot(seg[0], seg[2])
    m.mesh(t, "team", bar(P[k], P[k + 1], w, w + 6, taper=0.9))
    m.mesh(t, "hullDark", sphere(w * 0.62, at=P[k], steps=10))
    # a steel ring band a third of the way up the segment, a worn silver edge strip along its top
    q = tuple(P[k][i] + seg[i] * 0.55 for i in range(3))
    q2 = tuple(q[i] + seg[i] / ln * 2.6 for i in range(3))
    m.mesh(t, "hullMid", bar(q, q2, (w + 6) * 0.98, w * 0.98 + 4.4, taper=None), solid=False)
    parent = t
tip = P[4]
launcher = m.part("launcher", parent=parent, at=tip, driver=d("charge launcher: aims at the target", ("rotation",)))
m.mesh(launcher, "hullDark", [cyl(14, 50, at=(tip[0] + 22, 0, tip[2] - 5), axis="x", rot=(-12, 0, 0)),
                              sphere(14.5, at=tip)])
m.mesh(launcher, "chrome", cyl(15.8, 5, at=(tip[0] + 45, 0, tip[2] - 10), axis="x", rot=(-12, 0, 0)))
m.mesh(launcher, "hullMid", [cyl(14.8, 3, at=(tip[0] + x, 0, tip[2] - 5 - (x - 22) * 0.22), axis="x", rot=(-12, 0, 0)) for x in (6, 30)], solid=False)
m.mesh(launcher, "glowBlue", [box((16, 2, 3), at=(tip[0] + 20, s * 13.8, tip[2] - 5.5)) for s in (-1, 1)], solid=False)
lamp = m.part("tailLamp", parent=launcher, at=(tip[0] + 47, 0, tip[2] - 10),
              driver=d("tail lamp: blinks slowly while reloading, steady when ready", ("emission",)))
m.mesh(lamp, "glowRed", [cyl(10.8, 3, at=(tip[0] + 48, 0, tip[2] - 10.5), axis="x", rot=(-12, 0, 0)),
                         cyl(9.0, 3, at=(tip[0] - 4, 0, tip[2] - 3.5), axis="x", rot=(-12, 0, 0))])
m.mesh(launcher, "hullDark", cyl(5.2, 0.8, at=(tip[0] + 49.6, 0, tip[2] - 10.9), axis="x", rot=(-12, 0, 0)), solid=False)

# --- the mound (hidden at rest): the soil heaped over the buried body ------------------------------------------------------
mound = m.part("mound", at=(0, 0, 0), hidden=True, driver=d("heap of disturbed soil, shown while it is buried", ("visibility", "scale")))
lumps = [sphere(58, at=(0, 0, 4), squash=(1.45, 1.15, 0.26))]
for k in range(9):
    a = 2 * math.pi * k / 9 + 0.3
    r = 52 + 12 * ((k * 7) % 3)
    lumps.append(sphere(11 + (k % 3) * 3, at=(r * math.cos(a) * 1.05, r * math.sin(a) * 0.9, 7), squash=(1, 1, 0.55)))
for k in range(8):
    a = 2 * math.pi * k / 8 + 0.9
    lumps.append(sphere(6 + (k % 4) * 2, at=(30 * math.cos(a) * 1.3, 30 * math.sin(a) * 1.1, 11), squash=(1.2, 1, 0.6)))
m.mesh(mound, "soil", lumps)

m.write()
