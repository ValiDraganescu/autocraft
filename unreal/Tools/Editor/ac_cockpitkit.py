"""Helpers for the cockpit model scripts (models/cockpit_<kind>.py), 2026-10-05.

A cockpit `cockpit_<kind>_blue` follows the Longbow's: rig > level > model_root > model_<unit part>, the unit's own
parts copied (same names behind `model_`, same parents, same pivots, so the pose function of the unit drives them
under the eye), plus cockpit-only geometry on `rig` (the frame the driver looks through, built in the camera's
frame) and muzzle sockets. `load_unit` runs the unit's script without its write(), so the copies are made from the
very same primitives; nothing is duplicated by hand.

Camera frame for `rig`: authored in UE axes as the kit wants them, the SceneKit camera frame being x right, y up,
-z ahead: UE X = right, UE Y = BEHIND (-ahead), UE Z = up. `cam(ahead, right, up)` gives the point (cm).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from ac_modelkit import Model  # noqa: E402


def cam(ahead, right, up):
    return (right, -ahead, up)


def load_unit(name):
    """The unit's Model object, built by its script with the write() call taken out."""
    path = os.path.join(HERE, "models", name + ".py")
    with open(path) as f:
        src = f.read()
    if src.count("\nm.write()") != 1:
        raise RuntimeError("%s: expected one m.write()" % name)
    ns = {"__file__": path, "__name__": "unit_" + name}
    exec(compile(src.replace("\nm.write()", "\npass"), path, "exec"), ns)
    return ns["m"]


def make(unit, kind, notes):
    """(cockpit Model, unit Model, rig, model_root); the unit's materials are shared."""
    u = load_unit(unit)
    cm = Model("cockpit_" + kind, category="cockpit", facing="camera space: x right, y up, -Z ahead", notes=notes)
    cm._materials.update(u._materials)
    zero = dict(what="the view frame", channels=["rotation", "position"], functions=[])
    rig = cm.part("rig", driver=dict(zero, what="parts under rig turn with the view and sway with the stride"))
    level = cm.part("level", parent=rig, driver=dict(zero, what="the view's pitch taken off again (the machine stays level)"))
    root = cm.part("model_root", parent=level, driver=dict(zero, what="the unit's model, placed under the eye by the pose code"))
    return cm, u, rig, root


def points_of(prim):
    return prim.points()


def copy_parts(cm, u, root, names, keep=None, skip_materials=()):
    """Copy the unit's parts `names` as model_<name>, in order, under the copy of their parent (model_root for a
    child of the unit's root). `keep(part_name, material, prim)` says whether a primitive comes along. Every copy is
    decoration (no ray pieces: a cockpit is only seen)."""
    made = {u.root.name: root}
    for n in names:
        sp = next(p for p in u.parts if p.name == n)
        par = made[sp.parent.name]
        cp = cm.part("model_" + n, parent=par, at=sp.at, hidden=sp.hidden, driver=sp.driver,
                     light=sp.light, particles=sp.particles)
        cp.R = sp.R
        made[n] = cp
        for mat, prims in sp.meshes.items():
            if mat in skip_materials:
                continue
            ps = [p for p in prims if keep is None or keep(n, mat, p)]
            if ps:
                cm.mesh(cp, mat, ps, solid=False)
    return made


def need_parents(u, names):
    """`names` with every ancestor (except the root) put in front, in unit order."""
    want = set(names)
    for n in list(names):
        p = next(x for x in u.parts if x.name == n).parent
        while p is not None and p.parent is not None:
            want.add(p.name)
            p = p.parent
    return [p.name for p in u.parts if p.name in want]
