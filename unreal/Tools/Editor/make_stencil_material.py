"""/Game/Materials/M_AcStencil: the material of the stencil renders (AAcModelRow, -AcModelView, pipeline skill
step 2). One lit material with two parameters: `Color` and `Flat`. Flat 0 is grey clay (the colour as base
colour, rough, lit); Flat 1 is a flat, unlit colour (the part-colour sheet). Rerunnable.

    UnrealEditorBG <abs>/unreal/Autocraft.uproject -run=pythonscript \
        -script=<abs>/unreal/Tools/Editor/make_stencil_material.py -unattended -nullrhi -nosplash -nosound -abslog=<log>
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_materials as MM  # noqa: E402

PATH = "/Game/Materials/M_AcStencil"
MEL = unreal.MaterialEditingLibrary


def main():
    mat = MM.load(PATH) or MM.create(PATH, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    g = MM.Graph(mat)
    color = g.vector("Color", (0.62, 0.62, 0.62), "Stencil")
    flat = g.scalar("Flat", 0.0, "Stencil")
    inv = g.node(unreal.MaterialExpressionOneMinus, x=-500)
    g.link(flat, "", inv, "")
    base = g.binop(unreal.MaterialExpressionMultiply, color, inv)
    emis = g.binop(unreal.MaterialExpressionMultiply, color, flat)
    rough = g.node(unreal.MaterialExpressionConstant, x=-300, r=0.85)
    g.out(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    spec = g.node(unreal.MaterialExpressionConstant, x=-300, r=0.0)
    g.out(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    for usage in (unreal.MaterialUsage.MATUSAGE_NANITE, unreal.MaterialUsage.MATUSAGE_STATIC_LIGHTING):
        MEL.set_base_material_usage(mat, usage)
    MEL.layout_material_expressions(mat)
    MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    unreal.log("[make_stencil_material] " + PATH)


main()
