"""M_AcCloud / MI_AcCloud: the volumetric clouds, thinning out toward the horizon (B9).

Rerunnable: the material is copied afresh from the engine's simple cloud
material each time, then edited.

Swift's sky (`SkyShader.sky_color`, Sources/Autocraft/SkyShader.swift) has its
cloud layer overhead only: `cover *= smoothstep(0.05, 0.3, d.y)`, so no cloud
below about 3° and the full layer from about 17° up, and the haze rising from
the horizon under it. Unreal's volumetric layer instead ran on to its tracing
distance (50 km) and stopped there along a flat line a few degrees over the
horizon, and the pilot's fog veil (E4), which paints the far ground with the
atmosphere's sky, showed the far mountains as a pale outline cut into the
clouds behind them.

So the copy multiplies the cloud's extinction (a volume material's
`SubsurfaceColor` pin) by the same smoothstep of the view ray's height:
`smoothstep(0.05, 0.3, -CameraVector.z)`. Cheap (per sample, no texture), and
the cloud shapes overhead are unchanged.

Output: /Game/Sky/M_AcCloud and its instance /Game/Sky/MI_AcCloud with the
engine instance's textures (`m_SimpleVolumetricCloud_Inst`). AAcDaylight
(Source/Autocraft/AcDaylight.cpp) loads MI_AcCloud, else the engine's.

Run headless while the live editor does not have the assets open:
  UnrealEditor-Cmd Autocraft.uproject -run=pythonscript -script=<abs path>
"""
import unreal

SRC = "/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud"
SRC_INST = "/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst"
DIR = "/Game/Sky"
MAT = f"{DIR}/M_AcCloud"
INST = f"{DIR}/MI_AcCloud"
# Swift: smoothstep(0.05, 0.3, d.y), d the unit view direction (+y up).
FADE_LO, FADE_HI = 0.05, 0.3

mel = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary


def build():
    if assets.does_asset_exist(INST):
        assets.delete_asset(INST)
    if assets.does_asset_exist(MAT):
        assets.delete_asset(MAT)
    # The engine content is not in a commandlet's asset registry: copy the
    # loaded object.
    mat = unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset(
        "M_AcCloud", DIR, unreal.load_asset(SRC))
    if mat is None:
        raise RuntimeError(f"could not copy {SRC}")

    ext = unreal.MaterialProperty.MP_SUBSURFACE_COLOR
    src_node = mel.get_material_property_input_node(mat, ext)
    if src_node is None:
        raise RuntimeError("the cloud material has no extinction input")
    src_out = mel.get_material_property_input_node_output_name(mat, ext) or ""

    def node(cls, x, y):
        return mel.create_material_expression(mat, cls, x, y)

    # The view ray's height: CameraVector points from the sample to the eye.
    cam = node(unreal.MaterialExpressionCameraVectorWS, -900, 1400)
    mask = node(unreal.MaterialExpressionComponentMask, -750, 1400)
    for c, on in (("r", False), ("g", False), ("b", True), ("a", False)):
        mask.set_editor_property(c, on)
    mel.connect_material_expressions(cam, "", mask, "")
    neg = node(unreal.MaterialExpressionMultiply, -600, 1400)
    neg.set_editor_property("const_b", -1.0)
    mel.connect_material_expressions(mask, "", neg, "A")
    fade = node(unreal.MaterialExpressionSmoothStep, -450, 1400)
    fade.set_editor_property("const_min", FADE_LO)
    fade.set_editor_property("const_max", FADE_HI)
    if not mel.connect_material_expressions(neg, "", fade, "Value"):
        raise RuntimeError("could not connect the smoothstep")
    mul = node(unreal.MaterialExpressionMultiply, -250, 1300)
    if not mel.connect_material_expressions(src_node, src_out, mul, "A"):
        raise RuntimeError("could not re-route the extinction")
    mel.connect_material_expressions(fade, "", mul, "B")
    if not mel.connect_material_property(mul, "", ext):
        raise RuntimeError("could not connect the extinction")
    mel.recompile_material(mat)
    assets.save_asset(MAT, only_if_is_dirty=False)

    # The engine instance's overrides (its textures), on the copy.
    src_inst = unreal.load_asset(SRC_INST)
    mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "MI_AcCloud", DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, mat)
    for p in src_inst.get_editor_property("scalar_parameter_values"):
        mel.set_material_instance_scalar_parameter_value(mi, p.parameter_info.name, p.parameter_value)
    for p in src_inst.get_editor_property("vector_parameter_values"):
        mel.set_material_instance_vector_parameter_value(mi, p.parameter_info.name, p.parameter_value)
    for p in src_inst.get_editor_property("texture_parameter_values"):
        mel.set_material_instance_texture_parameter_value(mi, p.parameter_info.name, p.parameter_value)
    mel.update_material_instance(mi)
    assets.save_asset(INST, only_if_is_dirty=False)
    return mi


unreal.log(f"make_cloud_material: built {build().get_path_name()}")
