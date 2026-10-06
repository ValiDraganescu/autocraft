"""The pointer cue materials (chunk D4, GAME-LAYER.md §2.3 "Hover"): the
hover ring, the cockpit uplink and the selection ring of
`GameScene+Pointers.swift`, drawn by AAcPointerCues (AcPointerCues.cpp).
Rerunnable: every asset is rebuilt in place.

Run it in the editor (`py "<repo>/unreal/Tools/Editor/make_pointer_materials.py"`)
or as a commandlet:

    UnrealEditor-Cmd unreal/Autocraft.uproject -run=pythonscript \
        -script="$PWD/unreal/Tools/Editor/make_pointer_materials.py" -unattended -nullrhi

What it makes (unlit, no shadows; parameters Color (linear), Intensity,
Opacity, Shape):

- /Game/Materials/M_AcPointer: translucent (the rings: SceneKit `.constant`
  with no depth write).
- /Game/Materials/M_AcPointerAdd: additive (the uplink's beam and disc,
  `blendMode = .add`).
- /Game/Materials/M_AcPointerTop: translucent with no depth test (the
  uplink's brackets, `readsFromDepthBuffer = false`: over the unit).

Shape: 0 flat colour; 1 the beam (bright at the ground, gone at the top:
v = 0 at the bottom of AAcPointerCues's cylinder); 2 the disc (the Swift
radial gradient: alpha 0.05 at the middle, 0.25 at 0.7, 0.7 at 0.93, 0 at
the rim).
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
F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3

CUE_HLSL = """
// Shape 0: flat; 1: the beam's upward fade; 2: the disc's radial gradient.
float a = 1.0;
if (Shape > 1.5) {
    float d = saturate(length(UV - 0.5) * 2.0);
    a = d < 0.7 ? lerp(0.05, 0.25, d / 0.7)
      : d < 0.93 ? lerp(0.25, 0.7, (d - 0.7) / 0.23)
      : lerp(0.7, 0.0, saturate((d - 0.93) / 0.07));
} else if (Shape > 0.5) {
    a = saturate(1.0 - UV.y);
}
return a;
"""


def log(msg):
    unreal.log("[make_pointer_materials] " + msg)


def build(path, blend, no_depth):
    mat = MM.load(path) or MM.create(path, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", blend)
    mat.set_editor_property("two_sided", False)
    mat.set_editor_property("disable_depth_test", no_depth)
    # No velocity or fog on a UI-like cue.
    mat.set_editor_property("use_translucency_vertex_fog", False)
    g = MM.Graph(mat)
    color = g.vector("Color", (0.3, 1.0, 0.4))
    intensity = g.scalar("Intensity", 1.0)
    opacity = g.scalar("Opacity", 1.0)
    shape = g.scalar("Shape", 0.0)
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    c = g.custom("CueAlpha", CUE_HLSL, ["UV", "Shape"], F1)
    g.link(uv, "", c, "UV")
    g.link(shape, "", c, "Shape")
    # Emissive = Color × Intensity, times the shape's alpha; additive also
    # times Opacity (additive ignores the opacity pin).
    lit = g.binop(unreal.MaterialExpressionMultiply, color, intensity)
    shaped = g.binop(unreal.MaterialExpressionMultiply, lit, c)
    if blend == unreal.BlendMode.BLEND_ADDITIVE:
        shaped = g.binop(unreal.MaterialExpressionMultiply, shaped, opacity)
    else:
        alpha = g.binop(unreal.MaterialExpressionMultiply, opacity, c)
        g.out(alpha, "", unreal.MaterialProperty.MP_OPACITY)
    g.out(shaped, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.layout_material_expressions(mat)
    MEL.recompile_material(mat)
    EAL.set_metadata_tag(mat, "AcMadeBy", "Tools/Editor/make_pointer_materials.py")
    EAL.save_loaded_asset(mat)
    log("master %s" % path)


def main():
    MM.ensure_dir(A.MASTER_DIR)
    build(A.MASTER_DIR + "/M_AcPointer", unreal.BlendMode.BLEND_TRANSLUCENT, False)
    build(A.MASTER_DIR + "/M_AcPointerAdd", unreal.BlendMode.BLEND_ADDITIVE, False)
    build(A.MASTER_DIR + "/M_AcPointerTop", unreal.BlendMode.BLEND_TRANSLUCENT, True)


if __name__ == "__main__":
    main()
