import { enter, head, listWithin, optionalTextWithin, rich, textWithin } from './shared.mjs';

const listComponent = (type, className) => ({
  type,
  validate(visual) {
    const errors = [];
    optionalTextWithin(errors, visual.kicker, 'kicker', 40);
    textWithin(errors, visual.title, 'title', 70);
    listWithin(errors, visual.items, 'items', 1, 5).forEach((item, index) => textWithin(errors, item, `items[${index}]`, 90));
    return errors;
  },
  html(visual) {
    const items = visual.items
      .map((item, index) => `<div class="item" ${enter(2 + index, 'left')}><span class="mark"></span><span>${rich(item)}</span></div>`)
      .join('\n    ');
    return `<div class="panel">
  ${head(visual, { headingStep: 1 })}
  <div class="${className}">
    ${items}
  </div>
</div>`;
  },
});

export const bullets = listComponent('bullets', 'list');
export const checks = listComponent('checks', 'list checks');
