"""The driven unit's aids in the world (GAME-LAYER.md §2.12, chunk E4), drawn
by AAcPilotAids (Source/Autocraft/AcPilotAids.cpp).

Rerunnable: each material's graph is rebuilt from nothing every time.

- /Game/Pilot/M_AcRangeRing: the range ring (`RangeRing.swift`): unlit,
  additive, two-sided, no depth write (translucent), so units and buildings
  stand over it. Emissive = `Color` (linear, set per section by the actor)
  x the vertex colour (the disc's glow toward its rim; 1 on the bands).
- /Game/Pilot/M_AcPilotVeil: the pilot's fog veil (`FogOfWar.veil`, the
  `skyHaze.w > 0` branch of `FogOfWar.shader`), a post-process material
  before tonemapping: each pixel's world point from the depth; clear to a
  quarter of `Center.w` (cm) from `Center.xyz` (the driven unit), the sky
  toward the pixel (`SkyAtmosphereViewLuminance`, pre-exposed) from
  `Center.w` on, under Swift's haze rising from the horizon (`Haze`: rgb
  linear, a = how high it rises, degrees; the same as on M_AcSky); the sky
  itself (farther than 10 km) left alone. Then the
  Swift pilot camera's vignette, `1 - Vignette * (r / r_corner)^2.2` (0.55),
  taken to linear light (^2.2) since this runs before the tonemapper.

Run it in the editor (`py "<repo>/unreal/Tools/Editor/make_pilot_materials.py"`),
or headless while the live editor does not have the assets open:
  UnrealEditor-Cmd unreal/Autocraft.uproject -run=pythonscript \
      -script="$PWD/unreal/Tools/Editor/make_pilot_materials.py" -unattended
"""
import unreal

MAT_DIR = "/Game/Pilot"

mel = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary

VEIL_HLSL = r"""
float d = length(W - Center.xyz);
float lost = d < 1.0e6 ? smoothstep(Center.w * 0.25, Center.w, d) : 0.0;
// Swift's haze rising from the horizon (the sky dome's, M_AcSky): the far
// ground goes to the same colour as the sky just over it.
float3 toward = normalize(W - Cam);
float e = max(asin(clamp(toward.z, -1.0, 1.0)) * 57.29578, 0.0);
float rise = pow(1.0 - smoothstep(0.0, max(Haze.a, 1.0), e), 1.6);
float3 s = lerp(Sky, Haze.rgb, rise) * View.PreExposure;
float2 ndc = UV * 2.0 - 1.0;
float2 aspect = float2(Size.x / max(Size.y, 1.0), 1.0);
float v = saturate(1.0 - Vignette * pow(length(ndc * aspect) / length(aspect), 2.2));
return lerp(C.rgb, s, lost) * pow(v, 2.2);
"""

LOOK_HLSL = r"""
float3 d = normalize(W - Cam);
d.z = max(d.z, 0.02);
return normalize(d);
"""


def log(msg):
    unreal.log("[make_pilot_materials] " + msg)


def fresh(name):
    path = f"{MAT_DIR}/{name}"
    mat = unreal.load_asset(path) if assets.does_asset_exist(path) else None
    if mat is None:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    mel.delete_all_material_expressions(mat)
    return path, mat


def connect(a, a_out, b, b_in):
    if not mel.connect_material_expressions(a, a_out, b, b_in):
        raise RuntimeError(f"could not connect {a_out} -> {b_in}")


def build_ring():
    path, mat = fresh("M_AcRangeRing")
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)

    color = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -500, -100)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.1, 0.4, 0.6, 1))
    vc = mel.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -500, 100)
    mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -250, 0)
    connect(color, "", mul, "A")
    connect(vc, "", mul, "B")
    if not mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError("could not connect emissive")
    mel.recompile_material(mat)
    assets.save_asset(path, only_if_is_dirty=False)
    return mat


def build_veil():
    path, mat = fresh("M_AcPilotVeil")
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    mat.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_DOF)

    def node(cls, x, y):
        return mel.create_material_expression(mat, cls, x, y)

    scene = node(unreal.MaterialExpressionSceneTexture, -800, -300)
    scene.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    world = node(unreal.MaterialExpressionWorldPosition, -800, -160)
    # The sky toward the pixel, never below the horizon (there the
    # atmosphere shows the planet's dark ground; Swift's sky is its haze).
    cam = node(unreal.MaterialExpressionCameraPositionWS, -1300, -40)
    look = node(unreal.MaterialExpressionCustom, -1050, -40)
    look.set_editor_property("code", LOOK_HLSL)
    look.set_editor_property("description", "toward the pixel, over the horizon")
    look.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    look_inputs = []
    for name in ("W", "Cam"):
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", name)
        look_inputs.append(ci)
    look.set_editor_property("inputs", look_inputs)
    sky = node(unreal.MaterialExpressionSkyAtmosphereViewLuminance, -800, -40)
    uv = node(unreal.MaterialExpressionScreenPosition, -800, 80)
    size = node(unreal.MaterialExpressionViewSize, -800, 200)
    center = node(unreal.MaterialExpressionVectorParameter, -800, 320)
    center.set_editor_property("parameter_name", "Center")
    center.set_editor_property("default_value", unreal.LinearColor(0, 0, 0, 11000))
    vignette = node(unreal.MaterialExpressionScalarParameter, -800, 480)
    vignette.set_editor_property("parameter_name", "Vignette")
    vignette.set_editor_property("default_value", 0.55)

    custom = node(unreal.MaterialExpressionCustom, -300, 0)
    custom.set_editor_property("code", VEIL_HLSL)
    custom.set_editor_property("description", "Swift pilot fog veil")
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    inputs = []
    haze_p = node(unreal.MaterialExpressionVectorParameter, -800, 600)
    haze_p.set_editor_property("parameter_name", "Haze")
    haze_p.set_editor_property("default_value", unreal.LinearColor(0.34, 0.32, 0.3, 12.0))
    haze4 = node(unreal.MaterialExpressionAppendVector, -550, 600)
    connect(haze_p, "", haze4, "A")
    connect(haze_p, "A", haze4, "B")
    for name in ("C", "W", "Sky", "UV", "Size", "Center", "Vignette", "Cam", "Haze"):
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", name)
        inputs.append(ci)
    custom.set_editor_property("inputs", inputs)
    connect(world, "", look, "W")
    connect(cam, "", look, "Cam")
    connect(look, "", sky, "WorldDirection")
    connect(scene, "Color", custom, "C")
    connect(world, "", custom, "W")
    connect(sky, "", custom, "Sky")
    connect(uv, "ViewportUV", custom, "UV")
    connect(size, "", custom, "Size")
    # A vector parameter's default output is its RGB: append A for float4.
    center4 = node(unreal.MaterialExpressionAppendVector, -550, 320)
    connect(center, "", center4, "A")
    connect(center, "A", center4, "B")
    connect(center4, "", custom, "Center")
    connect(vignette, "", custom, "Vignette")
    connect(cam, "", custom, "Cam")
    connect(haze4, "", custom, "Haze")
    if not mel.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError("could not connect emissive")
    mel.recompile_material(mat)
    assets.save_asset(path, only_if_is_dirty=False)
    return mat


log(f"built {build_ring().get_path_name()}")
log(f"built {build_veil().get_path_name()}")
