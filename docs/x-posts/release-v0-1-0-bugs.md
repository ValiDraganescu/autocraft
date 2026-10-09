---
status: proposed
date: 
angle: build
video: record: the home screen's key art drifting smoothly (the fixed zoom), Play, the RTS view over the base with every unit mesh in place; end card with "v0.1.0 for Mac, Windows and Linux"
posted: 
---

## Post

Claude Code writes all of Ringshadow's code, and I direct. This week the agents packaged it: v0.1.0, the first download, for Mac, Windows and Linux.

The first Windows build quit right after the home screen. The game loads most of its models by path, so the cook packed only 501 packages and left out every unit mesh. The fix cooks every folder the game loads from: 3,237 packages now.

The Mac build had three bugs of its own: the archive step shipped an empty app, a broken signature killed it at launch, and the home screen's slow zoom stepped a pixel at a time. All fixed before release. Nobody has played the Linux build yet, so Linux players, tell me what breaks.

## Reply

The release, with SHA256SUMS: github.com/ValiDraganescu/autocraft/releases/tag/v0.1.0
How the agents build it, a Windows machine in AWS for Windows and Linux, the Mac one here: github.com/ValiDraganescu/autocraft/blob/main/docs/builds.md

## Notes

The release announcement, build version (2026-10-09): the bugs packaging found, each fixed before the release. Facts as verified that day: 501 packages in the first cook (the game loads most assets by path, so the cook missed the unit meshes and the Windows game quit after the home screen), 3,237 after cooking every folder the game loads; the Mac archive copied the bare app, the sandboxed signature broke on the renamed Info.plist, the key art's zoom snapped to pixels.

The sibling draft release-v0-1-0 is the play version of the same news. Pick one.

Video: none recorded yet.
