"""/Game/Materials/M_AcOutline: the outline pass (Source/Autocraft/AcOutline.h,
GAME-LAYER.md "Outline pass"). A post-process material at "after DOF" (before
TSR, so the line is anti-aliased with the picture and the custom stencil, which
is jittered with it, lines up). Rerunnable: the graph is rebuilt from nothing.

For each pixel it reads the custom stencil there and at four neighbours
`Width` pixels away. A pixel with no stencil value of its own next to one that
has draws the line, outside the shape; the picture is left alone everywhere
else. The line's colour comes from the neighbour's value (the largest wins):

    1..8   team N-1: `PaintN` of /Game/Materials/MPC_AcTeams, at `TeamStrength`
    16     foe: `FoeColor` (red), at `FoeStrength`

The colour is an sRGB colour taken to linear light and scaled by `Gain` and
the view's pre-exposure (this runs before the tonemapper). Parameters:
`Width` (pixels), `Gain`, `TeamStrength`, `FoeStrength`, `FoeColor`.

Needs `r.CustomDepth=3` in the project (DefaultEngine.ini): the stencil is
written only then. The pass is switched off by the game while nothing has a
stencil value (`AAcWorld::Outline`).

    UnrealEditorBG <abs>/unreal/Autocraft.uproject -run=pythonscript \
        -script=<abs>/unreal/Tools/Editor/make_outline_material.py -unattended -nullrhi -nosplash -nosound -abslog=<log>
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ac_models as A  # noqa: E402
import make_materials as MM  # noqa: E402

PATH = A.MASTER_DIR + "/M_AcOutline"
MEL = unreal.MaterialEditingLibrary
F2 = unreal.CustomMaterialOutputType.CMOT_FLOAT2

OFFSET_HLSL = """
return UV + float2(%s) * Inv * Width;
"""

LINE_HLSL = r"""
int c0 = (int)(S0.r + 0.5);
if (c0 != 0) return C.rgb;
int n = max(max((int)(SA.r + 0.5), (int)(SB.r + 0.5)), max((int)(SC.r + 0.5), (int)(SD.r + 0.5)));
if (n == 0) return C.rgb;
float3 line_srgb;
float a;
if (n >= 16)
{
    line_srgb = FoeColor.rgb;
    a = FoeStrength;
}
else
{
    int t = clamp(n - 1, 0, 7);
    line_srgb = P0;
    if (t == 1) line_srgb = P1; else if (t == 2) line_srgb = P2; else if (t == 3) line_srgb = P3;
    else if (t == 4) line_srgb = P4; else if (t == 5) line_srgb = P5; else if (t == 6) line_srgb = P6;
    else if (t == 7) line_srgb = P7;
    a = TeamStrength;
}
float3 lin = pow(max(line_srgb, 0.0), 2.2) * Gain * View.PreExposure;
return lerp(C.rgb, lin, saturate(a));
"""


def main():
    mat = MM.load(PATH) or MM.create(PATH, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    mat.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_DOF)
    g = MM.Graph(mat)
    mpc = MM.load(A.MPC_TEAMS)

    scene = g.node(unreal.MaterialExpressionSceneTexture, x=-1400,
                   scene_texture_id=unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    uv = g.node(unreal.MaterialExpressionScreenPosition, x=-1400)
    width = g.scalar("Width", 1.5, "Outline")
    gain = g.scalar("Gain", 4.0, "Outline")
    team_strength = g.scalar("TeamStrength", 0.55, "Outline")
    foe_strength = g.scalar("FoeStrength", 0.95, "Outline")
    foe_color = g.vector("FoeColor", (1.0, 0.22, 0.14), "Outline")

    def stencil():
        return g.node(unreal.MaterialExpressionSceneTexture, x=-700,
                      scene_texture_id=unreal.SceneTextureId.PPI_CUSTOM_STENCIL)

    centre = stencil()
    g.link(uv, "ViewportUV", centre, "UVs")
    inv = g.node(unreal.MaterialExpressionSceneTexture, x=-1100,
                 scene_texture_id=unreal.SceneTextureId.PPI_CUSTOM_STENCIL)
    taps = []
    for d in ("1.0, 0.0", "-1.0, 0.0", "0.0, 1.0", "0.0, -1.0"):
        off = g.custom("AcOutlineTap", OFFSET_HLSL % d, ["UV", "Inv", "Width"], F2)
        g.link(uv, "ViewportUV", off, "UV")
        g.link(inv, "InvSize", off, "Inv")
        g.link(width, "", off, "Width")
        s = stencil()
        g.link(off, "", s, "UVs")
        taps.append(s)

    line = g.custom("AcOutlineLine", LINE_HLSL,
                    ["C", "S0", "SA", "SB", "SC", "SD", "FoeColor", "FoeStrength", "TeamStrength", "Gain"]
                    + ["P%d" % i for i in range(8)])
    g.link(scene, "Color", line, "C")
    g.link(centre, "Color", line, "S0")
    for name, s in zip(("SA", "SB", "SC", "SD"), taps):
        g.link(s, "Color", line, name)
    g.link(foe_color, "", line, "FoeColor")
    g.link(foe_strength, "", line, "FoeStrength")
    g.link(team_strength, "", line, "TeamStrength")
    g.link(gain, "", line, "Gain")
    for i in range(8):
        g.link(g.mpc(mpc, "Paint%d" % i), "", line, "P%d" % i)
    g.out(line, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    unreal.log("[make_outline_material] " + PATH)


main()
