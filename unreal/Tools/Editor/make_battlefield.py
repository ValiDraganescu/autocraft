"""/Game/Maps/Battlefield: the game's default map (chunk A1, GAME-LAYER.md §3.7).

An empty level (no World Partition, nothing placed): the terrain, the
lights and every unit are spawned from C++ at runtime. Its World Settings
name AAcGameMode, which is also the global default in Config/DefaultEngine.ini.

Rerunnable: makes the level only when it is missing, and sets the game mode
override either way.

Run it headless while the live editor does not have the level open:
  UnrealEditor-Cmd Autocraft.uproject -run=pythonscript -script=<abs path>
(the Autocraft module must be built first, so AcGameMode exists), or in the
editor (`py <path>`) when the level is not the one open.
"""
import unreal

PATH = "/Game/Maps/Battlefield"
GAME_MODE = "/Script/Autocraft.AcGameMode"

eal = unreal.EditorAssetLibrary

if eal.does_asset_exist(PATH):
    world = eal.load_asset(PATH)
    unreal.log("make_battlefield: %s exists" % PATH)
else:
    factory = unreal.WorldFactory()
    world = unreal.AssetToolsHelpers.get_asset_tools().create_asset("Battlefield", "/Game/Maps", unreal.World, factory)
    unreal.log("make_battlefield: made %s" % PATH)

mode = unreal.load_class(None, GAME_MODE)
if mode is None:
    unreal.log_error("make_battlefield: %s is not loaded (build the Autocraft module)" % GAME_MODE)
else:
    settings = world.get_world_settings()
    settings.set_editor_property("default_game_mode", mode)
eal.save_asset(PATH, only_if_is_dirty=False)
unreal.log("make_battlefield: saved %s" % PATH)
