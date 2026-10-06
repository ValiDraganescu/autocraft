"""The placement hologram (chunk E8, GAME-LAYER.md §2.12 "Build placement"):
the see-through building a driven Prospector holds out, drawn by
AAcHologram (Source/Autocraft/AcHologram.cpp). The port of
`GameScene+Pointers.swift` `hologram(of:)` and `Pointers.ghostMaterial`.

Rerunnable: the graph is rebuilt from nothing every time.

Run it in the editor (`py "<repo>/unreal/Tools/Editor/make_hologram_material.py"`)
or as a commandlet while the live editor does not have it open:

    UnrealEditor-Cmd unreal/Autocraft.uproject -run=pythonscript \
        -script="$PWD/unreal/Tools/Editor/make_hologram_material.py" -unattended -nullrhi

/Game/Materials/M_Hologram: Swift's ghost material is lambert-lit, additive,
transparency 0.45, no depth write, and it is drawn after a depth-only copy of
the model so that only the nearest surface shows (one layer of glass, not
every face behind it added up). Here: lit (Surface ForwardShading) additive
translucency; the components also render custom depth (the material allows
translucent custom depth writes), and a pixel farther than the custom depth
there (a face behind the hologram's own front) is dropped. That is the
depth prepass.

    BaseColor = Color x 0.45          (diffuse c, lit by the sun and sky)
    Emissive  = Color x 0.3 x Pulse x 0.45
                                      (`c.blended(withFraction: 1 - 0.3·pulse,
                                      of: .black)`: c × 0.3·pulse; pulse =
                                      0.85 + 0.15·sin(5t), set by the actor)

Parameters: Color (linear), Pulse.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ac_models as A  # noqa: E402
import make_materials as MM  # noqa: E402

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
F1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1

PATH = A.MASTER_DIR + "/M_Hologram"

# 1 where this pixel is the hologram's nearest surface, 0 behind it.
FRONT_HLSL = """
return PD <= CD + max(0.5, PD * 0.001) ? 1.0 : 0.0;
"""


def log(msg):
    unreal.log("[make_hologram_material] " + msg)


def setp(mat, name, value):
    try:
        mat.set_editor_property(name, value)
    except Exception as e:  # noqa: BLE001
        log("cannot set %s: %s" % (name, e))


def main():
    MM.ensure_dir(A.MASTER_DIR)
    mat = MM.load(PATH) or MM.create(PATH, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    setp(mat, "shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    setp(mat, "blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    setp(mat, "translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    setp(mat, "two_sided", False)
    setp(mat, "allow_translucent_custom_depth_writes", True)
    setp(mat, "use_translucency_vertex_fog", False)
    g = MM.Graph(mat)
    color = g.vector("Color", (0.25, 1.0, 0.45))
    pulse = g.scalar("Pulse", 1.0)
    cd = g.node(unreal.MaterialExpressionSceneTexture, scene_texture_id=unreal.SceneTextureId.PPI_CUSTOM_DEPTH)
    pd = g.node(unreal.MaterialExpressionPixelDepth)
    front = g.custom("HologramFront", FRONT_HLSL, ["PD", "CD"], F1)
    g.link(pd, "", front, "PD")
    g.link(cd, "Color", front, "CD")
    k = g.node(unreal.MaterialExpressionConstant, r=0.45)
    ck = g.binop(unreal.MaterialExpressionMultiply, color, k)
    base = g.binop(unreal.MaterialExpressionMultiply, ck, front)
    k3 = g.node(unreal.MaterialExpressionConstant, r=0.3)
    e1 = g.binop(unreal.MaterialExpressionMultiply, ck, k3)
    e2 = g.binop(unreal.MaterialExpressionMultiply, e1, pulse)
    emis = g.binop(unreal.MaterialExpressionMultiply, e2, front)
    zero = g.node(unreal.MaterialExpressionConstant, r=0.0)
    one = g.node(unreal.MaterialExpressionConstant, r=1.0)
    g.out(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.out(zero, "", unreal.MaterialProperty.MP_METALLIC)
    g.out(zero, "", unreal.MaterialProperty.MP_SPECULAR)
    g.out(one, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.layout_material_expressions(mat)
    MEL.recompile_material(mat)
    EAL.set_metadata_tag(mat, "AcMadeBy", "Tools/Editor/make_hologram_material.py")
    EAL.save_loaded_asset(mat)
    log("made %s" % PATH)


if __name__ == "__main__":
    main()
