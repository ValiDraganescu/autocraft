"""The console's UI materials (GAME-LAYER.md §2.4, chunk D2). Rerunnable.

Output:
  /Game/UI/Materials/M_AcUiAdditive
      User-interface domain, additive: Tex × the Slate tint (vertex colour),
      premultiplied by both alphas. SpriteKit's `blendMode = .add`, which
      Slate's own brushes lack: the selection portrait's glow
      (`Console.buildCenter`) draws through it (`SAcConsole`, a dynamic
      instance per icon with `Tex` set).

Run in the editor (py "<repo>/unreal/Tools/Editor/make_console_materials.py")
or headless: UnrealEditor Autocraft.uproject -run=pythonscript -script=<abs path>
"""
import unreal

DEST = "/Game/UI/Materials"
NAME = "M_AcUiAdditive"
PATH = f"{DEST}/{NAME}"

mel = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary


def make():
    if assets.does_asset_exist(PATH):
        mat = unreal.load_asset(PATH)
        mel.delete_all_material_expressions(mat)
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            NAME, DEST, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_UI)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)

    tex = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -700, 0)
    tex.set_editor_property("parameter_name", "Tex")
    tex.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    default = unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture")
    if default:
        tex.set_editor_property("texture", default)
    vc = mel.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -700, 300)

    rgb = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -400, 0)
    mel.connect_material_expressions(tex, "RGB", rgb, "A")
    mel.connect_material_expressions(vc, "", rgb, "B")
    alpha = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -400, 250)
    mel.connect_material_expressions(tex, "A", alpha, "A")
    mel.connect_material_expressions(vc, "A", alpha, "B")
    out = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -200, 0)
    mel.connect_material_expressions(rgb, "", out, "A")
    mel.connect_material_expressions(alpha, "", out, "B")
    mel.connect_material_property(out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)
    mel.recompile_material(mat)
    assets.save_asset(PATH, only_if_is_dirty=False)
    unreal.log(f"make_console_materials: {PATH}")


make()
