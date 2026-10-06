"""M_Fog: the fog of war's top-down pass (GAME-LAYER.md §2.5, chunk B10).

Rerunnable: rebuilds the material's graph from nothing each time.

Output: /Game/Fog/M_Fog, a post-process material that AAcFog
(Source/Autocraft/AcFog.cpp) puts on an unbound post-process component, the
port of the Swift pass (`FogOfWar.shader`, Sources/Autocraft/FogOfWar.swift):

- each pixel's ground point is the absolute world position from the scene
  depth (Unreal cm, Z up; sim cells = XY / 100);
- five taps of the fog texture about 1.4 texels apart (0.4 centre, 0.15 each
  diagonal), the level lifting to 1 over 4 cells past the map's edge;
- the colour: `mix(grey · (0.86, 0.92, 1.06), c, f) · f`, grey the
  luma (0.3, 0.59, 0.11).

Parameters (set by AAcFog): `Fog` (texture object: the levels, linear BGRA8),
`Rect` (origin x, origin y in cells, 1 / width, 1 / height in cells),
`Texel` (1 / texels wide, 1 / texels high).

Where in the frame (`LOCATION`): after tonemapping, on the display colour
decoded to linear (gamma 2.2) and encoded back, as SceneKit's technique
read its sRGB colour target as linear after its own tone mapping. Checked
against the Swift bench shots (fog on / `AUTOCRAFT_FOG=off`): the ratio of
fogged to clear pixels is 0.64 (never seen) and 0.8 (explored) in Swift;
before Unreal's tonemapper it came out 0.52 and 0.72 (its toe crushes the
darkened ground).

Run it in the editor (`py <path>` in the console), or headless while the live
editor does not have the asset open:
  UnrealEditor-Cmd Autocraft.uproject -run=pythonscript -script=<abs path>
"""
import unreal

MAT_DIR = "/Game/Fog"
MAT_NAME = "M_Fog"
LOCATION = unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING

mel = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary

FOG_HLSL = r"""
float2 t = (W.xy / 100.0 - Rect.xy) * Rect.zw;
float2 e = Texel.xy * 1.4;
float f = Texture2DSampleLevel(Fog, FogSampler, t, 0).r * 0.4
    + (Texture2DSampleLevel(Fog, FogSampler, t + float2(e.x, e.y), 0).r
     + Texture2DSampleLevel(Fog, FogSampler, t - float2(e.x, e.y), 0).r
     + Texture2DSampleLevel(Fog, FogSampler, t + float2(e.x, -e.y), 0).r
     + Texture2DSampleLevel(Fog, FogSampler, t - float2(e.x, -e.y), 0).r) * 0.15;
float past = length(max(max(-t, t - 1.0), 0.0) / Rect.zw);
f = lerp(f, 1.0, smoothstep(0.0, 4.0, past));
if (f >= 0.999) return C.rgb;
float3 c = pow(max(C.rgb, 0.0), 2.2);
float grey = dot(c, float3(0.3, 0.59, 0.11));
return pow(max(lerp(grey * float3(0.86, 0.92, 1.06), c, f) * f, 0.0), 1.0 / 2.2);
"""


def build():
    path = f"{MAT_DIR}/{MAT_NAME}"
    mat = unreal.load_asset(path) if assets.does_asset_exist(path) else None
    if mat is None:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            MAT_NAME, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    mel.delete_all_material_expressions(mat)
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    mat.set_editor_property("blendable_location", LOCATION)

    def node(cls, x, y):
        return mel.create_material_expression(mat, cls, x, y)

    def connect(a, a_out, b, b_in):
        if not mel.connect_material_expressions(a, a_out, b, b_in):
            raise RuntimeError(f"could not connect {b_in}")

    scene = node(unreal.MaterialExpressionSceneTexture, -700, -200)
    scene.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    world = node(unreal.MaterialExpressionWorldPosition, -700, -60)

    fog = node(unreal.MaterialExpressionTextureObjectParameter, -700, 60)
    fog.set_editor_property("parameter_name", "Fog")
    fog.set_editor_property("texture", unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture"))
    fog.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)

    rect = node(unreal.MaterialExpressionVectorParameter, -700, 200)
    rect.set_editor_property("parameter_name", "Rect")
    rect.set_editor_property("default_value", unreal.LinearColor(0, 0, 1, 1))
    texel = node(unreal.MaterialExpressionVectorParameter, -700, 360)
    texel.set_editor_property("parameter_name", "Texel")
    texel.set_editor_property("default_value", unreal.LinearColor(0.01, 0.01, 0, 0))

    custom = node(unreal.MaterialExpressionCustom, -300, 0)
    custom.set_editor_property("code", FOG_HLSL)
    custom.set_editor_property("description", "Swift fog of war")
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    inputs = []
    for name in ("C", "W", "Fog", "Rect", "Texel"):
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", name)
        inputs.append(ci)
    custom.set_editor_property("inputs", inputs)
    connect(scene, "Color", custom, "C")
    connect(world, "", custom, "W")
    connect(fog, "", custom, "Fog")
    # A vector parameter's default output is its RGB: append A for float4.
    rect4 = node(unreal.MaterialExpressionAppendVector, -500, 200)
    connect(rect, "", rect4, "A")
    connect(rect, "A", rect4, "B")
    connect(rect4, "", custom, "Rect")
    connect(texel, "", custom, "Texel")
    if not mel.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError("could not connect emissive")

    mel.recompile_material(mat)
    assets.save_asset(path, only_if_is_dirty=False)
    return mat


unreal.log(f"make_fog_material: built {build().get_path_name()}")
