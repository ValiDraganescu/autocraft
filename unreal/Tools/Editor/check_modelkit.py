"""Checks of ac_modelkit.py that need the editor (hidden run, -nullrhi):

    UnrealEditorBG ... -run=pythonscript -script=unreal/Tools/Editor/check_modelkit.py

1. every prim kind, built by Geometry Script in a rotated, offset frame: its mesh bounds equal the bounds of
   Prim.points() (what the model's boundsMin/Max and the ray pieces rest on), and its triangles face the way
   the kit assumes ((b - a) x (c - a) into the solid);
2. unreal.Transform's matrix equals ac_modelkit.ue_matrix for the same rotation;
3. the Longbow's `hull`: its SceneKit ray pieces, converted with sk_to_ue, bound the hull's static meshes
   (the round trip of MODELLING.md).
Raises (and so exits non-zero) on a failure.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ac_modelkit as K  # noqa: E402


def log(s):
    unreal.log("[check_modelkit] " + s)


def mesh_points_tris(dm):
    _, pos, _ = unreal.GeometryScript_MeshQueries.get_all_vertex_positions(dm, True)
    pts = [(v.x, v.y, v.z) for v in unreal.GeometryScript_List.convert_vector_list_to_array(pos)]
    _, tri, _ = unreal.GeometryScript_MeshQueries.get_all_triangle_indices(dm, True)
    tris = [(t.x, t.y, t.z) for t in unreal.GeometryScript_List.convert_triangle_list_to_array(tri)]
    return pts, tris


def aabb(pts):
    return [min(p[i] for p in pts) for i in range(3)], [max(p[i] for p in pts) for i in range(3)]


def volume(pts, tris):
    return sum(K.v_dot(pts[a], K.v_cross(pts[b], pts[c])) for a, b, c in tris) / 6.0


def main():
    prims = {
        "box": K.box((40, 20, 10), at=(30, -10, 5), rot=(10, 35, 20)),
        "box base": K.box((40, 20, 10), at=(30, -10, 5), rot=(0, 90, 0), base=True),
        "cyl z": K.cyl(8, 50, at=(0, 5, 10)),
        "cyl x": K.cyl(8, 50, at=(10, 5, 10), axis="x"),
        "cyl y base": K.cyl(8, 50, at=(10, 5, 10), axis="y", base=True),
        "cone x": K.cone(12, 3, 60, at=(0, 0, 0), axis="x", rot=(0, 20, 0)),
        "sphere": K.sphere(10, at=(3, 4, 5), rot=(10, 20, 30), squash=(2, 1, 0.5)),
        "prism xy": K.prism([(0, 0), (60, 0), (30, 40)], 6, "xy", at=(5, 5, 5), rot=(0, 15, 0)),
        "prism xy cw": K.prism([(0, 0), (30, 40), (60, 0)], 6, "xy", at=(5, 5, 5)),
        "prism xz": K.prism([(0, 0), (40, 0), (25, 60), (5, 60)], 4, "xz", at=(5, 5, 5), rot=(0, 0, 20)),
        "prism yz": K.prism([(0, 0), (40, 0), (25, 60)], 4, "yz", at=(5, 5, 5)),
        "bar box": K.bar((0, 0, 0), (30, 10, 80), 12, 8),
        "bar cyl": K.bar((0, 0, 0), (-30, 10, 80), 12, shape="cyl"),
        "bar taper": K.bar((0, 0, 0), (30, 0, 80), 12, 8, taper=0.5),
        "loft": K.loft([K.ring_ellipse(x, w, h, 0, 5, 12) for x, w, h in ((0, 1, 1), (40, 10, 6), (90, 6, 4), (120, 0.5, 0.5))]),
    }
    Rw, tw = K.rot3(5, 25, 10), (200.0, 40.0, 60.0)  # a part frame
    bad = []
    for name, pr in prims.items():
        placed = pr.placed(Rw, tw)
        dm = K.build_mesh([placed])
        pts, tris = mesh_points_tris(dm)
        # back into the model frame
        model_pts = [K.m_apply(p, Rw, tw) for p in pts]
        lo, hi = aabb(model_pts)
        plo, phi = aabb(pr.points())
        err = max(max(abs(a - b) for a, b in zip(lo, plo)), max(abs(a - b) for a, b in zip(hi, phi)))
        vol = volume(pts, tris)
        # sphere/cylinder tessellation: points() is the exact shape, the mesh its inscribed polygon
        tol = 1.5 if name.startswith(("sphere", "cyl", "cone", "bar cyl")) else 0.05
        ok = err <= tol and vol < 0
        log("%-12s tris %4d  bounds err %.3f cm  volume %.0f %s" % (name, len(tris), err, vol, "ok" if ok else "FAIL"))
        if not ok:
            bad.append(name)
    # 2. the matrix
    for rpy in [(20, 30, 10), (-40, 120, -70), (0, 0, 90)]:
        t = unreal.Transform(unreal.Vector(1, 2, 3), unreal.Rotator(roll=rpy[2], pitch=rpy[0], yaw=rpy[1]), unreal.Vector(1, 1, 1))
        m = t.to_matrix()
        rows = [m.x_plane, m.y_plane, m.z_plane]
        R = K.rot3(*rpy)
        err = max(abs(getattr(rows[i], c) - R[i][j]) for i in range(3) for j, c in enumerate("xyz"))
        log("matrix %s: max difference to rot3 %.2e %s" % (rpy, err, "ok" if err < 1e-5 else "FAIL"))
        if err >= 1e-5:
            bad.append("matrix")
    # 3. the Longbow's hull
    doc = K.load_catalog()
    hull = next(p for m in doc["models"] if m["name"] == "longbow_blue" for p in m["parts"] if p["name"] == "hull")
    ue_pts = []
    for piece in hull["rays"]:
        ue = K.sk_to_ue(piece["transform"])
        R = tuple(tuple(ue[i * 4 + j] for j in range(3)) for i in range(3))
        t = tuple(ue[12:15])
        lo, hi = piece["lo"], piece["hi"]
        # SceneKit local axes are UE's (x, z, y)
        for sx in (lo[0], hi[0]):
            for sy in (lo[1], hi[1]):
                for sz in (lo[2], hi[2]):
                    ue_pts.append(K.m_apply((sx * K.CM, sz * K.CM, sy * K.CM), R, t))
    rlo, rhi = aabb(ue_pts)
    mlo = [1e9] * 3
    mhi = [-1e9] * 3
    for mesh in hull["meshes"]:
        sm = unreal.load_asset(mesh["mesh"].split(".")[0])
        b = sm.get_bounds()
        o, e = b.origin, b.box_extent
        for i, (oc, ec) in enumerate(zip((o.x, o.y, o.z), (e.x, e.y, e.z))):
            mlo[i] = min(mlo[i], oc - ec)
            mhi[i] = max(mhi[i], oc + ec)
    err = max(max(abs(a - b) for a, b in zip(mlo, rlo)), max(abs(a - b) for a, b in zip(mhi, rhi)))
    log("longbow hull: meshes %s..%s  rays %s..%s  (cm), largest difference %.1f cm" % (
        [round(x) for x in mlo], [round(x) for x in mhi], [round(x) for x in rlo], [round(x) for x in rhi], err))
    # the ray pieces are boxes around the parts; the meshes lie inside (the nets add a little relief)
    inside = all(rlo[i] - 6 <= mlo[i] and mhi[i] <= rhi[i] + 6 for i in range(3))
    log("longbow hull meshes lie inside the converted ray boxes (+-6 cm): %s" % ("ok" if inside else "FAIL"))
    if not inside:
        bad.append("hull")
    K.selftest()
    if bad:
        raise RuntimeError("modelkit check failed: " + ", ".join(bad))
    log("all ok")


main()
