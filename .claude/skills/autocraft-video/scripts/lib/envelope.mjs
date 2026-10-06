// A volume lane (HyperFrames `data-automation`) that ducks under the voices:
// `level` away from them, `low` while one speaks, `ramp` seconds between,
// times a fade in over `fadeIn` and out over the last `fadeOut` seconds.
// `voices`: [on, off] in the lane's own seconds.
const round = (value) => Math.round(value * 1000) / 1000;

export const duckLane = ({ length, level, low, ramp, fadeIn, fadeOut, voices }) => {
  const duck = (t) => {
    let v = level;
    for (const [on, off] of voices) {
      if (t >= on && t <= off) v = Math.min(v, low);
      else if (t > on - ramp && t < on) v = Math.min(v, level + (low - level) * ((t - (on - ramp)) / ramp));
      else if (t > off && t < off + ramp) v = Math.min(v, low + (level - low) * ((t - off) / ramp));
    }
    return v;
  };
  const fade = (t) => Math.max(0, Math.min(1, fadeIn > 0 ? t / fadeIn : 1, fadeOut > 0 ? (length - t) / fadeOut : 1));
  const times = new Set([0, fadeIn, length - fadeOut, length]);
  for (const [on, off] of voices) [on - ramp, on, off, off + ramp].forEach((t) => times.add(t));
  const points = [...times]
    .filter((t) => t >= 0 && t <= length)
    .map(round)
    .sort((a, b) => a - b)
    .filter((t, i, all) => i === 0 || t !== all[i - 1])
    .map((t) => ({ t, v: round(duck(t) * fade(t)) }));
  return JSON.stringify({ version: 1, lanes: [{ target: 'volume', points }] });
};
