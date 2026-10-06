"""The buildings' materials (chunks B6/B7, GAME-LAYER.md §2.6). Rerunnable:
every asset is rebuilt in place.

Run it after make_materials.py (which makes the model instances it
re-parents) and make_resource_materials.py (whose mercury shader it reuses):

    UnrealEditor-Cmd unreal/Autocraft.uproject -run=pythonscript \
        -script="$PWD/unreal/Tools/Editor/make_building_materials.py" -unattended -nullrhi

or `py "<path>"` in a running editor.

What it makes:

- /Game/Materials/M_AcThroat: a Derrick's throat (`Models+Derrick.swift`
  :88-99, `throatGlow`): liquid silver lit cold blue-white from below. The
  Swift shader adds the mercury look (`MaterialLibrary.mercuryLook`: the
  studio sky mirrored, a blue-white sheen) on top of the glow colour, scaled
  by how lit it is (`lit = saturate(length(glow)·1.4)`), so a dry or
  unfinished Derrick's throat goes dull. Here: M_Mercury's shader
  (make_resource_materials.MERCURY_HLSL, `Pool` = 0: no waves) plus
  `EmissiveColor` × custom data 1, with the mirror scaled by
  `saturate(scale·1.1·0.804·1.4)` (the exported intensity is 1.1, the glow
  colour's length 0.804). The pose (AcPoseBuildings2.cpp, `PoseDerrick`)
  drives custom data 1 as the Swift `throatGlow.emission.intensity / 1.1`.

It then re-parents the model instance of every `throatGlow` material to it,
keeping the parameter names M_Hull uses (BaseColorTint, Metallic, Roughness,
EmissiveColor), so make_materials.py's values carry over.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ac_models as A  # noqa: E402
import make_materials as MM  # noqa: E402
import make_resource_materials as R  # noqa: E402

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

M_THROAT = A.MASTER_DIR + "/M_AcThroat"

# The mercury shader's last line, `return (...) * Scale;`, becomes the glow
# plus the mirror scaled by how lit the glow is.
_TAIL = "return (env * 0.7 * EnvGain * day + float3(0.55, 0.75, 1.0) * sheen * 0.8 * lerp(1.0, 0.5, Dark)) * Scale;"
THROAT_HLSL = R.MERCURY_HLSL.replace(
    _TAIL,
    "float lit = saturate(Scale * 1.1 * 0.804 * 1.4);\n"
    "return Glow * Scale + (env * 0.7 * EnvGain * day + float3(0.55, 0.75, 1.0) * sheen * 0.8 * lerp(1.0, 0.5, Dark)) * lit;")


def log(msg):
    unreal.log("[make_building_materials] " + msg)


def build_throat(mpc_world):
    if THROAT_HLSL == R.MERCURY_HLSL:
        raise RuntimeError("M_Mercury's shader changed: update make_building_materials._TAIL")
    mat, g = R.new_master(M_THROAT, unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    mat.set_editor_property("tangent_space_normal", False)
    g.out(g.vector("BaseColorTint", (0.2867, 0.30921, 0.35234)), "", unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.scalar("Metallic", 1.0), "", unreal.MaterialProperty.MP_METALLIC)
    g.out(g.scalar("Roughness", 0.05), "", unreal.MaterialProperty.MP_ROUGHNESS)
    c = R.custom(g, "Swift throatGlow (mercuryLook over the glow)", THROAT_HLSL, [
        ("LP", R.local_position(g), ""), ("O", R.instance_origin(g), ""),
        ("T", g.node(unreal.MaterialExpressionTime), ""), ("Pool", g.scalar("Pool", 0.0), ""),
        ("N", g.node(unreal.MaterialExpressionVertexNormalWS), ""),
        ("V", g.node(unreal.MaterialExpressionCameraVectorWS), ""),
        ("Dark", g.mpc(mpc_world, "Dark"), ""),
        ("EnvGain", g.scalar("EnvGain", 0.6), ""),
        ("Scale", g.custom_data(1, "EmissionScale", 1.0), ""),
        ("Glow", g.vector("EmissiveColor", (0.0588, 0.1696, 0.5804)), ""),
    ], outputs=[("Nrm", R.F3)])
    g.out(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.out(c, "Nrm", unreal.MaterialProperty.MP_NORMAL)
    for usage in (unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES, unreal.MaterialUsage.MATUSAGE_NANITE):
        MEL.set_base_material_usage(mat, usage)
    MEL.layout_material_expressions(mat)
    MEL.recompile_material(mat)
    EAL.set_metadata_tag(mat, "AcMadeBy", "Tools/Editor/make_building_materials.py")
    EAL.save_loaded_asset(mat)
    log("master %s" % M_THROAT)
    return mat


def reparent(info, master):
    n = 0
    for key, mat in sorted(info["materials"].items()):
        if mat["name"] != "throatGlow":
            continue
        mi = MM.load(A.mi_asset_path(mat))
        if mi is None:
            log("no instance yet for %s (%s): run make_materials.py" % (mat["name"], key))
            continue
        MEL.set_material_instance_parent(mi, master)
        EAL.set_metadata_tag(mi, "AcMadeBy", "Tools/Editor/make_materials.py + make_building_materials.py")
        MEL.update_material_instance(mi)
        EAL.save_loaded_asset(mi)
        log("%s → %s" % (A.mi_asset_path(mat), M_THROAT))
        n += 1
    return n


def main(info=None):
    info = info or A.analyse(A.load_manifest())
    MM.ensure_dir(A.MASTER_DIR)
    master = build_throat(R.make_world_mpc())
    log("%d throat instances re-parented" % reparent(info, master))


if __name__ == "__main__":
    main()
