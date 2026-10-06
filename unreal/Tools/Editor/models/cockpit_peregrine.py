"""Cockpit of the Peregrine (`cockpit_peregrine_blue`): the view from behind its small canopy, at the hover.

    UnrealEditorBG <abs>/unreal/Autocraft.uproject -run=pythonscript \
        -script=<abs>/unreal/Tools/Editor/models/cockpit_peregrine.py -unattended -nullrhi -nosplash -nosound -abslog=<log>

Contract (AcCockpitKinds.cpp, PosePeregrineCockpit): rig > level > model_root > model_<unit part>; muzzles_0 and
muzzles_1 under model_body. The eye (AcPilotCamera::Eye) is 30 cm ahead of the body's pivot and 19 cm above it, in
the front of the canopy (x -14..42): the canopy of the unit model is left out (the eye is inside it) and a thin
canopy frame is built on `rig` instead, round the driver; the needle nose runs ahead down the middle, the wings,
flaps, beacons and the missile rails come from the unit model. The two deck plates on the nose (x 46 and 72) are
left out too: they would lie right under the eye.
"""
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from ac_cockpitkit import make, copy_parts, need_parents, cam  # noqa: E402
from ac_modelkit import box, bar  # noqa: E402

Z0 = 50.0
cm, u, rig, root = make("peregrine", "peregrine", [
    "First-person view model hung off the camera while the peregrine is driven: the unit's nose, wings, flaps, beacons "
    "and missiles under the eye (model_*), a canopy frame on rig, muzzles_0/1 ahead of the rails."])
for n in ("hullDark", "hullMid", "team", "glowBlue", "hud"):
    cm.material(n)


def in_canopy(name, mat, p):
    """The unit's canopy glass, its ribs, base plate and spine: the eye sits in them."""
    if name != "body":
        return True
    if mat == "team" and p.kind == "box" and abs(p.at[1]) < 1 and p.at[0] in (46.0, 72.0):
        return False
    pts = p.points()
    return not (all(q[2] > Z0 + 13.5 for q in pts) and all(-16 < q[0] < 44 for q in pts) and all(abs(q[1]) < 12 for q in pts)) \
        and mat != "canopy"


names = need_parents(u, ["body", "flaps_0", "flaps_1", "beacon_0", "beacon_1", "missiles_0", "missiles_1", "missiles_2", "missiles_3"])
made = copy_parts(cm, u, root, names, keep=in_canopy)
body = made["body"]
# the rounds leave ahead of the rails, on the same lines of sight: model-frame points ahead of the eye
for i, s in enumerate((-1, 1)):
    cm.part("muzzles_%d" % i, parent=body, at=(70, s * 72, Z0 - 17),
            driver=dict(what="where the missiles leave", channels=["position"], functions=["AcCockpitKinds.cpp:PosePeregrineCockpit"]))

# --- the canopy frame on rig: bows over the driver, side rails, a sill in team blue (camera frame, cm) --------------
def arc(a, rw, up0, up1, n=8):
    """points on a half ellipse from (-rw, up0) over the top (up1) to (rw, up0) at distance `a` ahead"""
    return [cam(a, -rw * math.cos(math.pi * k / n), up0 + (up1 - up0) * math.sin(math.pi * k / n)) for k in range(n + 1)]


bows = [arc(14, 17, -9, 12), arc(4, 27, -9, 17)]
for pts in bows:
    for p, q in zip(pts, pts[1:]):
        cm.mesh(rig, "hullDark", bar(p, q, 1.6, 2.2), solid=False)
for s in (-1, 1):
    cm.mesh(rig, "hullDark", bar(cam(3, s * 28, -9.5), cam(14, s * 17, -9.5), 1.6, 2.2), solid=False)          # side rails
    cm.mesh(rig, "hullMid", bar(cam(4, s * 27, -8.6), cam(14, s * 16.5, -8.6), 0.8, 0.8), solid=False)         # steel edge
    cm.mesh(rig, "team", box((7, 18, 1.6), at=cam(9, s * 24, -10.4)), solid=False)                            # sill
cm.mesh(rig, "glowBlue", [box((6, 0.8, 0.6), at=cam(9, s * 22, -9.4)) for s in (-1, 1)], solid=False)
cm.write()
