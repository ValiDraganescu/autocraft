import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { probeDuration } from './ffmpeg.mjs';
import { SKILL_DIR } from './paths.mjs';

export const voiceCache = (buildDir) => {
  const path = join(buildDir, 'tts.json');
  const entries = existsSync(path) ? JSON.parse(readFileSync(path, 'utf8')) : {};
  return {
    narrate(scene, { voice, speed, out }) {
      const key = createHash('sha256').update(`${voice}|${speed}|${scene.narration}`).digest('hex');
      if (entries[scene.id] !== key || !existsSync(out)) {
        console.log(`build: voice for "${scene.id}"`);
        execFileSync('bash', [join(SKILL_DIR, 'scripts', 'tts.sh'), out, scene.narration], {
          stdio: 'inherit',
          env: { ...process.env, VIDEO_TTS_VOICE: voice, VIDEO_TTS_SPEED: speed },
        });
        entries[scene.id] = key;
      }
      return probeDuration(out);
    },
    save() {
      writeFileSync(path, `${JSON.stringify(entries, null, 2)}\n`);
    },
  };
};
