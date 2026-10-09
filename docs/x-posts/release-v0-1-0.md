---
status: proposed
date: 
angle: play
video: record: the home screen's key art drifting, Play, the match opens on the RTS view and dives into a unit; end card with "v0.1.0 for Mac, Windows and Linux"
posted: 
---

## Post

Ringshadow v0.1.0 is out, the first build you can download and play. It's an RTS where an AI commander runs your army, and you can take over any of your units and play it like a shooter.

Three zips on GitHub: macOS on Apple silicon (signed and notarized by Apple, so it opens with no warning), Windows 64-bit and Linux x64. A Windows machine in AWS builds the Windows and Linux ones on demand in about 18 minutes, then gets terminated.

I played the Windows build on a cloud GPU at 160 to 173 fps. Nobody has played the Linux build yet. If you run Linux, you'd be the first, and I'd like to hear how it went.

## Reply

The release, with SHA256SUMS: github.com/ValiDraganescu/autocraft/releases/tag/v0.1.0
The Windows zip doesn't carry the Visual C++ runtime yet; the release notes link Microsoft's installer.

## Notes

The release announcement, play version (2026-10-09). Facts as verified that day: a GitHub pre-release built from a9e3d2e; macOS 535 MB (Developer ID, notarized), Windows 416 MB, Linux 419 MB (cross-compiled on the Windows machine); the AWS machine is in Stockholm, 18 to 19 minutes a build, terminated afterwards; Windows played on an NVIDIA A10G at 2560x1378, 160 to 173 fps; Linux not played by anyone yet.

The sibling draft release-v0-1-0-bugs tells the same release as the story of the bugs packaging found. Pick one.

Video: none recorded yet. Fallback until it is: video/renders/dive-giant-x.mp4 (already posted on 2026-10-08, so a new clip is better).
