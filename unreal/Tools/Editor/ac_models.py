"""Shared helpers for the model scripts (import_models.py, make_materials.py,
make_model_row.py): reading the export's manifest, pairing the blue and red
exports, and the asset names. No `unreal` import here, so it also runs in a
plain Python for checks.

Conventions (GAME-LAYER.md §1, Source/Autocraft/AcSpace.h):
- SceneKit (x, y, z), Y up, cells → Unreal (x, z, y) · 100 cm. The swap is a
  reflection (right- to left-handed); the triangle index order is kept
  (SceneKit's CCW front faces become Unreal's CW front faces).
- USD st has V flipped from SceneKit; Unreal's UV is SceneKit's: v = 1 - t.
- Team colour is not baked: the blue export is the canonical mesh and
  material; the red export is only read to learn which materials change with
  the team (see team_flags).
"""
import hashlib
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
UNREAL_ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
SRC = os.path.join(UNREAL_ROOT, "Content-src", "models")
MANIFEST = os.path.join(SRC, "manifest.json")
CATALOG_FILE = os.path.join(UNREAL_ROOT, "Content", "Models", "ModelCatalog.json")

# Asset locations.
TEXTURE_DIR = "/Game/Models/Textures"
MI_DIR = "/Game/Models/Materials"
MASTER_DIR = "/Game/Materials"
M_HULL = MASTER_DIR + "/M_Hull"
M_EMISSIVE = MASTER_DIR + "/M_Emissive"
M_ADDITIVE = MASTER_DIR + "/M_Additive"
MPC_TEAMS = MASTER_DIR + "/MPC_AcTeams"

CM = 100.0  # AcSpace::CmPerCell

# Bump when the mesh conversion changes, to force every mesh to rebuild.
MESH_VERSION = "5"
# Meshes with at least this many triangles get Nanite (opaque materials only).
NANITE_MIN_TRIANGLES = 1000

TEAMS = ("blue", "red")


def load_manifest():
    with open(MANIFEST) as f:
        return json.load(f)


def base_name(model):
    """ranger_blue → ranger; ore_0 → ore_0."""
    for t in TEAMS:
        if model["name"].endswith("_" + t) and model.get("team") == t:
            return model["name"][: -len(t) - 1]
    return model["name"]


def canonical_name(model):
    """The export whose meshes and materials Unreal uses: the blue one."""
    if model.get("team") == "red":
        return base_name(model) + "_blue"
    return model["name"]


def asset_name(s):
    return re.sub(r"[^A-Za-z0-9_]", "_", s)


def mesh_asset_path(model, prim):
    """The static mesh for one (part, material) prim of a canonical model."""
    folder = "/Game/Models/" + asset_name(base_name(model))
    name = "SM_" + asset_name(prim)
    return folder + "/" + name


def mi_asset_path(material):
    """By material key (the same key means the same material in every model;
    pass the canonical entry, analyse()["materials"][key], for a stable name)."""
    return MI_DIR + "/MI_" + asset_name(material["name"]) + "_" + material["key"]


def texture_asset_path(rel):
    """textures/foo_bar.png → /Game/Models/Textures/T_foo_bar."""
    stem = os.path.splitext(os.path.basename(rel))[0]
    return TEXTURE_DIR + "/T_" + asset_name(stem)


def object_path(package_path):
    """/Game/A/B → /Game/A/B.B (what a soft object path wants)."""
    return package_path + "." + package_path.rsplit("/", 1)[1]


def _strip_team(s, team):
    return s.replace("_" + team, "_TEAM") if isinstance(s, str) else s


def pair_meshes(blue_model, red_model):
    """Yields (blue_mesh, red_mesh, part_name) for every mesh of the red export,
    matched to the blue mesh with the same geometry: same part (by position in
    the part list), same triangle and vertex counts, in order. The exporter
    writes the meshes of a part in material order, which differs between teams."""
    for pb, pr in zip(blue_model["parts"], red_model["parts"]):
        if _strip_team(pb["name"], "blue") != _strip_team(pr["name"], "red"):
            raise ValueError("parts differ: %s / %s" % (pb["name"], pr["name"]))
        free = list(pb["meshes"])
        for mr in pr["meshes"]:
            same = [mb for mb in free if mb["triangles"] == mr["triangles"] and mb["vertices"] == mr["vertices"]]
            # Two meshes of one part can share their counts (the Dropship's
            # lamps): prefer the one whose material has the same name.
            named = [mb for mb in same if _strip_team(mb["material"], "blue") == _strip_team(mr["material"], "red")]
            hit = (named or same or [None])[0]
            if hit is None:
                raise ValueError("no blue mesh for %s/%s" % (red_model["name"], mr["prim"]))
            free.remove(hit)
            yield hit, mr, pb["name"]


TEAM_FIELDS = ("TeamTint", "PaintBase", "PaintEmissive", "TeamGlow")


def team_flags(blue_mat, red_mat):
    """What changes between the blue and the red version of a material:
    - TeamTint: the tint is the team colour (the `team` hull material);
    - PaintBase / PaintEmissive: the texture's blue paint is recoloured
      (SkinTextures.painted, Models+Prospector.swift);
    - TeamGlow: the emission colour is the team's lamp colour."""
    flags = set()
    if blue_mat.get("baseColorTint") != red_mat.get("baseColorTint"):
        flags.add("TeamTint")
    bu = blue_mat.get("baseColorTextureUntinted", blue_mat.get("baseColorTexture"))
    ru = red_mat.get("baseColorTextureUntinted", red_mat.get("baseColorTexture"))
    if bu != ru:
        flags.add("PaintBase")
    if blue_mat.get("emissiveTexture") != red_mat.get("emissiveTexture"):
        flags.add("PaintEmissive")
    if blue_mat.get("emissive") != red_mat.get("emissive"):
        flags.add("TeamGlow")
    return flags


def analyse(manifest):
    """Everything the scripts need from the manifest.

    Returns dict with:
      models: name → model
      canonical: [model] (blue and teamless exports)
      materials: key → material (canonical materials only)
      flags: key → set of team flags
      red_map: (red model, red prim) → blue prim
    """
    models = {m["name"]: m for m in manifest["models"]}
    canonical = [m for m in manifest["models"] if m.get("team") != "red"]
    materials = {}
    for m in canonical:
        for mt in m["materials"]:
            materials.setdefault(mt["key"], mt)
    flags = {k: set() for k in materials}
    red_map = {}
    for m in manifest["models"]:
        if m.get("team") != "red":
            continue
        blue = models[canonical_name(m)]
        bmats = {x["name"]: x for x in blue["materials"]}
        rmats = {x["name"]: x for x in m["materials"]}
        for mb, mr, _ in pair_meshes(blue, m):
            red_map[(m["name"], mr["prim"])] = mb["prim"]
            b, r = bmats[mb["material"]], rmats[mr["material"]]
            if b["key"] != r["key"]:
                flags[b["key"]] |= team_flags(b, r)
    return {"models": models, "canonical": canonical, "materials": materials, "flags": flags, "red_map": red_map}


# --- USDA meshes -------------------------------------------------------------

_NUM = re.compile(r"-?\d+(?:\.\d*)?(?:[eE][-+]?\d+)?")


def _array(block, decl):
    i = block.find(decl + " = [")
    if i < 0:
        return None
    i += len(decl) + 4
    j = block.find("]", i)
    return block[i:j]


def read_usda_meshes(path):
    """name → dict(points, normals, st, indices, hash) for every Mesh prim, as
    flat float/int lists in the file's own (SceneKit) space."""
    with open(path) as f:
        text = f.read()
    out = {}
    for m in re.finditer(r'def Mesh "([^"]+)"', text):
        end = text.find("subdivisionScheme", m.end())
        block = text[m.end():end]
        pts = _array(block, "point3f[] points")
        nrm = _array(block, "normal3f[] normals")
        st = _array(block, "texCoord2f[] primvars:st")
        idx = _array(block, "int[] faceVertexIndices")
        counts = _array(block, "int[] faceVertexCounts")
        if counts and set(_NUM.findall(counts)) - {"3"}:
            raise ValueError("%s: %s is not all triangles" % (path, m.group(1)))
        h = hashlib.md5((MESH_VERSION + block).encode()).hexdigest()
        out[m.group(1)] = {
            "points": [float(x) for x in _NUM.findall(pts)],
            "normals": [float(x) for x in _NUM.findall(nrm)] if nrm else None,
            "st": [float(x) for x in _NUM.findall(st)] if st else None,
            "indices": [int(x) for x in _NUM.findall(idx)],
            "hash": h,
        }
    return out


def file_hash(path):
    st = os.stat(path)
    return "%d-%d" % (st.st_size, int(st.st_mtime))
