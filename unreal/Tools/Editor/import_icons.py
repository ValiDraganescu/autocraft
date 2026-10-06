"""The console's icons (GAME-LAYER.md §2.4 "Icons", chunk D2). Rerunnable.

Input:
  unreal/Content-src/icons/<name>.png
      192 px with alpha, exported from the Swift game by
      `sh unreal/Tools/hud/bake_icons.sh`: the 20 model icons (citadel,
      ranger, ore, ...) and the drawn emblems (up.minigun, act.back, ...).

Output:
  /Game/UI/Icons/T_Icon_<name>  ("." becomes "_": up.minigun → T_Icon_up_minigun)
      UI textures: uncompressed BGRA, sRGB, no mips (SpriteKit draws the
      Swift icons without mips too), never streamed.

`FAcIcons` (Source/Autocraft/AcIcons.cpp) loads them by the Swift `Icons`
name. Run in the editor: py "<repo>/unreal/Tools/Editor/import_icons.py"
(or the MCP editor), or headless with -run=pythonscript (no fonts here).
"""
import glob
import os

import unreal

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC = os.path.join(ROOT, "Content-src", "icons")
DEST = "/Game/UI/Icons"

assets = unreal.EditorAssetLibrary


def asset_name(file):
    return "T_Icon_" + os.path.splitext(os.path.basename(file))[0].replace(".", "_")


def main():
    files = sorted(glob.glob(os.path.join(SRC, "*.png")))
    if not files:
        raise RuntimeError(f"no icons in {SRC}: run unreal/Tools/hud/bake_icons.sh first")
    tasks = []
    for f in files:
        t = unreal.AssetImportTask()
        t.filename = f
        t.destination_path = DEST
        t.destination_name = asset_name(f)
        t.replace_existing = True
        t.automated = True
        t.save = False
        tasks.append(t)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    for f in files:
        path = f"{DEST}/{asset_name(f)}"
        tex = unreal.load_asset(path)
        if tex is None:
            raise RuntimeError(f"icon import failed: {path}")
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        tex.set_editor_property("srgb", True)
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        tex.set_editor_property("never_stream", True)
        tex.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
        tex.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
        tex.set_editor_property("filter", unreal.TextureFilter.TF_BILINEAR)
        # Slate on the Mac writes pow(1/2.2), not sRGB (FAcHudStyle::Srgb): decode
        # the PNG as gamma 2.2 so its pixels land on screen as baked.
        colors = tex.get_editor_property("source_color_settings")
        colors.set_editor_property("encoding_override", unreal.TextureSourceEncoding.TSE_GAMMA22)
        tex.set_editor_property("source_color_settings", colors)
        assets.save_asset(path, only_if_is_dirty=False)
    unreal.log(f"import_icons: {len(files)} icons in {DEST}")


main()
if "-AcQuit" in unreal.SystemLibrary.get_command_line():
    unreal.SystemLibrary.quit_editor()
