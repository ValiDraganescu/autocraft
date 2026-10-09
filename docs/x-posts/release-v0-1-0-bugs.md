---
status: wip
date: 
angle: build
video: video/renders/release-v0-1-0-bugs-x.mp4
posted: 
---

## Post

Claude Code writes all of Ringshadow's code, and I direct. v0.1.0 of the RTS is out, the first download, for Mac, Windows and Linux, and packaging it turned up bugs.

The first build quit right after the home screen: the cook packed 501 packages and missed the unit meshes, which the game loads by path. Now it cooks 3,237. The Mac app shipped empty, then crashed at launch on a sandbox entitlement and a broken signature; it's signed and notarized now. The home screen's zoom stepped a pixel at a time; with pixel snapping off, it glides, as the clip shows.

Windows ran at 160 to 173 fps on an A10G in AWS. Nobody has played Linux yet, so tell me what breaks.

## Reply

The release, a pre-release built from a9e3d2e, with SHA256SUMS: github.com/ValiDraganescu/autocraft/releases/tag/v0.1.0
How the agents build it, a Windows machine in AWS for Windows and Linux, the Mac one here: github.com/ValiDraganescu/autocraft/blob/main/docs/builds.md

## Notes

The release announcement, build version (2026-10-09): the bugs packaging found, each fixed before the release. Picked over the play version (release-v0-1-0) on 2026-10-09. Facts as verified that day (docs/builds.md): 501 packages in the first cook (the game loads most assets by path, so the cook missed the unit meshes and the game quit after the home screen), 3,237 after cooking every folder the game loads; the Mac archive copied the bare app, then the sandboxed signature broke on the renamed Info.plist; the key art's zoom snapped to pixels; Windows 160 to 173 fps on the AWS tester's A10G; Linux not played by anyone yet. 641 characters; the fold falls in the second paragraph, after the news.

Video: the sim `home-to-match` (the video skill's sims.json): the home screen over the night-base key art with the gas giant, drifting (`-AcLauncherArtStart=3`), RESUME GAME pressed by `-AcHomePlay=7` (5 s into the clip), then the RTS view over a staged base and army (`-AcStage=12 -AcStageBuildings -AcHour=10`), the camera easing toward the Rangers. Recorded hidden at a fixed 30 fps; the key art's drift now follows the engine clock (FApp time), so it is smooth in a fixed-step recording too (frame-to-frame change 0.39 to 0.55, steady). No pilot, so no pull-out: the opening card sits on the home screen's first 2.2 s, at the lower right (`--card-right`, the developer's pick on 2026-10-09: at the left it covered the home menu), and the end card comes up over the match. 14.0 s; the X file is 9.77 MB.

```
uv run .claude/skills/ringshadow-video/scripts/record.py home-to-match
uv run .claude/skills/ringshadow-video/scripts/giantcut.py video/clips/home-to-match.mp4 --end 12 --hold 2.2 --card-right \
  --title "v0.1.0 is out." --end-line "v0.1.0 for Mac, Windows and Linux. Public domain." \
  --music ashfall_reach_1 --music-at 18 --out video/renders/release-v0-1-0-bugs.mp4 --x
```
