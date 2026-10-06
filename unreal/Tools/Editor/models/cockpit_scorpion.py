"""Cockpit of the Scorpion (`cockpit_scorpion_blue`): a low view over its shoulders, the pincers at the bottom edges, the tail's launcher over the top.

    UnrealEditorBG <abs>/unreal/Autocraft.uproject -run=pythonscript \
        -script=<abs>/unreal/Tools/Editor/models/cockpit_scorpion.py -unattended -nullrhi -nosplash -nosound -abslog=<log>

Contract (AcCockpitKinds.cpp, PoseScorpionCockpit): rig > level > model_root > model_<unit part>; the single socket
`muzzle` under model_launcher. The eye (AcPilotCamera::Eye) is 52 cm ahead of the body's pivot and 64 cm up (just over
the carapace's front): the body, plates and pincers come from the unit; the legs, the mound and the eyes (two red blobs at the bottom edge) are left out. The
unit's tail rises over its own back, which from the eye is overhead and behind, out of the view: so the cockpit hangs
the tail's last two segments, the launcher and its lamp 146 cm farther ahead and 4 cm higher (the pivots of tail_0..3,
launcher and tailLamp move together, so the unit's pose still drives them; tail_0 and tail_1, which would show as a
stump in front of the head, are left empty): the launcher's muzzle ring arches in over the top of the view.
"""
import copy
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from ac_cockpitkit import make, copy_parts, need_parents  # noqa: E402
from ac_modelkit import Loft  # noqa: E402

cm, u, rig, root = make("scorpion", "scorpion", [
    "First-person view model hung off the camera while the scorpion is driven: its body, plates, pincers, the tail's "
    "end and launcher under the eye (model_*), the socket `muzzle` on the launcher."])
for n in ("hullDark", "hullMid", "chrome"):
    cm.material(n)

SHIFT = (146.0, 0.0, 4.0)
keep = ["body", "plates_0", "plates_1", "plates_2", "plates_3", "plates_4", "pincers_0", "pincers_1",
        "tail_0", "tail_1", "tail_2", "tail_3", "launcher", "tailLamp"]
made = copy_parts(cm, u, root, need_parents(u, keep), keep=lambda name, mat, p: name not in ("tail_0", "tail_1"))


def moved(pr, d):
    c = copy.copy(pr)
    if isinstance(pr, Loft):
        c.sections = [[(q[0] + d[0], q[1] + d[1], q[2] + d[2]) for q in ring] for ring in pr.sections]
    else:
        c.at = (pr.at[0] + d[0], pr.at[1] + d[1], pr.at[2] + d[2])
    return c


for n in ("tail_0", "tail_1", "tail_2", "tail_3", "launcher", "tailLamp"):
    part = made[n]
    part.at = tuple(a + b for a, b in zip(part.at, SHIFT))
    for mat, prims in part.meshes.items():
        prims[:] = [moved(pr, SHIFT) for pr in prims]

tip = (-14, 0, 123)
cm.part("muzzle", parent=made["launcher"], at=(tip[0] + 51 + SHIFT[0], 0, tip[2] - 11.5 + SHIFT[2]),
        driver=dict(what="launcher muzzle: where the charge leaves", channels=["position"],
                    functions=["AcCockpitKinds.cpp:PoseScorpionCockpit"]))
cm.write()
