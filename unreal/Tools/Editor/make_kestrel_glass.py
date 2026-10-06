"""The Kestrel's canopy glass (chunk B5, GAME-LAYER.md §2.6 "Flyers"): the
Swift shader modifier `Models.kestrelGlass` (Sources/Autocraft/
Models+Kestrel.swift) rebuilt as an Unreal material. Rerunnable: the
material is rebuilt in place.

Run it (make_materials.py also runs it as its last step, so the instance it
re-parents never falls back to M_Hull):

    UnrealEditor-Cmd unreal/Autocraft.uproject -run=pythonscript \
        -script="$PWD/unreal/Tools/Editor/make_kestrel_glass.py" -unattended -nullrhi

What it makes:

- /Game/Materials/M_AcKestrelGlass: dark smoked glass, metallic 0.85,
  roughness 0.1, tinted deeper toward the sills and teal-blue toward the
  crown, a rim of sky light at grazing angles, two painted glint streaks
  across the upper panes and a faint cyan instrument glow low at the front.
  It reads the canopy's own coordinates (the export merges the bubble into
  the Kestrel's `body` part, whose frame is the canopy's, as in SceneKit),
  converted back to SceneKit cells (x, z, y)/100. Like M_Hull's metals it
  adds the Swift studio sky (make_resource_materials.hull_env).

It then re-parents the export's glass instance (`plain_000205`, with a
surface shader modifier: only the Kestrel uses it) to the new material.
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

M_GLASS = A.MASTER_DIR + "/M_AcKestrelGlass"

GLASS_HLSL = """
// Models.kestrelGlass (Models+Kestrel.swift). `op` is the canopy's own space
// in SceneKit cells: Unreal's local (x, y, z) cm is SceneKit (x, z, y) / 100.
float3 op = float3(OP.x, OP.z, OP.y) * 0.01;
float3 n = normalize(N);
float3 v = normalize(V);
float rim = pow(1.0 - saturate(dot(n, v)), 3.0);
float along = saturate((op.x - 0.12) / 0.68);
float sill = lerp(0.13, 0.07, along), crown = lerp(0.28, 0.15, along);
float h = saturate((op.y - sill) / max(crown - sill, 0.01));
float3 low = float3(0.006, 0.012, 0.022), high = float3(0.025, 0.075, 0.115);
Diffuse = lerp(low, high, h * h);
float d = op.x + 1.5 * op.y + 0.4 * abs(op.z);
float glint = (1.0 - smoothstep(0.0, 0.022, abs(d - 0.8))) * 0.9 + (1.0 - smoothstep(0.0, 0.009, abs(d - 0.86))) * 0.7;
glint *= smoothstep(0.3, 0.6, h);
float dash = exp(-h * 7.0) * smoothstep(0.4, 0.75, along);
return (high * (0.05 + 0.25 * h) + float3(0.35, 0.55, 0.7) * rim * 0.3
    + float3(0.75, 0.9, 1.0) * glint + float3(0.1, 0.55, 0.65) * dash * 0.35) * Boost;
"""


def log(msg):
    unreal.log("[make_kestrel_glass] " + msg)


def is_glass(mat):
    return mat["name"].startswith("plain_000205") and \
        "SCNShaderModifierEntryPointSurface" in mat.get("shaderModifiers", [])


def build_glass():
    mat, g = R.new_master(M_GLASS, unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    teams = MM.load(A.MPC_TEAMS)
    boost = g.mpc(teams, "EmissiveBoost") if teams else g.scalar("EmissiveBoost", 1.0)
    c = R.custom(g, "Kestrel glass", GLASS_HLSL, [
        ("OP", R.local_position(g), ""),
        ("N", g.node(unreal.MaterialExpressionVertexNormalWS), ""),
        ("V", g.node(unreal.MaterialExpressionCameraVectorWS), ""),
        ("Boost", boost, ""),
    ], outputs=[("Diffuse", unreal.CustomMaterialOutputType.CMOT_FLOAT3)])
    metallic = g.scalar("Metallic", 0.85)
    roughness = g.scalar("Roughness", 0.1)
    g.out(c, "Diffuse", unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(metallic, "", unreal.MaterialProperty.MP_METALLIC)
    g.out(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    # The Swift studio sky on the glass, as on M_Hull's metals.
    env = R.hull_env(g, c, metallic, roughness)
    # hull_env reads the Custom node's first output (the emission): give it
    # the diffuse instead.
    MEL.connect_material_expressions(c, "Diffuse", env, "Base")
    e = g.binop(unreal.MaterialExpressionAdd, c, env)
    g.out(e, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    R.finish(mat, M_GLASS, (unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES,))
    EAL.set_metadata_tag(mat, "AcMadeBy", "Tools/Editor/make_kestrel_glass.py")
    EAL.save_loaded_asset(mat)
    return mat


def reparent(info, master):
    n = 0
    for key, mat in sorted(info["materials"].items()):
        if not is_glass(mat):
            continue
        mi = MM.load(A.mi_asset_path(mat))
        if mi is None:
            log("no instance yet for %s (%s): run make_materials.py" % (mat["name"], key))
            continue
        MEL.set_material_instance_parent(mi, master)
        EAL.set_metadata_tag(mi, "AcMadeBy", "Tools/Editor/make_materials.py + make_kestrel_glass.py")
        MEL.update_material_instance(mi)
        EAL.save_loaded_asset(mi)
        log("%s → %s" % (A.mi_asset_path(mat), M_GLASS))
        n += 1
    return n


def main(info=None):
    info = info or A.analyse(A.load_manifest())
    MM.ensure_dir(A.MASTER_DIR)
    master = build_glass()
    log("%d instances re-parented" % reparent(info, master))


if __name__ == "__main__":
    main()
