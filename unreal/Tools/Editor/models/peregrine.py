"""Peregrine: the air-superiority fighter (docs/new-units.md, "Looks"; concept
art/models/peregrine/concepts/peregrine-c1-dart.jpg). BLOCKOUT: primitives at the right size, the part tree and
the pivots. Step 5 (2026-10-05) refined it to painted-v2 and the dart concept: pitot, teardrop canopy with
frame ribs, panel steps and flank light strips, intakes with lips, silver leading edges, wing-tip blocks, missiles
with fins, engine rings, fins with silver edge and gunmetal base. Part names and pivots are the blockout's.

    UnrealEditorBG <abs>/unreal/Autocraft.uproject -run=pythonscript \
        -script=<abs>/unreal/Tools/Editor/models/peregrine.py -unattended -nullrhi -nosplash -nosound -abslog=<log>

Writes /Game/Models/peregrine/SM_<part>__<material> and the catalog entry `peregrine_blue`.

UE cm, +X the nose, +Y the right, Z up; the fuselage axis is at Z0. About 320 cm nose to exhaust (a Ranger is
about 120 long and 115 tall in the catalog). `_0` is the left (-Y), `_1` the right (+Y).
"""
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from ac_modelkit import Model, box, cyl, cone, sphere, prism, loft, ring_box, ring_ellipse  # noqa: E402

Z0 = 50.0
POSE = "AcPoseAir.cpp:PosePeregrine"

m = Model("peregrine", category="unit", notes=[
    "Light flyer, about 320 cm long with the needle nose; the pose function is PosePeregrine (to write).",
    "Parts: flaps_0/1, fins_0/1, engines_0/1 (+ glow, glow_2 and emitter_1/2 sockets), missiles_0..3, beacon_0/1."])
for name in ("hullLight", "hullMid", "hullDark", "team", "chrome", "glowCold", "glowRed", "glowBlue"):
    m.material(name)
m.material("canopy", parent="hull", color=(0.012, 0.018, 0.03), metallic=0.9, roughness=0.08)


def d(left, right=None):
    """a driver block"""
    return dict(what=left, channels=right or ["rotation"], functions=[POSE])


# --- body: the needle, the fuselage, the canopy, the wings, rails, tip pods ---------------------
body = m.part("body", at=(0, 0, Z0), driver=d("bank, pitch and bob of the airframe", ["position", "rotation"]))

# (x, half width, half height, centre height, power): needle nose first, a rounded box at the back
sections = [(195, 0.6, 0.6, 0, 2.0), (165, 2.2, 2.2, 0, 2.0), (125, 4.6, 4.2, 0, 2.2), (88, 8, 7, 0, 2.4),
            (52, 13, 10, 1, 2.8), (18, 20, 13, 2, 3.2), (-30, 28, 15, 1, 3.6), (-80, 30, 15, 0, 3.8), (-102, 27, 14, 0, 3.8)]
m.mesh(body, "hullLight", loft([ring_box(x, w, h, 0, Z0 + cz, 12, p) for x, w, h, cz, p in sections]))
# pitot at the needle's tip, dark seams round the nose
m.mesh(body, "hullDark", cone(1.15, 0.3, 14.2, at=(188, 0, Z0), axis="x"))
for x, w, h in ((165, 2.2, 2.2), (125, 4.6, 4.2), (88, 8, 7), (60, 11.5, 9)):
    m.mesh(body, "hullDark", cyl(max(w, h) * 1.04, 1.4, at=(x, 0, Z0 + (1 if x < 70 else 0)), axis="x", sides=14), solid=False)

# teardrop canopy: a loft of ellipses, a base plate and three hoop ribs in a thin gunmetal frame
CX, CHL, CWID, CHT, CZ = 14.0, 28.0, 9.5, 7.6, Z0 + 14.0
def canopy_ring(x, k=1.0, pad=0.0):
    t = (x - CX) / CHL
    s_ = max(math.sqrt(max(1 - t * t, 0.0)), 0.1)
    hh = CHT * (1.0 if x < CX else 0.8 + 0.2 * (1 - t))
    return ring_ellipse(x, (CWID * s_ + pad) * k, (hh * s_ + pad) * k, 0, CZ, 14)
m.mesh(body, "canopy", loft([canopy_ring(x) for x in (-13.5, -8, 0, 10, 20, 30, 37, 41.5)]))
m.mesh(body, "hullDark", [loft([canopy_ring(x - 0.9, 1.0, 0.5), canopy_ring(x + 0.9, 1.0, 0.5)]) for x in (30, 14, -2)], solid=False)
m.mesh(body, "hullDark", [prism([(CX + CHL * 0.98 * math.cos(2 * math.pi * k / 20), CWID * 1.02 * math.sin(2 * math.pi * k / 20)) for k in range(20)],
                                1.6, "xy", at=(0, 0, Z0 + 15.2)),
                          box((46, 1.5, 1.2), at=(14, 0, Z0 + 21.4))], solid=False)
# dark intakes on the flanks with a steel lip, a cyan light strip under each
for s in (-1, 1):
    m.mesh(body, "hullDark", box((30, 9, 11), at=(-14, s * 24, Z0 + 2)))
    m.mesh(body, "hullMid", [box((34, 12, 2.2), at=(-14, s * 24, Z0 + 8.2)), box((34, 12, 2.2), at=(-14, s * 24, Z0 - 4.2)),
                             box((2.4, 12, 14), at=(-30.5, s * 24, Z0 + 2))], solid=False)
    m.mesh(body, "glowBlue", [box((32, 1.3, 2.0), at=(-14, s * 29.6, Z0 - 6.8)),
                              box((22, 1.3, 1.6), at=(54, s * 13.2, Z0 - 3.5))], solid=False)
    m.mesh(body, "hullDark", [box((10, 6, 1.0), at=(40, s * 12, Z0 + 11.2))], solid=False)
# panel steps on the upper fuselage: deep blue deck plates, a second one stepped up, vent slots
m.mesh(body, "team", [box((46, 38, 2.4), at=(-62, 0, Z0 + 16.3)), box((26, 24, 2.4), at=(-76, 0, Z0 + 18.6)),
                      box((24, 11, 2.0), at=(72, 0, Z0 + 9.4)), box((34, 15, 2.0), at=(46, 0, Z0 + 13.2))], solid=False)
m.mesh(body, "hullDark", [box((1.3, 14, 0.8), at=(-66 - 5 * k, 0, Z0 + 20.1)) for k in range(4)] +
       [box((1.3, 10, 0.8), at=(-50, s * 12, Z0 + 17.7)) for s in (-1, 1)], solid=False)
# the keel: a dark gunmetal blade underneath
m.mesh(body, "hullDark", [box((84, 14, 5), at=(-40, 0, Z0 - 13.5)), box((30, 10, 4), at=(30, 0, Z0 - 9.0))])

# swept-back wings (blue) with a silver leading edge and a gunmetal-and-steel tip block each
for s in (-1, 1):
    wing = [(10, 12), (-55, 112), (-100, 112), (-100, 85), (-78, 85), (-78, 35), (-100, 35), (-100, 12)]
    m.mesh(body, "team", prism([(x, s * y) for x, y in wing], 5, "xy", at=(0, 0, Z0 - 4)))
    edge = [(10, 12), (-55, 112), (-66, 112), (-3, 12)]
    m.mesh(body, "hullLight", prism([(x, s * y) for x, y in edge], 5.8, "xy", at=(0, 0, Z0 - 4)))
    # panel lines on the wing top: thin dark strips along the chord and across it
    m.mesh(body, "hullDark", [box((52, 0.9, 0.5), at=(-40, s * 60, Z0 - 1.3)), box((0.9, 58, 0.5), at=(-60, s * 62, Z0 - 1.3))], solid=False)
    # wing-tip pod with its pointed nose, and the rail with its pylons under the wing
    m.mesh(body, "hullMid", [cyl(4.5, 36, at=(-72, s * 112, Z0 - 4), axis="x"),
                             cone(4.5, 0.8, 18, at=(-54, s * 112, Z0 - 4), axis="x", base=True)])
    m.mesh(body, "hullDark", [box((26, 5.4, 11), at=(-84, s * 112, Z0 - 3)), box((12, 5.8, 2.5), at=(-96, s * 112, Z0 + 3.5)),
                              box((62, 3, 3), at=(-52, s * 65, Z0 - 9)),
                              box((6, 3, 7), at=(-30, s * 65, Z0 - 8)), box((6, 3, 7), at=(-72, s * 65, Z0 - 8)),
                              box((3, 22, 3), at=(-21, s * 65, Z0 - 9)), box((4, 22, 3), at=(-83, s * 65, Z0 - 9))])

# --- flaps: the notch in each wing's trailing edge; pivot on the hinge line ------------------------
for i, s in enumerate((-1, 1)):
    f = m.part("flaps_%d" % i, parent=body, at=(-78, s * 60, Z0 - 4),
               driver=d("wing flap: pitch with the bank and the turn"))
    m.mesh(f, "team", prism([(-22, s * -24), (0, s * -24), (0, s * 24), (-22, s * 24)], 4, "xy", at=(-78, s * 60, Z0 - 4)))
    m.mesh(f, "hullLight", box((2, 48, 4.6), at=(-99, s * 60, Z0 - 4)), solid=False)
    m.mesh(f, "hullDark", box((2.4, 48, 4.4), at=(-78.8, s * 60, Z0 - 4)), solid=False)

# --- fins: tall, canted outward, hinged at the root --------------------------------------------------
for i, s in enumerate((-1, 1)):
    f = m.part("fins_%d" % i, parent=body, at=(-78, s * 30, Z0 + 12),
               driver=d("tail fin: rudder swing with the turn"))
    m.mesh(f, "team", prism([(22, 0), (-38, 0), (-54, 66), (-28, 66)], 3.5, "xz", at=(-78, s * 30, Z0 + 12), rot=(0, 0, s * 20)))
    m.mesh(f, "hullLight", prism([(22, 0), (-28, 66), (-35, 66), (14, 0)], 4.4, "xz", at=(-78, s * 30, Z0 + 12), rot=(0, 0, s * 20)))
    m.mesh(f, "hullDark", prism([(24, 0), (-38, 0), (-40.5, 11), (13.5, 11)], 5.6, "xz", at=(-78, s * 30, Z0 + 12), rot=(0, 0, s * 20)))

# --- engines: two nacelles side by side, a cold exhaust disc each -----------------------------------
for i, s in enumerate((-1, 1)):
    e = m.part("engines_%d" % i, parent=body, at=(-80, s * 22, Z0),
               driver=d("engine nacelle: shake and gimbal with the thrust", ["rotation", "position"]))
    m.mesh(e, "hullMid", [cyl(17, 76, at=(-67, s * 22, Z0), axis="x"),
                          cone(17, 13.5, 24, at=(-105, s * 22, Z0), axis="x", base=False)])
    m.mesh(e, "hullDark", cyl(14.5, 10, at=(-27, s * 22, Z0), axis="x"))
    m.mesh(e, "chrome", cyl(15, 3, at=(-118, s * 22, Z0), axis="x"))
    m.mesh(e, "hullLight", [cyl(17.8, 3, at=(x, s * 22, Z0), axis="x") for x in (-44, -66, -92)], solid=False)
    m.mesh(e, "hullDark", [cyl(13.6, 5, at=(-115.5, s * 22, Z0), axis="x"), cyl(17.4, 1.5, at=(-55, s * 22, Z0), axis="x")], solid=False)
    glow = m.part("glow" if i == 0 else "glow_2", parent=e, at=(-129, s * 22, Z0),
                  driver=dict(what="exhaust glow: scale and brightness with speed", channels=["scale", "emission"],
                              functions=[POSE]))
    m.mesh(glow, "glowCold", cyl(12.5, 3, at=(-129, s * 22, Z0), axis="x"))
    m.part("emitter_%d" % (i + 1), parent=e, at=(-132, s * 22, Z0),
           driver=dict(what="vapour trail and exhaust particles socket", channels=["particles"], functions=[POSE]),
           particles=[dict(name="exhaust", kind="niagara", direction=[-1, 0, 0])])

# --- missiles: two under each wing, each its own part so it can leave the rail ------------------------
for i, y in enumerate((-74, -56, 56, 74)):
    ms = m.part("missiles_%d" % i, parent=body, at=(-50, y, Z0 - 17),
                driver=d("seeker missile: hides on launch, returns when reloaded", ["visibility", "position"]))
    m.mesh(ms, "hullLight", [cyl(3.2, 44, at=(-57, y, Z0 - 17), axis="x"),
                             cone(3.2, 1.6, 11, at=(-29.5, y, Z0 - 17), axis="x")])
    m.mesh(ms, "hullDark", [cyl(3.3, 4, at=(-79, y, Z0 - 17), axis="x"), cone(1.6, 0.35, 4.6, at=(-21.7, y, Z0 - 17), axis="x"),
                            cyl(3.35, 1.6, at=(-46, y, Z0 - 17), axis="x"),
                            prism([(-74, 0), (-79, 6.5), (-79, 0)], 0.8, "xy", at=(0, y, Z0 - 17)),
                            prism([(-74, 0), (-79, 0), (-79, -6.5)], 0.8, "xy", at=(0, y, Z0 - 17)),
                            prism([(-74, 0), (-79, 6.5), (-79, 0)], 0.8, "xz", at=(0, y, Z0 - 17)),
                            prism([(-74, 0), (-79, 0), (-79, -2.8)], 0.8, "xz", at=(0, y, Z0 - 17))], solid=False)

# --- wing-tip beacons (red, blink) -----------------------------------------------------------------------
for i, s in enumerate((-1, 1)):
    b = m.part("beacon_%d" % i, parent=body, at=(-52, s * 112, Z0 - 4),
               driver=dict(what="red wing-tip beacon: blinks", channels=["emission", "visibility"], functions=[POSE]))
    m.mesh(b, "glowRed", sphere(3.2, at=(-52, s * 112, Z0 - 4)))

m.write()
