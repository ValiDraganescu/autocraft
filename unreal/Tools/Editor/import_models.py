"""Imports every exported model (chunk A4, GAME-LAYER.md §2.6 and §3.2).

Reads unreal/Content-src/models (USDA, textures, manifest.json, made by
`Autocraft export-models`) and makes:

- /Game/Models/<model>/SM_<part>__<material>: one static mesh per (part,
  material) mesh prim, in the part's own frame (the part's rest transform is
  NOT baked in: it goes to the catalog). <model> drops the team suffix: the
  blue and red exports share their meshes, the team colour comes from
  per-instance custom data (make_materials.py).
- the materials and textures (make_materials.main, run first);
- Content/Models/ModelCatalog.json: the manifest plus asset paths, read at
  runtime by FAcModelCatalog (Source/Autocraft/AcModelCatalog.h).

Axes and units as AcSpace.h: SceneKit (x, y, z) in cells → Unreal
(x, z, y) · 100 cm. The swap is a reflection, yet the index order is kept:
SceneKit's counter-clockwise front faces read clockwise after it, which is
Unreal's front face (checked against the Swift render: reversing them shows
the back faces, black). UV v = 1 - t (USD st is flipped from SceneKit).

Collision is off (the game casts its own rays, GameScene+Rays). Nanite is on
for opaque meshes of at least NANITE_MIN_TRIANGLES triangles.

Rerunnable: a mesh is rebuilt only when its USDA prim changed (hash in the
asset's metadata), or when ac_models.MESH_VERSION changes.

Run it from the repo root (about 2-4 min the first time):

    UnrealEditor unreal/Autocraft.uproject -run=pythonscript \
        -script="$PWD/unreal/Tools/Editor/import_models.py" -unattended -nullrhi

AC_ONLY=ranger,citadel (base names) limits the meshes to those models; the
catalog always lists every model. AC_SKIP_MATERIALS=1 skips
make_materials.
"""
import json
import os
import sys
import time

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ac_models as A  # noqa: E402
import make_materials  # noqa: E402

EAL = unreal.EditorAssetLibrary
GS = unreal.GeometryScript_MeshEdits
NEW = unreal.GeometryScript_NewAssetUtils
COPY = unreal.GeometryScript_AssetUtils


def log(msg):
    unreal.log("[import_models] " + msg)


def manifold_triangles(idx, nv):
    """The triangles (index order kept, see the module notes), and the
    source vertex of every vertex. A DynamicMesh refuses a triangle on an edge
    that already has two (the exports have double-sided shells and stacked
    panels), so such a triangle gets its own copies of its three vertices:
    the static mesh build welds them back where they match."""
    edges = {}
    src = list(range(nv))
    tris = []
    for k in range(0, len(idx), 3):
        a, b, c = idx[k], idx[k + 1], idx[k + 2]
        if a == b or b == c or a == c:
            continue
        keys = [(min(a, b), max(a, b)), (min(b, c), max(b, c)), (min(c, a), max(c, a))]
        if any(edges.get(e, 0) >= 2 for e in keys):
            a, b, c = len(src), len(src) + 1, len(src) + 2
            src += [idx[k], idx[k + 1], idx[k + 2]]
            keys = [(a, b), (b, c), (a, c)]
        for e in keys:
            edges[e] = edges.get(e, 0) + 1
        tris.append((a, b, c))
    return tris, src


def build_dynamic_mesh(data):
    p, n, st, idx = data["points"], data["normals"], data["st"], data["indices"]
    tris, src = manifold_triangles(idx, len(p) // 3)
    c = A.CM
    buf = unreal.GeometryScriptSimpleMeshBuffers()
    buf.set_editor_property("vertices", [unreal.Vector(p[3 * i] * c, p[3 * i + 2] * c, p[3 * i + 1] * c) for i in src])
    if n:
        buf.set_editor_property("normals", [unreal.Vector(n[3 * i], n[3 * i + 2], n[3 * i + 1]) for i in src])
    if st:
        buf.set_editor_property("uv0", [unreal.Vector2D(st[2 * i], 1.0 - st[2 * i + 1]) for i in src])
    buf.set_editor_property("triangles", [unreal.IntVector(a, b, c) for a, b, c in tris])
    dm = unreal.DynamicMesh()
    dm.reset()
    GS.append_buffers_to_mesh(dm, buf, 0)
    return dm


def nanite_for(material, triangles):
    return material["blend"] == "opaque" and triangles >= A.NANITE_MIN_TRIANGLES


def nanite_settings(on):
    s = unreal.MeshNaniteSettings()
    s.set_editor_property("enabled", on)
    return s


def write_mesh(path, data, mi, nanite):
    existing = EAL.load_asset(path) if EAL.does_asset_exist(path) else None
    if existing is not None and EAL.get_metadata_tag(existing, "AcSource") == data["hash"] + ("-n" if nanite else ""):
        # Same geometry, but the material key (so the instance) may have
        # changed: keep the slot on the catalog's instance.
        if existing.get_material(0) != mi:
            existing.set_material(0, mi)
            EAL.save_loaded_asset(existing)
        return existing, False
    dm = build_dynamic_mesh(data)
    if existing is None:
        folder = path.rsplit("/", 1)[0]
        if not EAL.does_directory_exist(folder):
            EAL.make_directory(folder)
        opts = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
        opts.set_editor_property("enable_collision", False)
        opts.set_editor_property("collision_mode", unreal.CollisionTraceFlag.CTF_USE_DEFAULT)
        opts.set_editor_property("enable_nanite", nanite)
        opts.set_editor_property("nanite_settings", nanite_settings(nanite))
        opts.set_editor_property("enable_recompute_normals", False)
        opts.set_editor_property("enable_recompute_tangents", True)
        sm, outcome = NEW.create_new_static_mesh_asset_from_mesh(dm, path, opts)
        if sm is None or outcome != unreal.GeometryScriptOutcomePins.SUCCESS:
            raise RuntimeError("cannot create " + path)
    else:
        sm = existing
        opts = unreal.GeometryScriptCopyMeshToAssetOptions()
        opts.set_editor_property("enable_recompute_normals", False)
        opts.set_editor_property("enable_recompute_tangents", True)
        opts.set_editor_property("apply_nanite_settings", True)
        opts.set_editor_property("new_nanite_settings", nanite_settings(nanite))
        opts.set_editor_property("replace_materials", True)
        opts.set_editor_property("new_materials", [mi])
        lod = unreal.GeometryScriptMeshWriteLOD()
        _, outcome = COPY.copy_mesh_to_static_mesh(dm, sm, opts, lod)
        if outcome != unreal.GeometryScriptOutcomePins.SUCCESS:
            raise RuntimeError("cannot update " + path)
    sm.set_material(0, mi)
    body = sm.get_editor_property("body_setup")
    if body is not None:
        body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
    EAL.set_metadata_tag(sm, "AcSource", data["hash"] + ("-n" if nanite else ""))
    EAL.set_metadata_tag(sm, "AcMadeBy", "Tools/Editor/import_models.py")
    EAL.save_loaded_asset(sm)
    return sm, True


def import_meshes(info, only):
    made = skipped = 0
    t0 = time.time()
    models = [m for m in info["canonical"] if not only or A.base_name(m) in only]
    with unreal.ScopedSlowTask(len(models), "Model meshes") as task:
        for m in models:
            task.enter_progress_frame(1, m["name"])
            prims = A.read_usda_meshes(os.path.join(A.SRC, m["file"]))
            mats = {x["name"]: x for x in m["materials"]}
            for part in m["parts"]:
                for mesh in part["meshes"]:
                    mat = mats[mesh["material"]]
                    mi = unreal.load_asset(A.mi_asset_path(info["materials"][mat["key"]]))
                    if mi is None:
                        raise RuntimeError("no material instance for %s (run make_materials.py)" % mat["name"])
                    _, new = write_mesh(A.mesh_asset_path(m, mesh["prim"]), prims[mesh["prim"]], mi,
                                        nanite_for(mat, mesh["triangles"]))
                    made += new
                    skipped += not new
            log("%s: %d parts (%.0f s)" % (m["name"], len(m["parts"]), time.time() - t0))
    log("meshes: %d built, %d unchanged, %.0f s" % (made, skipped, time.time() - t0))


# --- the catalog ---------------------------------------------------------------

def catalog(info, manifest):
    """The manifest, slimmed, plus where each mesh and material went."""
    models_out = []
    for m in manifest["models"]:
        canon = info["models"][A.canonical_name(m)]
        cmats = {x["name"]: x for x in canon["materials"]}
        own_mats = {x["name"]: x for x in m["materials"]}
        parts_out = []
        for part, cpart in zip(m["parts"], canon["parts"]):
            cmeshes = {x["prim"]: x for x in cpart["meshes"]}
            meshes = []
            for mesh in part["meshes"]:
                prim = info["red_map"].get((m["name"], mesh["prim"]), mesh["prim"])
                cmat = cmats[cmeshes[prim]["material"]]
                meshes.append({
                    "material": mesh["material"],
                    "prim": mesh["prim"],
                    "mesh": A.object_path(A.mesh_asset_path(canon, prim)),
                    "materialInstance": A.object_path(A.mi_asset_path(info["materials"][cmat["key"]])),
                    "materialKey": cmat["key"],
                    "blend": own_mats[mesh["material"]]["blend"],
                    "team": sorted(info["flags"].get(cmat["key"], ())),
                    "triangles": mesh["triangles"],
                })
            p = {k: part[k] for k in ("name", "path", "localTransform", "position", "eulerAngles", "scale", "hidden")}
            for k in ("parent", "handles", "sceneKitName", "pivot", "opacity", "light", "particles", "rays"):
                if k in part:
                    p[k] = part[k]
            if "driver" in part:
                p["driver"] = {k: part["driver"][k] for k in ("what", "channels", "functions") if k in part["driver"]}
            p["meshes"] = meshes
            parts_out.append(p)
        out = {k: m[k] for k in ("name", "file", "category", "facing", "boundsMin", "boundsMax", "triangles")}
        out["team"] = m.get("team")
        out["base"] = A.base_name(m)
        out["canonical"] = canon["name"]
        out["notes"] = m.get("notes", [])
        out["parts"] = parts_out
        models_out.append(out)
    doc = {
        "version": 1,
        "madeBy": "unreal/Tools/Editor/import_models.py from " + manifest.get("generator", "export-models"),
        "axes": "Transforms and bounds are SceneKit's (Y up, cells, row-vector 4x4 matrices with the translation in "
                "elements 12-14). Convert with AcSpace (SceneKit (x, y, z) -> UE (x, z, y) * 100 cm). Meshes are "
                "already in Unreal space, in their part's frame.",
        "customData": {"0": "team index (MPC_AcTeams palette, 0..7)", "1": "emission scale (default 1)",
                       "2": "reserved: fade", "3": "reserved: char"},
        "models": models_out,
    }
    os.makedirs(os.path.dirname(A.CATALOG_FILE), exist_ok=True)
    with open(A.CATALOG_FILE, "w") as f:
        json.dump(doc, f, indent=1)
    log("catalog: %d models → %s" % (len(models_out), A.CATALOG_FILE))


def main():
    manifest = A.load_manifest()
    info = A.analyse(manifest)
    only = set(filter(None, os.environ.get("AC_ONLY", "").split(",")))
    if not os.environ.get("AC_SKIP_MATERIALS"):
        make_materials.main(info)
    import_meshes(info, only)
    catalog(info, manifest)


if __name__ == "__main__":
    main()
