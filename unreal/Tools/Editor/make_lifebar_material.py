"""M_LifeBar: the life bars' material (chunk D6, GAME-LAYER.md §2.6 "Life bars").

Rerunnable: rebuilds the material's graph from nothing each time.

The Swift `LifeBars` shader modifier (Sources/Autocraft/LifeBars.swift) as one
Custom HLSL node: a dark frame round a fill that runs green (> 0.6), yellow
(> 0.3), then red as it empties, lit from above, cut by tick marks into
segments of equal hit points, and a dark track where the fill is gone.
Unlit, translucent, drawn over everything (depth test off), no fog, both
sides. UV u runs left to right, v top to bottom.

Per-instance custom data (AAcLifeBars, Source/Autocraft/AcLifeBars.cpp):
  0 Frac    the fill, 0-1
  1 Segs    segments (max hp / hp per segment)
  2 Aspect  the quad's width over its height, so the frame is as thick at
            the ends as along the sides

Run it in a commandlet while the live editor does not have the asset open:
  UnrealEditor-Cmd unreal/Autocraft.uproject -run=pythonscript \
      -script="$PWD/unreal/Tools/Editor/make_lifebar_material.py" -unattended -nullrhi
or in the editor (`py "<repo>/unreal/Tools/Editor/make_lifebar_material.py"`).
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ac_models as A  # noqa: E402
import make_materials as MM  # noqa: E402

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

M_LIFEBAR = A.MASTER_DIR + "/M_LifeBar"

F1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1

# LifeBars.swift's fragment modifier, line for line. Returns the colour;
# `Opacity` is the alpha (SceneKit's output was premultiplied; Unreal's
# translucent blend multiplies emissive by opacity itself).
HLSL = r"""
float edge = 0.17;
float eu = edge / max(Aspect, 0.01);
float4 c;
if (UV.x < eu || UV.x > 1.0 - eu || UV.y < edge || UV.y > 1.0 - edge) {
    c = float4(0.0, 0.0, 0.0, 0.92);
} else {
    float u = (UV.x - eu) / (1.0 - 2.0 * eu);
    float v = (UV.y - edge) / (1.0 - 2.0 * edge);
    if (u <= Frac) {
        float3 fill = Frac > 0.6 ? float3(0.2, 0.95, 0.3)
            : Frac > 0.3 ? float3(1.0, 0.82, 0.15) : float3(1.0, 0.22, 0.14);
        // Lit from above.
        fill *= 1.25 - 0.55 * v;
        float s = u * Segs;
        float w = fwidth(s) * 1.2;
        bool tick = s > 0.5 && frac(s) < w && u < Frac - 0.01;
        c = float4(tick ? fill * 0.3 : fill, 1.0);
    } else {
        c = float4(0.02, 0.025, 0.03, 0.9);
    }
}
Opacity = c.a;
return c.rgb;
"""


def log(msg):
    unreal.log("[make_lifebar_material] " + msg)


def build():
    mat = MM.load(M_LIFEBAR) or MM.create(M_LIFEBAR, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("two_sided", True)
    # Over everything, as SceneKit's readsFromDepthBuffer = false.
    mat.set_editor_property("disable_depth_test", True)
    # Neither fogged nor hazed: a HUD mark in the world.
    mat.set_editor_property("use_translucency_vertex_fog", False)
    # Thin quads (a one-pixel frame at the default zoom): composited after
    # TSR and motion blur, so the temporal upscaler does not wash the frame
    # into the ground.
    mat.set_editor_property("translucency_pass", unreal.MaterialTranslucencyPass.MTP_AFTER_MOTION_BLUR)
    mat.set_editor_property("enable_responsive_aa", True)
    g = MM.Graph(mat)
    frac = g.custom_data(0, "Frac", 1.0)
    segs = g.custom_data(1, "Segs", 5.0)
    aspect = g.custom_data(2, "Aspect", 10.0)
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    c = g.custom("LifeBar", HLSL, ["UV", "Frac", "Segs", "Aspect"])
    o = unreal.CustomOutput()
    o.set_editor_property("output_name", "Opacity")
    o.set_editor_property("output_type", F1)
    c.set_editor_property("additional_outputs", [o])
    g.link(uv, "", c, "UV")
    g.link(frac, "", c, "Frac")
    g.link(segs, "", c, "Segs")
    g.link(aspect, "", c, "Aspect")
    g.out(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.out(c, "Opacity", unreal.MaterialProperty.MP_OPACITY)
    MEL.set_base_material_usage(mat, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    MEL.layout_material_expressions(mat)
    MEL.recompile_material(mat)
    EAL.set_metadata_tag(mat, "AcMadeBy", "Tools/Editor/make_lifebar_material.py")
    EAL.save_loaded_asset(mat)
    log("made %s" % M_LIFEBAR)
    return mat


if __name__ == "__main__":
    build()
