import { execFileSync, spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { fail } from './fail.mjs';

const tools = () => {
  const ffprobe = process.env.HYPERFRAMES_FFPROBE_PATH;
  const ffmpeg = process.env.HYPERFRAMES_FFMPEG_PATH;
  if (!ffprobe || !ffmpeg) fail('source video/env.sh first (HYPERFRAMES_FFMPEG_PATH and HYPERFRAMES_FFPROBE_PATH are not set)');
  return { ffprobe, ffmpeg };
};

export const probeDuration = (file) => {
  const out = execFileSync(tools().ffprobe, ['-v', 'error', '-show_entries', 'format=duration', '-of', 'csv=p=0', file], { encoding: 'utf8' });
  const value = Number.parseFloat(out.trim());
  if (!Number.isFinite(value) || value <= 0) fail(`cannot read the duration of ${file}`);
  return value;
};

export const hasAudio = (file) => {
  const out = execFileSync(tools().ffprobe, ['-v', 'error', '-select_streams', 'a', '-show_entries', 'stream=index', '-of', 'csv=p=0', file], { encoding: 'utf8' });
  return out.trim().length > 0;
};

export const blankLeadEnd = (file, length) => {
  const { stderr } = spawnSync(
    tools().ffmpeg,
    ['-hide_banner', '-nostats', '-i', file, '-vf', 'negate,blackdetect=d=0.3:pix_th=0.04:pic_th=0.98', '-an', '-f', 'null', '-'],
    { encoding: 'utf8' },
  );
  let end = 0;
  for (const match of String(stderr).matchAll(/black_start:([\d.]+) black_end:([\d.]+)/g)) {
    const blankEnd = Number.parseFloat(match[2]);
    if (blankEnd < length - 0.5) end = Math.max(end, blankEnd);
  }
  return end;
};

export const extractFrame = (src, at, out) => {
  execFileSync(tools().ffmpeg, ['-v', 'error', '-y', '-ss', String(Math.max(0, at)), '-i', src, '-frames:v', '1', out]);
  if (!existsSync(out)) fail(`could not extract a still from ${src} at ${at}s`);
};
