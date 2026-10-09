# /// script
# requires-python = ">=3.11"
# dependencies = ["pillow"]
# ///
"""Cuts a recorded sim into the post video's gas giant shape.

1. The opening card over the clip's last frame (the gas giant over the
   scene): RINGSHADOW, the title on a cyan bar, "A real-time strategy game
   in Unreal Engine 5", held, on screen from frame 0.
2. The clip's pull-out played in reverse, faster: the gas giant comes down
   into the scene, and the card fades.
3. A cross-fade into the scene, with the game's sound.
4. The pull-out back to the gas giant, its last frame held under the end
   card.

A clip without a pull-out (the home screen, the RTS view) leaves out --down:
the opening card sits on the scene's first seconds and fades, and the end
card comes up over the scene's last frame.

Music under all of it, loudness -16 LUFS. --x also writes the X upload
(2-pass x264, its rate set for 9.4 MB at any length) next to the output.

    uv run .claude/skills/ringshadow-video/scripts/giantcut.py video/clips/ranger-dive-giant.mp4 \\
        --down 5.7,11.95 --end 11.95 --title "Take over any unit." --music ashfall_reach_1 --music-at 18 \\
        --out video/renders/dive-giant.mp4 --x
"""
import argparse
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageOps

REPO = Path(__file__).resolve().parents[4]
FF = str(REPO / "video/.tools/ffmpeg-bin/bin/ffmpeg")
FONTS = REPO / "unreal/Content-src/fonts"
W, H = 1920, 1080
ICE, CYAN, INK = (235, 247, 255, 255), (89, 217, 255, 240), (6, 18, 30, 255)
X0 = 90           # the left edge of the text
BASE = H - 230    # the text's lowest line ends here: clear of X's player bar


def font(name: str, size: int, variation: bytes | None = None) -> ImageFont.FreeTypeFont:
    f = ImageFont.truetype(str(FONTS / name), size)
    if variation:
        f.set_variation_by_name(variation)
    return f


def scrim() -> Image.Image:
    """Dark at the lower left, clear over the gas giant."""
    g = Image.new("L", (W, H), 0)
    px = g.load()
    for y in range(int(H * 0.4), H):
        v = (y - H * 0.4) / (H * 0.6)
        for x in range(0, int(W * 0.7)):
            px[x, y] = int(255 * 0.78 * v * (1 - x / (W * 0.7)) ** 0.8)
    out = Image.new("RGBA", (W, H), (4, 8, 16, 0))
    out.putalpha(g)
    return out


def shadowed(img: Image.Image, xy: tuple[float, float], s: str, f: ImageFont.FreeTypeFont, fill) -> None:
    sh = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(sh).text((xy[0] + 4, xy[1] + 5), s, font=f, fill=(0, 0, 0, 190))
    img.alpha_composite(sh.filter(ImageFilter.GaussianBlur(6)))
    ImageDraw.Draw(img).text(xy, s, font=f, fill=fill)


def bar(img: Image.Image, y: float, s: str, f: ImageFont.FreeTypeFont) -> None:
    """Dark text on a solid cyan bar: reads on any ground."""
    d = ImageDraw.Draw(img)
    l, t, r, b = d.textbbox((0, 0), s, font=f)
    pad = 24
    d.rectangle([X0, y, X0 + (r - l) + 2 * pad, y + (b - t) + 2 * 18], fill=CYAN)
    d.text((X0 + pad - l, y + 18 - t), s, font=f, fill=INK)


def opening(title: str, right: bool = False) -> Image.Image:
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    # At the right the name is smaller, so it clears a panel at the left (the home menu ends ~600 px in).
    size = 150 if right else 190
    shadowed(img, (X0 - 6, BASE - 140 - size), "RINGSHADOW", font("Exo2-Variable.ttf", size, b"Black"), ICE)
    bar(img, BASE - 82, title, font("BarlowCondensed-ExtraBold.ttf", 84))
    shadowed(img, (X0 + 2, BASE + 40), "A REAL-TIME STRATEGY GAME IN UNREAL ENGINE 5",
             font("BarlowCondensed-SemiBold.ttf", 40), ICE)
    if right:
        # The block's right edge X0 from the frame's: the left stays clear (the home menu).
        moved = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        moved.paste(img, ((W - X0) - img.getbbox()[2], 0))
        return Image.alpha_composite(ImageOps.mirror(scrim()), moved)
    return Image.alpha_composite(scrim(), img)


def ending(line: str) -> Image.Image:
    img = scrim()
    shadowed(img, (X0 - 6, BASE - 380), "RINGSHADOW", font("Exo2-Variable.ttf", 170, b"Black"), ICE)
    bar(img, BASE - 150, "A real-time strategy game in Unreal Engine 5.", font("BarlowCondensed-ExtraBold.ttf", 64))
    shadowed(img, (X0 + 2, BASE - 30), line,
             font("BarlowCondensed-SemiBold.ttf", 52), ICE)
    shadowed(img, (X0 + 2, BASE + 36), "github.com/ValiDraganescu/ringshadow",
             font("BarlowCondensed-SemiBold.ttf", 46), (89, 217, 255, 255))
    return img


def caption(s: str) -> Image.Image:
    """One line on a cyan bar at the lower left, over the scene."""
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    if s:
        # Above the pilot's console (its top is about 790 px down), so the music deck shows.
        bar(img, H - 390, s, font("BarlowCondensed-ExtraBold.ttf", 58))
    return img


def main() -> None:
    a = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    a.add_argument("src", type=Path, help="the recorded sim (with -AcPilotPullOut)")
    a.add_argument("--down", help="S,E: the pull-out, from leaving the eye to the gas giant (clip s); "
                   "leave it out for a clip without one: the card sits on the scene's start")
    a.add_argument("--end", type=float, required=True, help="where the scene ends (clip s), on the gas giant")
    a.add_argument("--start", type=float, default=0, help="where the scene starts (clip s): skips a take-over's first frames")
    a.add_argument("--title", required=True, help="the opening card's title, about 25 characters at most")
    a.add_argument("--music", default="ashfall_reach_1", help="a track of unreal/Resources/Sounds/music")
    a.add_argument("--music-at", type=float, default=18, help="seconds into the track")
    a.add_argument("--music-ends", action="store_true",
                   help="the music only under the opening and the end card (the scene's own sound is the point: the radio)")
    a.add_argument("--caption", help="a line over the scene, lower left, e.g. what the radio plays (X autoplays muted)")
    a.add_argument("--caption-at", default="0.5,8", help="S,E: when the caption shows, scene seconds")
    a.add_argument("--zoom", help="S,E,X,Y,F: ease into F× on the pixel X,Y between scene seconds S and E (small UI, e.g. cards)")
    a.add_argument("--hold", type=float, default=1.3, help="the opening frame held under the card")
    a.add_argument("--speed", type=float, default=2.0, help="the descent plays this much faster")
    a.add_argument("--tail", type=float, default=2.0, help="the last frame held under the end card")
    a.add_argument("--card-right", action="store_true", help="the opening card at the lower right (the left holds the home menu)")
    a.add_argument("--end-line", default="Drive any unit. Open source, public domain.", help="the end card's line under the bar")
    a.add_argument("--out", type=Path, required=True)
    a.add_argument("--x", action="store_true", help="also write OUT-x.mp4 for the X upload")
    o = a.parse_args()
    xf = 0.35
    if o.down:
        d0, d1 = (float(v) for v in o.down.split(","))
        a_len = o.hold + (d1 - d0) / o.speed
    else:
        # No descent: B alone, the opening card over its first seconds.
        xf = 0.0
        a_len = 0.0
    b_len = o.end - o.start + o.tail
    total = a_len + b_len - xf
    card_at = o.end - o.start - 2.2
    tmp = Path(tempfile.mkdtemp(prefix="giantcut-"))
    opening(o.title, o.card_right).save(tmp / "open.png")
    ending(o.end_line).save(tmp / "end.png")
    caption(o.caption or "").save(tmp / "caption.png")
    c0, c1 = (float(v) for v in o.caption_at.split(","))
    music = REPO / f"unreal/Resources/Sounds/music/{o.music}.mp3"
    if o.music_ends:
        # Full under the opening, out as the scene starts, back for the end card.
        s0, s1 = a_len - xf, a_len - xf + card_at
        bed = (f"volume='0.3*if(lt(t,{s0}),1,if(lt(t,{s0 + 0.8}),1-(t-{s0})/0.8,"
               f"if(lt(t,{s1}),0,if(lt(t,{s1 + 0.8}),(t-{s1})/0.8,1))))':eval=frame")
    else:
        bed = "volume=0.3"
    zoom = ""
    if o.zoom:
        z0, z1, zx, zy, zf = (float(v) for v in o.zoom.split(","))
        # 0 → 1 over 0.6 s from z0, back to 0 over the 0.6 s before z1 (smoothstep).
        e = f"clip(min((it-{z0})/0.6,({z1}-it)/0.6),0,1)"
        z = f"(1+{zf - 1}*({e})*({e})*(3-2*({e})))"
        zoom = (f"zoompan=z='{z}':x='clip({zx}-iw/zoom/2,0,iw-iw/zoom)':"
                f"y='clip({zy}-ih/zoom/2,0,ih-ih/zoom)':d=1:s={W}x{H}:fps=30,")
    if o.down:
        part_a = (
            # A: the descent, reversed and faster, its first frame held; the card fades as it lands.
            f"[0:v]trim={d0}:{d1},setpts=PTS-STARTPTS,reverse,setpts=PTS/{o.speed},"
            f"tpad=start_duration={o.hold}:start_mode=clone,fps=30,setsar=1[a0];"
            f"[2:v]format=rgba,fade=t=out:st={a_len - 0.65}:d=0.45:alpha=1[oc];"
            f"[a0][oc]overlay=0:0:shortest=1[a];")
        opened, join = "[b1]", f"[a][b]xfade=transition=fade:duration={xf}:offset={a_len - xf},format=yuv420p[v];"
    else:
        part_a = f"[2:v]format=rgba,fade=t=out:st={o.hold}:d=0.45:alpha=1[oc];"
        opened, join = "[bo]", "[b]format=yuv420p[v];"
    graph = (
        part_a +
        # B: the scene to the gas giant, its last frame held; the end card fades in.
        f"[0:v]trim={o.start}:{o.end},setpts=PTS-STARTPTS,{zoom}tpad=stop_duration={o.tail}:stop_mode=clone,fps=30,setsar=1[b0];"
        f"[3:v]format=rgba,fade=t=in:st={card_at}:d=0.5:alpha=1[ec];"
        f"[4:v]format=rgba,fade=t=in:st={c0}:d=0.4:alpha=1,fade=t=out:st={c1 - 0.4}:d=0.4:alpha=1[cc];"
        f"[b0][cc]overlay=0:0:shortest=1[b1];"
        + ("" if o.down else "[b1][oc]overlay=0:0:shortest=1[bo];") +
        f"{opened}[ec]overlay=0:0:shortest=1,fade=t=out:st={b_len - 0.5}:d=0.5[b];"
        + join +
        # The game's sound under the scene, the music under everything.
        f"[0:a]atrim={o.start}:{o.end},asetpts=PTS-STARTPTS,volume=2.0,afade=t=out:st={o.end - o.start - 0.6}:d=0.6,"
        f"adelay={int((a_len - xf) * 1000)}:all=1,apad=whole_dur={total}[g];"
        f"[1:a]atrim={o.music_at}:{o.music_at + total},asetpts=PTS-STARTPTS,{bed},afade=t=in:d=0.5,"
        f"afade=t=out:st={total - 2.5}:d=2.5[m];"
        f"[g][m]amix=inputs=2:duration=longest:normalize=0,loudnorm=I=-16:TP=-1.5:LRA=11,aresample=48000[au]")
    o.out.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([FF, "-y", "-loglevel", "error", "-i", str(o.src), "-i", str(music),
                    "-loop", "1", "-framerate", "30", "-i", str(tmp / "open.png"),
                    "-loop", "1", "-framerate", "30", "-i", str(tmp / "end.png"),
                    "-loop", "1", "-framerate", "30", "-i", str(tmp / "caption.png"),
                    "-filter_complex", graph, "-map", "[v]", "-map", "[au]", "-t", f"{total:.3f}",
                    "-c:v", "libx264", "-crf", "18", "-preset", "slow", "-c:a", "aac", "-b:a", "192k",
                    "-movflags", "+faststart", str(o.out)], check=True)
    print(f"cut: {o.out} ({total:.1f} s, {o.out.stat().st_size / 1e6:.1f} MB)")
    if o.x:
        x = o.out.with_name(o.out.stem + "-x.mp4")
        log = str(tmp / "pass")
        # 9.4 MB in all, the sound's 128k off the top: under X's 10 MB browser upload.
        rate = int(9.4e6 * 8 / total / 1000) - 128
        common = ["-c:v", "libx264", "-b:v", f"{rate}k", "-preset", "slow", "-passlogfile", log]
        subprocess.run([FF, "-y", "-loglevel", "error", "-i", str(o.out), *common, "-pass", "1", "-an", "-f", "mp4", "/dev/null"], check=True)
        subprocess.run([FF, "-y", "-loglevel", "error", "-i", str(o.out), *common, "-pass", "2",
                        "-c:a", "aac", "-b:a", "128k", "-movflags", "+faststart", str(x)], check=True)
        print(f"x: {x} ({x.stat().st_size / 1e6:.2f} MB)")


if __name__ == "__main__":
    main()
