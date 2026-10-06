"""The HUD's font and art (GAME-LAYER.md §2.4, chunk D1). Rerunnable.

Inputs:
  unreal/Content-src/fonts/BarlowCondensed-{Medium,SemiBold,Bold}.ttf
      Barlow Condensed from Google Fonts (SIL Open Font License 1.1, OFL.txt
      beside them): the shippable stand-in for the Swift HUD's DIN Condensed
      Bold, a macOS system font.
  unreal/Content-src/hud/T_*.png
      Plates and resource icons baked from the Swift game's own drawing code
      by `sh unreal/Tools/hud/bake_hud.sh`.

Outputs:
  /Game/UI/Fonts/FF_BarlowCondensed_{Medium,SemiBold,Bold}  (UFontFace, inline)
  /Game/UI/Hud/T_*  (UI textures: no mips, uncompressed BGRA, sRGB, never streamed)

FAcHudStyle (Source/Autocraft/AcHudStyle.cpp) builds the composite font
from the three faces and the box brushes from the plates at runtime.

Run it in the editor (`py "<repo>/unreal/Tools/Editor/import_hud.py"` in the
console, or the MCP editor), or headless when the editor does not have
these assets open (the font import needs Slate, so not -run=pythonscript):
  UnrealEditor Autocraft.uproject -ExecutePythonScript=<abs path> -AcQuit
      -RenderOffscreen -unattended -nosplash
"""
import glob
import os

import unreal

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FONTS_SRC = os.path.join(ROOT, "Content-src", "fonts")
HUD_SRC = os.path.join(ROOT, "Content-src", "hud")
FONT_DIR = "/Game/UI/Fonts"
HUD_DIR = "/Game/UI/Hud"
FACES = ["Medium", "SemiBold", "Bold"]

assets = unreal.EditorAssetLibrary


def import_tasks(tasks):
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)


def task(filename, dest_dir, dest_name, factory=None):
    t = unreal.AssetImportTask()
    t.filename = filename
    t.destination_path = dest_dir
    t.destination_name = dest_name
    t.replace_existing = True
    t.automated = True
    t.save = False
    if factory is not None:
        t.factory = factory
    return t


def import_fonts():
    tasks = []
    for face in FACES:
        src = os.path.join(FONTS_SRC, f"BarlowCondensed-{face}.ttf")
        if not os.path.exists(src):
            raise RuntimeError(f"missing {src}")
        tasks.append(task(src, FONT_DIR, f"FF_BarlowCondensed_{face}", unreal.FontFileImportFactory()))
    import_tasks(tasks)
    for face in FACES:
        path = f"{FONT_DIR}/FF_BarlowCondensed_{face}"
        ff = unreal.load_asset(path)
        if ff is None:
            raise RuntimeError(f"font import failed: {path}")
        # Inline: the font's bytes live in the asset (no file needed at runtime).
        ff.set_editor_property("loading_policy", unreal.FontLoadingPolicy.INLINE)
        ff.set_editor_property("hinting", unreal.FontHinting.DEFAULT)
        assets.save_asset(path, only_if_is_dirty=False)
        unreal.log(f"import_hud: {path}")


def import_art():
    files = sorted(glob.glob(os.path.join(HUD_SRC, "T_*.png")))
    if not files:
        raise RuntimeError(f"no art in {HUD_SRC}: run unreal/Tools/hud/bake_hud.sh first")
    import_tasks([task(f, HUD_DIR, os.path.splitext(os.path.basename(f))[0]) for f in files])
    for f in files:
        name = os.path.splitext(os.path.basename(f))[0]
        path = f"{HUD_DIR}/{name}"
        tex = unreal.load_asset(path)
        if tex is None:
            raise RuntimeError(f"texture import failed: {path}")
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
        unreal.log(f"import_hud: {path}")


import_fonts()
import_art()
unreal.log("import_hud: done")
# Headless (a whole editor started just for this, as the font import needs
# Slate, which -run=pythonscript lacks):
#   UnrealEditor Autocraft.uproject -ExecutePythonScript=<abs path> -AcQuit
#     -RenderOffscreen -unattended -nosplash
if "-AcQuit" in unreal.SystemLibrary.get_command_line():
    unreal.SystemLibrary.quit_editor()
