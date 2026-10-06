import { escapeHtml, plain } from './components/shared.mjs';
import { BRAND, CANVAS, FORMAT, TIMING } from './layout.mjs';

const MOTION = {
  up: { from: '{ opacity: 0, y: 28 }', to: '{ opacity: 1, y: 0, duration: 0.6, ease: "power3.out" }' },
  left: { from: '{ opacity: 0, x: -36 }', to: '{ opacity: 1, x: 0, duration: 0.55, ease: "power3.out" }' },
  pop: { from: '{ opacity: 0, scale: 0.86 }', to: '{ opacity: 1, scale: 1, duration: 0.6, ease: "back.out(1.8)" }' },
};

const round = (value) => Math.round(value * 1000) / 1000;

const chrome = ({ index, total, title }) => `<footer class="chrome">
  <div class="bar"><span class="brand">${escapeHtml(BRAND)} <span>· ${escapeHtml(plain(title))}</span></span><span class="count">${String(index + 1).padStart(2, '0')} / ${String(total).padStart(2, '0')}</span></div>
  <div class="track"><div class="fill"></div></div>
</footer>`;

const motionLines = (scope, body, duration) => {
  const steps = new Map();
  for (const match of body.matchAll(/data-in="(\w+)" data-step="(\d+)"/g)) {
    const [, motion, step] = match;
    if (!MOTION[motion]) throw new Error(`unknown motion "${motion}"`);
    steps.set(`${step}|${motion}`, { step: Number(step), motion });
  }
  const lines = [...steps.values()]
    .sort((a, b) => a.step - b.step)
    .map(({ step, motion }) => {
      const at = round(TIMING.firstStep + step * TIMING.stepGap);
      return `tl.fromTo("${scope} [data-in='${motion}'][data-step='${step}']", ${MOTION[motion].from}, ${MOTION[motion].to}, ${at});`;
    });
  if (body.includes('data-count')) {
    lines.push(`tl.from("${scope} [data-count]", { textContent: 0, snap: { textContent: 1 }, duration: 1.2, ease: "power2.out" }, ${round(TIMING.firstStep + 2 * TIMING.stepGap)});`);
  }
  lines.push(`tl.to("${scope} .stage", { opacity: 0, duration: ${TIMING.fade}, ease: "power1.in" }, ${round(duration - TIMING.fade)});`);
  return lines;
};

export const sceneFile = ({ id, index, total, title, duration, body }) => {
  const scope = `[data-composition-id='scene-${id}']`;
  const lines = [
    `tl.fromTo("${scope} .stage", { opacity: 0 }, { opacity: 1, duration: ${TIMING.fade}, ease: "power1.out" }, 0);`,
    `tl.fromTo("${scope} .fill", { scaleX: ${round(index / total)} }, { scaleX: ${round((index + 1) / total)}, duration: 0.9, ease: "power2.inOut" }, 0.1);`,
    ...motionLines(scope, body, duration),
  ];
  return `<!doctype html>
<html lang="en">
  <head>
    <meta charset="UTF-8" />
  </head>
  <body>
    <template>
      <style>
        #root { position: absolute; inset: 0; }
      </style>
      <div id="root" data-composition-id="scene-${id}" data-width="${CANVAS.width}" data-height="${CANVAS.height}">
        <div class="stage${FORMAT.vertical ? ` vertical ${FORMAT.name}` : ''}">
${body}
${chrome({ index, total, title })}
        </div>
      </div>
      <script>
        const tl = gsap.timeline({ paused: true });
        ${lines.join('\n        ')}
        window.__timelines["scene-${id}"] = tl;
      </script>
    </template>
  </body>
</html>
`;
};
