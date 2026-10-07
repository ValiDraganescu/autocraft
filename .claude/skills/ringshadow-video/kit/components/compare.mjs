import { enter, escapeHtml, head, listWithin, optionalTextWithin, rich, textWithin } from './shared.mjs';

const sideErrors = (errors, side, name) => {
  optionalTextWithin(errors, side?.label, `${name}.label`, 24);
  listWithin(errors, side?.items, `${name}.items`, 1, 4).forEach((item, index) => textWithin(errors, item, `${name}.items[${index}]`, 70));
};

const side = (entry, className, fallback, step) => {
  const items = entry.items.map((item) => `<div class="item"><span class="mark"></span><span>${rich(item)}</span></div>`).join('');
  return `<div class="side ${className}" ${enter(step)}><span class="label">${escapeHtml(entry.label ?? fallback)}</span><div class="list">${items}</div></div>`;
};

export default {
  type: 'compare',
  validate(visual) {
    const errors = [];
    optionalTextWithin(errors, visual.kicker, 'kicker', 40);
    textWithin(errors, visual.title, 'title', 70);
    sideErrors(errors, visual.before, 'before');
    sideErrors(errors, visual.after, 'after');
    return errors;
  },
  html(visual) {
    return `<div class="panel">
  ${head(visual, { headingStep: 1 })}
  <div class="compare">
    ${side(visual.before, 'before', 'Before', 2)}
    ${side(visual.after, 'after', 'After', 4)}
  </div>
</div>`;
  },
};
