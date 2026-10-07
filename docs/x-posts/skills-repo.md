---
status: wip
date:
angle: build
video: video/renders/skills-repo-x.mp4
posted: 
---

## Post

Claude Code writes all of Ringshadow's code, Grok paints the art, Suno makes the music. I direct. The agents work from Claude Code skills, written playbooks for one job each, and they're all public in the repo.

This video came out of one of them. The video skill staged the Longbow shots, recorded them and cut them: a tank that anchors into artillery and shells an enemy base.

The other skills take concept art to a finished Unreal model, set the light language of the buildings, and run the tests. Everything is CC0, so copy them into your own project.

## Reply

The skills the agents work from, video skill included: github.com/ValiDraganescu/autocraft/tree/main/.claude/skills

## Notes

Round 2 of the skill benchmark (skill), voted up.

Video: the longbow-siege-giant sim (16 s, dusk, third person: drives in, anchors, shells the base), 20.4 s, the X file 9.7 MB:

    uv run .claude/skills/ringshadow-video/scripts/giantcut.py video/clips/longbow-siege-giant.mp4 --down 10.4,13.6 --end 15.9 --title "Made with open skills." --music firefight_2 --music-at 20 --caption "Staged, recorded and cut by the video skill." --caption-at 1,7 --out video/renders/skills-repo.mp4 --x
