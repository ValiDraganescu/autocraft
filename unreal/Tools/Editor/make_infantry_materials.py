"""The Prospector's and the Ranger's animated materials (chunk B2,
GAME-LAYER.md §2.6; the poses are Source/Autocraft/AcPoseInfantry.cpp).
Rerunnable.

Run it after make_materials.py (which runs it as its last step, so a rerun
of that script keeps these changes), in the live editor or:

    UnrealEditor-Cmd unreal/Autocraft.uproject -run=pythonscript \
        -script="$PWD/unreal/Tools/Editor/make_infantry_materials.py" -unattended -nullrhi

What it does:

- `drillGlow` (the Prospector's drill bit, a copy of its steel with a gold
  emission at intensity 0) and `minigun_heat` (the Mini gun's barrel tips,
  an emissive at intensity 0): SceneKit exports them dark, so their
  instances get the Swift colour at intensity 1. The pose sets the mesh's
  emission scale (custom data 1) to Swift's `emission.intensity`.
- /Game/Materials/M_AcSmear: the Mini gun's smear sleeve (`plain_565656`,
  lambert grey, no depth write, its node's opacity eased with the spin),
  lit and translucent, opacity from custom data 1 (the pose writes the
  smear there). Its instance is re-parented to it; the parameters keep
  M_Hull's names (BaseColorTint, EmissiveColor, Roughness, Metallic).
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ac_models as A  # noqa: E402
import make_materials as MM  # noqa: E402

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

M_SMEAR = A.MASTER_DIR + "/M_AcSmear"
# The models whose materials these are (the canonical exports).
MODELS = ("prospector_blue", "ranger_blue", "cockpit_prospector_blue", "cockpit_ranger_blue")


def linear(rgb):
    """NSColor(calibratedRed:...) (generic RGB, gamma 1.8) → linear, as the
    export converts the other glows (glowOrange 0.55 → 0.3437)."""
    return tuple(c ** 1.8 for c in rgb)


# Models+ProspectorHull.swift: glow.emission.contents = (1, 0.7, 0.25).
DRILL_GLOW = linear((1.0, 0.7, 0.25))
# Models+Ranger.swift rangerMinigun: heat = emissive((1, 0.42, 0.1), intensity: 0).
MINIGUN_HEAT = linear((1.0, 0.42, 0.1))


def log(msg):
    unreal.log("[make_infantry_materials] " + msg)


def build_smear():
    path = M_SMEAR
    mat = MM.load(path) or MM.create(path, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("two_sided", False)
    mat.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE)
    g = MM.Graph(mat)
    g.out(g.vector("BaseColorTint", (0.34092, 0.34092, 0.34092)), "", unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.vector("EmissiveColor", (0, 0, 0)), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.out(g.scalar("Roughness", 1.0), "", unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.scalar("Metallic", 0.0), "", unreal.MaterialProperty.MP_METALLIC)
    g.out(g.custom_data(1, "EmissionScale", 0.0), "", unreal.MaterialProperty.MP_OPACITY)
    MEL.set_base_material_usage(mat, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    MEL.layout_material_expressions(mat)
    MEL.recompile_material(mat)
    EAL.set_metadata_tag(mat, "AcMadeBy", "Tools/Editor/make_infantry_materials.py")
    EAL.save_loaded_asset(mat)
    log("master %s" % path)
    return mat


def main(info=None):
    info = info or A.analyse(A.load_manifest())
    MM.ensure_dir(A.MASTER_DIR)
    smear = build_smear()
    # Keys used by the infantry models, and by any other model (a shared key
    # would change that model too: leave it alone).
    ours, others = set(), set()
    for m in info["canonical"]:
        for mt in m["materials"]:
            (ours if m["name"] in MODELS else others).add(mt["key"])
    for key, mat in sorted(info["materials"].items()):
        if mat["name"] not in ("drillGlow", "minigun_heat", "plain_565656") or key not in ours:
            continue
        if key in others:
            log("%s (%s) is shared with another model: left as it is" % (mat["name"], key))
            continue
        mi = MM.load(A.mi_asset_path(mat))
        if mi is None:
            log("no instance yet for %s (%s): run make_materials.py" % (mat["name"], key))
            continue
        if mat["name"] == "drillGlow":
            MEL.set_material_instance_vector_parameter_value(mi, "EmissiveColor", MM.lc(DRILL_GLOW))
            what = "gold at intensity 1"
        elif mat["name"] == "minigun_heat":
            MEL.set_material_instance_vector_parameter_value(mi, "EmissiveColor", MM.lc(MINIGUN_HEAT))
            what = "heat orange at intensity 1"
        else:
            MEL.set_material_instance_parent(mi, smear)
            what = "re-parented to M_AcSmear"
        EAL.set_metadata_tag(mi, "AcMadeBy", "Tools/Editor/make_materials.py + make_infantry_materials.py")
        MEL.update_material_instance(mi)
        EAL.save_loaded_asset(mi)
        log("%s: %s" % (A.mi_asset_path(mat), what))


if __name__ == "__main__":
    main()
