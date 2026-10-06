"""The debug level /Game/Maps/ModelRow (chunks A4/A5): every model in rows at
its rest pose (an AAcModelRow actor, Source/Autocraft/AcModelRow.h), under
a sky, a sun and a fixed exposure, with a grey floor. Rerunnable: the
level is emptied and refilled.

Needs the Autocraft module built (AAcModelRow) and import_models.py run.

    UnrealEditor unreal/Autocraft.uproject -run=pythonscript \
        -script="$PWD/unreal/Tools/Editor/make_model_row.py" -unattended -nullrhi

Look at one model the way `Autocraft export-models --compare` draws it:

    UnrealEditor unreal/Autocraft.uproject /Game/Maps/ModelRow -game \
        -RenderOffscreen -ResX=900 -ResY=900 -unattended -nosplash \
        -AcNoSave -AcNoAI -AcPaused -AcModelFocus=ranger_blue -AcShot=/abs/out.png
"""
import unreal

LEVEL = "/Game/Maps/ModelRow"

EAL = unreal.EditorAssetLibrary
LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ACT = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def log(msg):
    unreal.log("[model_row] " + msg)


def spawn(cls, loc=(0, 0, 0), rot=(0, 0, 0), label=None):
    a = ACT.spawn_actor_from_class(cls, unreal.Vector(*loc), unreal.Rotator(*rot))
    if label:
        a.set_actor_label(label)
    return a


def main():
    if EAL.does_asset_exist(LEVEL):
        LES.load_level(LEVEL)
        for a in ACT.get_all_level_actors():
            if not isinstance(a, unreal.WorldSettings):
                ACT.destroy_actor(a)
    else:
        if not EAL.does_directory_exist("/Game/Maps"):
            EAL.make_directory("/Game/Maps")
        LES.new_level(LEVEL)

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    ws = world.get_world_settings()
    # Not the game's mode: nothing but the row.
    ws.set_editor_property("default_game_mode", unreal.GameModeBase)

    # Rotator(roll, pitch, yaw).
    sun = spawn(unreal.DirectionalLight, (0, 0, 1000), (0, -45, 35), "Sun")
    sc = sun.get_component_by_class(unreal.DirectionalLightComponent)
    sc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sc.set_editor_property("intensity", 10.0)
    sc.set_editor_property("atmosphere_sun_light", True)
    spawn(unreal.SkyAtmosphere, label="Sky")
    sky = spawn(unreal.SkyLight, (0, 0, 500), label="SkyLight")
    skc = sky.get_component_by_class(unreal.SkyLightComponent)
    skc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    skc.set_editor_property("real_time_capture", True)

    ppv = spawn(unreal.PostProcessVolume, label="Exposure")
    ppv.set_editor_property("unbound", True)
    s = ppv.get_editor_property("settings")
    s.set_editor_property("override_auto_exposure_min_brightness", True)
    s.set_editor_property("auto_exposure_min_brightness", 3.0)
    s.set_editor_property("override_auto_exposure_max_brightness", True)
    s.set_editor_property("auto_exposure_max_brightness", 3.0)
    ppv.set_editor_property("settings", s)

    floor = spawn(unreal.StaticMeshActor, (6000, 3000, -1), label="Floor")
    fc = floor.static_mesh_component
    fc.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Plane"))
    fc.set_material(0, unreal.load_asset("/Engine/EngineMaterials/DefaultMaterial"))
    floor.set_actor_scale3d(unreal.Vector(160, 100, 1))
    floor.tags = ["AcFloor"]

    row = spawn(unreal.load_class(None, "/Script/Autocraft.AcModelRow"), label="ModelRow")
    log("row actor %s" % row.get_name())

    cam = spawn(unreal.PlayerStart, (-1500, -1500, 800), (0, -20, 45), "Start")

    LES.save_current_level()
    log("saved " + LEVEL)


main()
