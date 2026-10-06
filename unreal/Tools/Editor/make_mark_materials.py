"""M_AcMark and M_AcGlowMark: chunk C2's flat marks (GAME-LAYER.md §2.7:
decals, scorch, shock rings, the slow marker). Drawn by
Source/Autocraft/AcEffectsMarks.cpp on the engine plane
(/Engine/BasicShapes/Plane, UV 0-1) laid on the ground.

Rerunnable: rebuilds both graphs from nothing each time.

- /Game/Materials/M_AcMark: unlit, translucent (alpha blend), both sides.
  A soft round disc with the Swift `lib.smoke` profile (alpha 1 inside 0.15
  of the radius, then linearly to 0 at the rim: `MaterialLibrary.radial`).
  Per-instance custom data: 0-2 the colour (linear), 3 the alpha. Scorch
  marks (`Effects.scorch`), decals (`Effects.decal`: blood, a blast's mark).
- /Game/Materials/M_AcGlowMark: unlit, additive, both sides. Per-instance
  custom data: 0-2 the colour (linear, intensity and fade folded in), 3 the
  pipe: > 0 draws a ring (an `SCNTorus` seen from above: radius 1, half
  width `pipe`, on a plane spanning 1 + pipe), 0 draws the smoke disc. The
  shock ring (`Effects.shockRing`), the slow marker's ring and soft glow
  (`Effects.makeSlowMarker`).

Run in the editor (`py "<repo>/unreal/Tools/Editor/make_mark_materials.py"`)
or in a commandlet while the live editor does not have the assets open:
  UnrealEditor-Cmd unreal/Autocraft.uproject -run=pythonscript \
      -script="$PWD/unreal/Tools/Editor/make_mark_materials.py" -unattended -nullrhi
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ac_models as A  # noqa: E402
import make_materials as MM  # noqa: E402

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

M_MARK = A.MASTER_DIR + "/M_AcMark"
M_GLOW_MARK = A.MASTER_DIR + "/M_AcGlowMark"

F1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1

# MaterialLibrary.radial(size: 128, inner: 0.15): alpha 1 to 0.15 of the
# radius, then down to 0 at the rim.
MARK_HLSL = r"""
float d = length(UV - 0.5) * 2.0;
Opacity = C.a * saturate((1.0 - d) / 0.85);
return C.rgb;
"""

GLOW_HLSL = r"""
float d = length(UV - 0.5) * 2.0;
float m;
if (Pipe > 0.0) {
    // The plane spans the ring's outer edge: radius 1 + pipe.
    float r = d * (1.0 + Pipe);
    float e = Pipe - abs(r - 1.0);
    m = saturate(e / max(fwidth(r), 1e-4) + 0.5);
} else {
    m = saturate((1.0 - d) / 0.85);
}
return C * m;
"""


def log(msg):
    unreal.log("[make_mark_materials] " + msg)


def rgba(g):
    """Custom data 0-3 as one float4."""
    ch = [g.custom_data(i, n, d) for i, n, d in ((0, "R", 1.0), (1, "G", 1.0), (2, "B", 1.0), (3, "A", 1.0))]
    a = g.node(unreal.MaterialExpressionAppendVector)
    g.link(ch[0], "", a, "A")
    g.link(ch[1], "", a, "B")
    b = g.node(unreal.MaterialExpressionAppendVector)
    g.link(a, "", b, "A")
    g.link(ch[2], "", b, "B")
    c = g.node(unreal.MaterialExpressionAppendVector)
    g.link(b, "", c, "A")
    g.link(ch[3], "", c, "B")
    return c, ch


def start(path, blend):
    mat = MM.load(path) or MM.create(path, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", blend)
    mat.set_editor_property("two_sided", True)
    return mat, MM.Graph(mat)


def finish(mat, path):
    MEL.set_base_material_usage(mat, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    MEL.layout_material_expressions(mat)
    MEL.recompile_material(mat)
    EAL.set_metadata_tag(mat, "AcMadeBy", "Tools/Editor/make_mark_materials.py")
    EAL.save_loaded_asset(mat)
    log("made %s" % path)


def build_mark():
    mat, g = start(M_MARK, unreal.BlendMode.BLEND_TRANSLUCENT)
    c4, _ = rgba(g)
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    c = g.custom("Mark", MARK_HLSL, ["UV", "C"])
    o = unreal.CustomOutput()
    o.set_editor_property("output_name", "Opacity")
    o.set_editor_property("output_type", F1)
    c.set_editor_property("additional_outputs", [o])
    g.link(uv, "", c, "UV")
    g.link(c4, "", c, "C")
    g.out(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.out(c, "Opacity", unreal.MaterialProperty.MP_OPACITY)
    finish(mat, M_MARK)


def build_glow_mark():
    mat, g = start(M_GLOW_MARK, unreal.BlendMode.BLEND_ADDITIVE)
    _, ch = rgba(g)
    a = g.node(unreal.MaterialExpressionAppendVector)
    g.link(ch[0], "", a, "A")
    g.link(ch[1], "", a, "B")
    rgb = g.node(unreal.MaterialExpressionAppendVector)
    g.link(a, "", rgb, "A")
    g.link(ch[2], "", rgb, "B")
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    c = g.custom("GlowMark", GLOW_HLSL, ["UV", "C", "Pipe"])
    g.link(uv, "", c, "UV")
    g.link(rgb, "", c, "C")
    g.link(ch[3], "", c, "Pipe")
    g.out(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(mat, M_GLOW_MARK)


def main():
    MM.ensure_dir(A.MASTER_DIR)
    build_mark()
    build_glow_mark()


if __name__ == "__main__":
    main()
