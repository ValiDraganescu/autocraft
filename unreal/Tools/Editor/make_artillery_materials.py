"""The Longbow's animated materials (chunk B4, GAME-LAYER.md §2.6; the
poses are Source/Autocraft/AcPoseArtillery.cpp). Rerunnable.

Run it after make_materials.py (which runs it as its last step, so a rerun
of that script keeps these changes):

    UnrealEditor-Cmd unreal/Autocraft.uproject -run=pythonscript \
        -script="$PWD/unreal/Tools/Editor/make_artillery_materials.py" -unattended -nullrhi

What it does:

- `lockGlow` (the rail's lock lights, `Models+LongbowRhino.swift`, a copy of
  glowGreen): SceneKit exports it dark (its intensity is 0 until the pose
  sets it), so its instance gets glowGreen's colour at intensity 1. The pose
  then sets the mesh's emission scale (custom data 1) to Swift's
  `lockGlow.emission.intensity`.
- /Game/Materials/M_AcSmokeRing: the anchor shot's smoke ring
  (`smokeMaterial`), lit and translucent, its opacity from custom data 1
  (the pose writes Swift's node `opacity` there; the ring has no other
  emission to scale). Its instance is re-parented to it; the parameters keep
  M_Hull's names (BaseColorTint, EmissiveColor, Roughness, Metallic), so
  make_materials.py fills them as before.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ac_models as A  # noqa: E402
import make_materials as MM  # noqa: E402

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

M_SMOKE_RING = A.MASTER_DIR + "/M_AcSmokeRing"
# glowGreen (Models.swift: NSColor(0.2, 0.75, 0.2), intensity 0.7) as
# exported, divided by its intensity: linear, intensity 1.
GLOW_GREEN = (0.02859 / 0.7, 0.39739 / 0.7, 0.03809 / 0.7)


def log(msg):
    unreal.log("[make_artillery_materials] " + msg)


def build_smoke_ring():
    path = M_SMOKE_RING
    mat = MM.load(path) or MM.create(path, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("two_sided", False)
    mat.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE)
    g = MM.Graph(mat)
    g.out(g.vector("BaseColorTint", (0.33783, 0.47716, 0.67007)), "", unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.vector("EmissiveColor", (0.01498, 0.05755, 0.1515)), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.out(g.scalar("Roughness", 1.0), "", unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.scalar("Metallic", 0.0), "", unreal.MaterialProperty.MP_METALLIC)
    g.out(g.custom_data(1, "EmissionScale", 0.6), "", unreal.MaterialProperty.MP_OPACITY)
    MEL.set_base_material_usage(mat, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    MEL.layout_material_expressions(mat)
    MEL.recompile_material(mat)
    EAL.set_metadata_tag(mat, "AcMadeBy", "Tools/Editor/make_artillery_materials.py")
    EAL.save_loaded_asset(mat)
    log("master %s" % path)
    return mat


def main(info=None):
    info = info or A.analyse(A.load_manifest())
    MM.ensure_dir(A.MASTER_DIR)
    ring = build_smoke_ring()
    for key, mat in sorted(info["materials"].items()):
        if mat["name"] not in ("lockGlow", "smokeMaterial"):
            continue
        mi = MM.load(A.mi_asset_path(mat))
        if mi is None:
            log("no instance yet for %s (%s): run make_materials.py" % (mat["name"], key))
            continue
        if mat["name"] == "lockGlow":
            MEL.set_material_instance_vector_parameter_value(mi, "EmissiveColor", MM.lc(GLOW_GREEN))
            what = "glowGreen at intensity 1"
        else:
            MEL.set_material_instance_parent(mi, ring)
            what = "re-parented to M_AcSmokeRing"
        EAL.set_metadata_tag(mi, "AcMadeBy", "Tools/Editor/make_materials.py + make_artillery_materials.py")
        MEL.update_material_instance(mi)
        EAL.save_loaded_asset(mi)
        log("%s: %s" % (A.mi_asset_path(mat), what))


if __name__ == "__main__":
    main()
