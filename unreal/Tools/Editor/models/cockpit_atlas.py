"""Cockpit of the Atlas (`cockpit_atlas_blue`): the view through the torso's cockpit slit, the cannons ahead.

    UnrealEditorBG <abs>/unreal/Autocraft.uproject -run=pythonscript \
        -script=<abs>/unreal/Tools/Editor/models/cockpit_atlas.py -unattended -nullrhi -nosplash -nosound -abslog=<log>

Contract (AcCockpitKinds.cpp, PoseAtlasCockpit): rig > level > model_root > model_body > model_torso >
model_cannons_0/1, muzzles_0/1 under the cannons. The eye is in the slit (55 cm ahead of the torso's axis, 258 cm up, just under the cannons): a slab with a slit is built on `rig`, a heavy brow above (the view's top 7 degrees) and a sill below (its bottom 10),
the two barrels running ahead in the slit's upper corners; the cannons and the top plate (hazard stripes, ring mounts) come from the unit.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from ac_cockpitkit import make, copy_parts, need_parents, cam  # noqa: E402
from ac_modelkit import box, sphere  # noqa: E402

cm, u, rig, root = make("atlas", "atlas", [
    "First-person view model hung off the camera while the atlas is driven: the cannons and the top plate under the eye "
    "(model_*), a slit frame on rig, muzzles_0/1 at the cannon muzzles."])
for n in ("hullDark", "hullMid", "chrome", "hud", "team"):
    cm.material(n)


def up_top(name, mat, p):
    """torso: only the top plate, the hazard stripes and the ring mounts (in front of the stacks)."""
    if name != "torso":
        return True
    pts = p.points()
    return min(q[2] for q in pts) >= 240 and min(q[0] for q in pts) > -30


made = copy_parts(cm, u, root, need_parents(u, ["cannons_0", "cannons_1"]), keep=up_top)
for i, s in enumerate((-1, 1)):
    cm.part("muzzles_%d" % i, parent=made["cannons_%d" % i], at=(152, s * 42, 270),
            driver=dict(what="cannon muzzle", channels=["position"], functions=["AcCockpitKinds.cpp:PoseAtlasCockpit"]))

# --- the slit on rig (camera frame, cm): a dark slab 12 cm ahead with a window from 21 degrees below to 24 above the
# view's axis and 42 degrees to each side (as it shows: the cockpit is drawn at ac.CockpitScale); the view's vertical field is 62 degrees (31 to each side) -------------------
import math
D = 12.0
UP, DN, SD = D * math.tan(math.radians(24)), D * math.tan(math.radians(21)), D * math.tan(math.radians(42))
cm.mesh(rig, "hullDark", [box((130, 3, 40), at=cam(D, 0, UP + 20)),                  # the brow
                          box((130, 3, 40), at=cam(D, 0, -DN - 20)),                 # the sill
                          box((40, 3, UP + DN), at=cam(D, SD + 20, (UP - DN) / 2)),
                          box((40, 3, UP + DN), at=cam(D, -SD - 20, (UP - DN) / 2))], solid=False)
# a heavy lip under the brow, a steel edge round the window, a blue stripe on the brow, a lit strip on the sill
cm.mesh(rig, "hullMid", [box((2 * SD + 4, 3.0, 2.6), at=cam(D - 2.4, 0, UP + 1.3)),
                         box((2 * SD + 4, 3.0, 1.6), at=cam(D - 2.2, 0, -DN - 0.8)),
                         box((1.6, 3.0, UP + DN), at=cam(D - 2.2, SD + 0.9, (UP - DN) / 2)),
                         box((1.6, 3.0, UP + DN), at=cam(D - 2.2, -SD - 0.9, (UP - DN) / 2))], solid=False)
cm.mesh(rig, "team", [box((90, 1.2, 4.5), at=cam(D - 1.8, 0, UP + 7.5))], solid=False)
cm.mesh(rig, "hud", [box((40, 1.0, 0.9), at=cam(D - 1.6, 0, -DN - 3.0))], solid=False)
cm.mesh(rig, "chrome", [sphere(0.9, at=cam(D - 1.8, x, UP + 14), steps=6) for x in range(-40, 41, 10)] +
        [sphere(0.9, at=cam(D - 1.8, x, -DN - 9), steps=6) for x in range(-40, 41, 10)], solid=False)
cm.write()
