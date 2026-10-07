---
name: ringshadow-video
description: Make gameplay clips and narrated explainer videos (MP4) of Ringshadow for x.com posts. Records micro simulations of the real game hidden (a staged moment: a piloted unit, a fight, the base at night) at a fixed 30 fps with the game's own sound and optional RTS camera moves, cuts vertical versions for the phone feed, and lays clips out with the Ringshadow video kit (HUD colours, Barlow Condensed), a Kokoro voice, and HyperFrames. Use when the user wants a gameplay clip, a devlog or explainer video, a video of a new unit, model, effect, perf win or tool, or anything to post as video on X.
---

# Ringshadow video

Two products, both MP4:

- **A clip**: one micro simulation, recorded straight from the game with its sound. It's postable on its own: steps 1–2, plus a vertical cut.
- **An explainer**: clips plus kit scenes (cover, numbers, diagrams, before/after), with a voice. Steps 1–7.

A micro simulation is one short, staged moment of the real game: the game stages it from command-line flags, steps at a fixed 1/30 s, saves every frame, and quits. It is never a whole game played by the AIs (AGENTS.md). A clip shows what the game really does; never fake a frame.

| Path | Content | Lifetime |
|---|---|---|
| `.claude/skills/ringshadow-video/sims.json` | The micro simulations: name, flags or scene, seconds | committed |
| `.claude/skills/ringshadow-video/scripts/record.py` | Records sims hidden, with their sound, and encodes them | committed |
| `.claude/skills/ringshadow-video/scripts/vertical.py` | Vertical cuts (4:5, 9:16, 1:1) of any clip or explainer | committed |
| `.claude/skills/ringshadow-video/kit/` | The kit: tokens (the game's HUD palette and font), scene components, layout, motion, the gallery | committed |
| `.claude/skills/ringshadow-video/scripts/` | setup, build, verify, finish, gallery, tts | committed |
| `.claude/skills/ringshadow-video/workspace/` | `package.json`, `pnpm-lock.yaml`, `env.sh`; setup copies them into `video/` | committed |
| `video/` | The workspace: `node_modules/`, `.tools/` (FFmpeg), `skills/` (HyperFrames docs), `.frames/` (frames while recording) | ignored by git |
| `video/clips/` | Clips that `record.py` makes by default | ignored by git |
| `video/projects/<video>/` | One explainer: `scenes.json`, `sources/`, the build output | deleted by `finish.sh` |
| `video/renders/` | Finished explainers and their contact sheets (`VIDEO_OUTPUT_DIR` sets another folder) | ignored by git |

## Recording rules

- Every run is hidden and silent to the room: `UnrealEditorBG.app`, `-RenderOffscreen`, no `-log` (`unreal/Tools/README.md`, "Hidden test runs"). The sound is rendered offline into a WAV (`-deterministicaudio -AcAudioRecord`: the non-realtime mixer, nothing reaches the speakers); a sim without sound runs with `-nosound`. `record.py` does this; never launch the game another way, never play sound out loud, and never relaunch the user's game or editor.
- The game module must be built with `-AcShotRecord` (`unreal/Source/Autocraft/AcShot.h`). After a C++ change, build first (AGENTS.md); the hidden run loads the new module, the user's open editor does not.
- The frames never touch the disk: the game reads each one back from the GPU without stalling the render and streams it as raw BGRA into a pipe (`-AcShotRaw`, AcShot.h), and FFmpeg encodes it while the game runs. A 10 s clip at 1080p takes about 25 s: ~9 s of start-up, the recording at about 40 fps, then the sound muxed in. `record.py --png` saves PNG frames instead (the old way, three to four times slower); use it only if the raw path breaks.
- Run sims one at a time; the machine is shared.

## The kit

Every explainer uses the kit, so all videos look the same: the game's HUD palette (glass blue, cyan accent, ice text; `AcHudStyle.h`), the HUD font Barlow Condensed (`unreal/Content-src/fonts`), a footer with "Ringshadow", the video title and the scene count, a progress bar, and one entrance motion.

A scene is data in `scenes.json`, never HTML. Each `visual.type` is one component of `kit/components/`:

| `type` | Shows | Fields |
|---|---|---|
| `cover` | The opening: the hook in one sentence | `kicker`, `title` (max 48), `lead` (max 180) |
| `stats` | 1 to 4 large numbers | `kicker`, `title`, `items: [{ value, label }]`; a value is 1 to 7 characters with no space, and a whole number above 9 counts up |
| `flow` | A diagram from left to right, 1 to 3 rows of 1 to 5 nodes | `kicker`, `title`, `rows: [["Node", { "label": "New node", "focus": true }]]`; the focus node is cyan |
| `compare` | Before and after | `kicker`, `title`, `before: { label?, items }`, `after: { label?, items }`, 1 to 4 items each |
| `rules` | A table, 1 to 6 rows | `kicker`, `title` (optional), `rows: [{ when, result, tone }]`, `tone` is `ok`, `warn`, `fail` or `info` |
| `bullets` | 1 to 5 short points | `kicker`, `title`, `items` |
| `checks` | The close: 1 to 5 points with a tick | `kicker`, `title`, `items` |
| `clip` | A recorded sim, with its game sound | `kicker` (max 32), `title` (max 44, one line), `src`, `mediaStart`, `mediaEnd`, `rate`, `sound`, `soundVolume`, `fit`, `focus` |
| `image` | A still (a render, a turnaround sheet, a frame) | `kicker` (max 32), `title` (max 44, one line), `src` (`.png`, `.jpg`, `.webp`, `.svg`), `fit`, `focus` |

`kicker` is optional (max 40 unless the table says other), `title` is max 70 unless the table says other, and a list item is max 90 (max 70 in `compare`). In `title`, `lead` and the items, `*text*` shows the text in the cyan accent. The build checks every limit and names each field that breaks one.

When a scene needs a layout that no component has, change the kit; never write HTML for one video:

1. Add `kit/components/<name>.mjs` with `validate` and `html` (see `stats.mjs`), its classes in `kit/kit.css`, and the component in `kit/components/index.mjs`. Colours and sizes come from `kit/tokens.css`. Entrance motion comes from `data-in` (`up`, `left`, `pop`) and `data-step` (the order); `kit/scene.mjs` makes the timeline.
2. Add a scene of the new component to `kit/gallery/scenes.json`.
3. Run `bash .claude/skills/ringshadow-video/scripts/gallery.sh` (and `gallery.sh portrait`, `gallery.sh story` for the vertical canvases). It must pass `hyperframes check` with 0 errors and 0 warnings. Read `video/projects/kit-gallery/snapshots/contact-sheet.jpg`.
4. Show the gallery snapshot to the user. The kit changes only when the user accepts the look.
5. Delete `video/projects/kit-gallery/` (`rm -r video/projects/kit-gallery` from the repo root).

## Steps

### 1. Prepare the workspace

```bash
bash .claude/skills/ringshadow-video/scripts/setup.sh
```

It copies `workspace/` into `video/`, installs the pinned `hyperframes` and `gsap` from the lockfile, builds FFmpeg from nixpkgs into `video/.tools/`, and downloads the HyperFrames docs of the same version into `video/skills/`. It stops if git does not ignore `video/`. Safe to run again. The voice needs mlx-audio at `~/git/dev/mlx-audio/.venv` (`VIDEO_TTS_PYTHON` for another place).

Done when it prints `workspace ready` and the doctor lines show FFmpeg, FFprobe and Chrome.

### 2. Record the micro simulations

```bash
uv run .claude/skills/ringshadow-video/scripts/record.py --list
uv run .claude/skills/ringshadow-video/scripts/record.py fight-12 ranger-duel --out video/projects/<video>/sources
```

Each sim gives `NAME.mp4` (H.264, 1920×1080, 30 fps, AAC game sound), `NAME-contact.png` (four frames) and `NAME.log`. The run prints the sound's offset to the frames (about one frame), its peak and its loudness; a peak of 0 or "no sound was written" means a silent clip. Read every contact sheet before you use a clip.

A new moment is a new row in `sims.json`:

- `scene`: reuse a staged scene of `unreal/Tools/shots/scenes.json` (`pilot-*`, `third-*`, `top-*`, `orders`, `level-*`…).
- `flags`: the game's own staging flags. The useful ones, with their docs:
  - `-AcPilot=KIND -AcPilotPath=w:1.5,w+right:1.5,act:2` drives a unit in first person (`AcPilotPawn.h`: keys `w a s d act right left up down ability next view leave wait aim`; `aim+act:2` keeps the view on the nearest enemy's chest while firing); add `-AcPilotThird` for third person.
  - `-AcPilotDiveAt=1` takes over 1 s into the recording: the clip opens on the RTS view and the camera dives into the unit (1.2 s; `-AcPilotDive=S` sets the length, `ac.PilotDive` in the game). Start its path with a short `wait:` so the unit stands still while the camera lands. `ranger-dive` is the example.
  - `-AcPilotAt=slope|enemy|X,Y` puts a fresh unit down instead of taking the first one: `slope` on the top of the player's own ramp facing down it (the way out toward the enemy), `enemy` 20–26 cells from the nearest enemy Citadel facing it (a Longbow drives 2.4 s to be in anchored range), `X,Y` at a ground point facing `-AcPilotYaw`. `-AcPilotVariant=anchored` puts a Longbow down anchored. With `-AcPilotDiveAt` the unit is on the map before the dive. The log prints where it went (`pilot: -AcPilotAt=`); a fixed point is only valid on that map.
  - `-AcPilotPullOut=S,L` ends a clip: S seconds after the take-over the camera leaves the unit and rises behind it over L seconds, turned to the gas giant (best at night, `-AcHour=22`); the eject: the UI fades out over 0.5 s while the camera shoots out of the unit (eased out), the cockpit goes once it is out of the body, the view stays on the unit and then turns up to the gas giant; the drive goes on below. `longbow-finale` is the example.
  - `-AcPilotVariant=minigun+shield+flyer` (any of them, `+`-joined): the player's Mini gun and Aegis Shield upgrades; an enemy Kestrel in the air past the staged enemy (`aim` holds a flyer at its hover). `-AcPilotFoeHp=N` lets the driven unit win (the Kestrel gets 2N). `ranger-third-to-first` is the example.
  - `-AcPilotStage` (with `-AcPilotVariant=fire|jump|anchoring|heal|…`) stages the classic pilot shot and then **pauses**: a still, not motion.
  - `-AcStageFight=N -AcCamAtArmy` stages two armies of N face to face (`AcWorldRenderer.h`); `-AcNoFog` shows the whole fight.
  - `-AcOrders`, `-AcQueue` (`AcCommandMap.h`), `-AcHour=19` (dusk), `-AcMap=badlands-large`, `-AcZoom=`, `-AcCamAt=`, `-AcSelect=`, `-AcHover=`.
- `camera`: an RTS camera move (`-AcCamPath`, `AcRtsPawn.h`): `"T:DX,DZ,PPC;T:DX,DZ,PPC"`, keys at clip seconds, DX,DZ cells from the start view (the one `-AcCamAt`/`-AcCamAtArmy` sets), PPC the zoom in points per cell (about 10 far, 40 normal, 130 closest; 0 keeps the start zoom). Eased between keys. `"0:-10,6,18;6:0,0,75"` sweeps in from afar and dives into the fight; `"0:-6,0,45;6:6,-4,45"` pans across. The first key holds through the warm-up, so the clip opens on it.
- `sound` (default true) and `music` (default true; false adds `-AcNoMusic`, for a clip that goes under narration).
- `seconds` (default 4), `warm` (frames before recording, default 60), `width`/`height`/`fps`.

When no flag stages what the video needs, add the staging to the game: a new `-Ac…` flag in the chunk that owns it, documented in its header, like the ones above. Build, then add the sim. Keep it a staged moment, never a played-out game.

### The post cut (the gas giant shape)

Every X post video has the same shape (the posts skill's WORKFLOW.md, step 4): the opening card over the gas giant, the clip's pull-out reversed down into the scene, the scene, the eject back up to the gas giant under the end card. `scripts/giantcut.py` cuts it from one sim that ends with `-AcPilotPullOut`:

```bash
uv run .claude/skills/ringshadow-video/scripts/giantcut.py video/clips/ranger-third-to-first.mp4 \
  --down 5.85,10.0 --end 12.9 --title "Press V to switch views." --music firefight_1 --music-at 18 \
  --out video/renders/third-person.mp4 --x
```

The cards are drawn with Pillow: RINGSHADOW in Exo 2 Black, the title in dark Barlow Condensed ExtraBold on a cyan bar, a dark gradient under them at the lower left, everything above the bottom 230 px (X's player bar). The fonts are in `unreal/Content-src/fonts` (OFL). `--down` starts on the first frame without the cockpit and ends where the camera settles. The X file's rate is set for 9.4 MB at any length.

- `--caption "…" --caption-at S,E`: one line on a cyan bar over the scene, above the pilot's console (the music deck stays visible). X autoplays muted: say what the sound is.
- `--music-ends`: the music only under the opening and the end card, when the scene's own sound is the point (the radio).
- `--start S`: the scene starts S seconds into the clip (an `-AcPilotAt` take-over shows the top-down view for its first frames).
- `--zoom S,E,X,Y,F`: eases into F× on pixel X,Y between scene seconds S and E and back out (the level-up cards: `0.8,3.6,952,715,2`).

Staging that goes with it: `-AcPilotAt=ridge` puts the unit out from a base, in line with the gas giant behind it, facing the Citadel (`-AcPilotMiners=N` adds busy Prospectors). The generated maps put every base on its top level, so there is no ground above one: the ridge falls back to the base's own level, 20 cells out, clear of ore fields and wells. `-AcMusicAd=ID` opens the station on that ad (`quicksilver_cola`, `opal_glow`, `citadel_timeshares`…; `unreal/Resources/Sounds/ads/titles.json`); `-AcLevel=prospector:2 -AcPicks=1 -AcEarn=100@2 -AcPickKey=z@3` levels a driven Prospector up to 3 and keeps the first card after 3 s (`AcLeveling.h`).

For a post that is only a clip, cut the vertical version (below), then give the user both MP4 paths, their length and size, and the contact sheet.

### Vertical cuts

```bash
uv run .claude/skills/ringshadow-video/scripts/vertical.py video/clips/fight-12-dive.mp4 --kicker "Micro simulation" --title "Two armies of twelve|meet in the desert"
uv run .claude/skills/ringshadow-video/scripts/vertical.py video/clips/ranger-duel.mp4 --aspect 9:16 --mode crop --focus 0.5
```

- `--aspect`: `4:5` (1080×1350, the X feed; default), `9:16` (1080×1920), `1:1`.
- `--mode fit` (default): the whole 16:9 picture across the width over a blurred fill of the scene, with an optional cyan `--kicker` and an ice `--title` above it in the HUD font (`|` breaks the line). `--zoom 1.3` makes the picture larger and crops its sides. Use it when the HUD or the whole frame matters, and for explainers (their slides stay whole).
- `--mode crop`: a full-height slice; `--focus` 0..1 picks where. Use it for first-person clips whose action is in the middle.
- Writes `NAME-4x5-fit.mp4` (and so on) next to the input, or in `--out`; keeps the sound. Look at a frame of each cut before you hand it over: a crop can cut off what the clip is about.

Done when every clip the video needs is in `sources/` and its contact sheet shows the right moment.

### 3. Write the script

Write `video/projects/<video>/scenes.json` (`<video>` is kebab-case, e.g. `pilot-any-unit`). One scene is one idea, with one component and 1 to 3 spoken sentences.

```json
{
  "title": "Take over any unit",
  "voice": "am_michael",
  "speed": 1.0,
  "format": "portrait",
  "music": { "track": "long_haul_1", "start": 20 },
  "scenes": [
    { "id": "intro", "narration": "…", "visual": { "type": "cover", "kicker": "Devlog", "title": "Take over *any unit* mid-battle", "lead": "…" } },
    { "id": "fight", "narration": "…", "visual": { "type": "clip", "src": "sources/fight-12.mp4", "kicker": "RTS view", "title": "Two armies of twelve meet" } },
    { "id": "ranger", "narration": "…", "visual": { "type": "clip", "src": "sources/ranger-duel.mp4", "kicker": "First person", "title": "Now you are the Ranger" } }
  ]
}
```

| Field | Meaning |
|---|---|
| `title` | The video title, shown in each scene's footer |
| `id` | Kebab-case and unique; names the voice file and the scene composition |
| `narration` | The spoken text. The scene lasts as long as the voice, plus 0.4 s before and 0.7 s after |
| `minDuration` | Optional shortest scene length in seconds |
| `visual` | One kit component with its fields |
| `visual.mediaStart`, `visual.mediaEnd` | For `clip`: the part of the source to use, in seconds |
| `visual.rate` | For `clip`: the playback rate. Without it the build fits the clip to the scene between 0.5× and 3×, and holds the last frame if it is still too short |
| `voice`, `speed` | Kokoro voice and speed; `am_*`/`bm_*` male, `af_*`/`bf_*` female |
| `format` | `landscape` (1920×1080, default), `portrait` (1080×1350, 4:5, the X feed), `story` (1080×1920, 9:16). The vertical formats are laid out natively: the panels stack in one column, a flow runs top to bottom (at most 4 nodes a row), and clips fill the width, cropped (`fit: "cover"`) |
| `visual.fit`, `visual.focus` | For `clip` and `image`: `contain` (the whole frame) or `cover` (fills the box, cropped; the vertical default); `focus` 0..1 keeps the left, middle or right of a cropped frame |
| `visual.sound`, `visual.soundVolume` | For `clip`: the clip's game sound plays (default, when the clip has sound) at full volume around the voice and at `soundVolume` (0.35) under it; `false` mutes it. A clip with sound plays at 1× unless `rate` says other (a faster clip would shift the sound's pitch); a shorter clip holds its last frame, a longer one is cut at the scene's end |
| `music` | Optional music bed: a track of `unreal/Resources/Sounds/music/` by name (`"long_haul_1"`), or `{ track, start, volume }` (`start` seconds into the track, `volume` 0.25 by default). It fades in and out, and ducks to 40% under each voice line. The track must be longer than the video from `start` |

Write for X:

- X autoplays muted: the titles must carry the story without the voice. The voice adds; it never carries alone.
- The hook comes first: open on the most striking clip or a cover whose title is the verb ("Take over any unit"). No logo intro.
- Keep it short: 20–60 s for a post; never over 2:20 (X's limit for most accounts).
- Make it `portrait` for the feed: it fills more of a phone than 16:9. Use `landscape` for YouTube or a page.
- Use the game's names (docs/naming.md), capitalised. Don't mention other games.
- Narration is for the ear: short sentences; numbers as words when the reading is unclear; never read code, paths or ids.

Done when each scene has an id, a narration and a visual, and each `src` exists.

### 4. Build, check and look

```bash
cd video && source ./env.sh
node ../.claude/skills/ringshadow-video/scripts/build.mjs <video>
cd projects/<video>
hyperframes check
hyperframes snapshot --at "$(node -e "console.log(JSON.parse(require('fs').readFileSync('.build/timeline.json','utf8')).scenes.map(s=>(s.start+s.duration*0.6).toFixed(2)).join(','))")"
```

The build checks `scenes.json` against the kit, voices each changed narration (cache `.build/tts.json`), fits the clips, and writes `index.html` and `compositions/`. `check` must report 0 errors, 0 warnings, 0 layout issues, and every contrast check passed. Read each `snapshots/contact-sheet-*.jpg`: the visual shows what the narration says, no clip shows a loading frame, no text is cut off.

When a lint finding is unclear, read `video/skills/hyperframes-core/SKILL.md` or `video/skills/hyperframes-cli/SKILL.md`; don't follow their workflows. Fix `scenes.json` and build again; never edit `index.html`, `compositions/` or `assets/`.

### 5. Render

```bash
hyperframes render -o renders/<video>.mp4
bash ../../../.claude/skills/ringshadow-video/scripts/verify.sh . renders/<video>.mp4
```

`verify.sh` checks the H.264 and AAC tracks, the length against the timeline (within 0.5 s), that the voice is audible, and writes `renders/<video>-contact.png`. Read it.

### 6. Finish

```bash
bash .claude/skills/ringshadow-video/scripts/finish.sh <video>
```

From the repo root. It verifies again, copies the MP4 and contact sheet to `video/renders/`, and deletes the project. On a failed check it keeps the project.

### 7. Deliver

Give the user the absolute path of the MP4, its length and size, the contact sheet, and the scene list (id, start, length). A change after delivery is a new job from step 2.

## Known traps

| Symptom | Cause | Fix |
|---|---|---|
| `record.py`: fewer frames than expected | The run quit early (a staging error) | Read `NAME.log`; grep `shot:` and `Error` |
| The clip is frozen | `-AcPilotStage` pauses the game after staging | Use `-AcPilot` with `-AcPilotPath` for motion |
| The first frames show a unit right against the camera | A fresh unit spawns beside the piloted one at the Citadel | Start the path with `wait:` or a turn, or trim with `mediaStart` |
| A first-person clip walks into the enemy | `-AcPilot` puts an enemy Ranger ahead of the piloted unit | Fire at it first (`act:2,...`), or turn before walking |
| The clip is silent | The sound did not start within 300 frames, or the sim has `"sound": false` | Read `NAME.log` for `audio:` lines; a paused sim (`-AcPilotStage`) never starts its sound |
| The camera does not move | The view clamps to the map, or `-AcCamPath` did not parse | Grep the log for `rts camera: -AcCamPath`; keep DX,DZ inside the map |
| The clip plays too fast or too slow | Recorded without `-UseFixedTimeStep -FPS=30` | `record.py` passes them; a hand-made run must too |
| The perf panel shows | `-AcNoPerfPanel` missing | It's in the `sims.json` defaults; keep it |
| The build names a field that is too long | The text doesn't fit the component | Shorten it; never change a limit for one video |
| `rm -rf /abs/path` is refused | The repo's shell hook blocks it | `rm -r` with the exact relative path from the repo root |
| A new flag's value with a comma arrives cut short (`X,Y` read as `X`) | `FParse::Value` stops at commas | Parse it with `FParse::Value(Cmd, TEXT("AcFoo="), Out, false)` |
| An `-AcPilotAt` clip shows the top-down view only, "nothing to drive" in the log | The place did not parse, or is not on this map | Read the log's `pilot: -AcPilotAt=` line |
| `record.py` hangs or reports 0 frames, "cannot open" in the log | The game failed before it opened the pipe, or FFmpeg stopped reading it | Read `NAME.log` (`shot: -AcShotRaw`, `shot: writing raw frame`); try once with `--png` |
| Render warns about the V8 heap | Too many capture workers | `hyperframes render --workers 5 -o …` |

## Next steps for this skill

- A recorded eject back to the RTS view: leaving a unit now blends the view from the eye up to the top-down view (`ac.PilotEject`, 0.9 s), but the pilot HUD still swaps for the RTS HUD at once.
- Camera moves in first person (an orbit around the piloted unit in third person).
