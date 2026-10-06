import { enter, escapeHtml, optionalTextWithin, rich, textWithin } from './shared.mjs';

export default {
  type: 'cover',
  validate(visual) {
    const errors = [];
    optionalTextWithin(errors, visual.kicker, 'kicker', 40);
    textWithin(errors, visual.title, 'title', 48);
    optionalTextWithin(errors, visual.lead, 'lead', 180);
    return errors;
  },
  html(visual) {
    return `<div class="panel cover">
  <div class="rule" ${enter(0, 'left')}></div>
  ${visual.kicker ? `<span class="kicker" ${enter(0)}>${escapeHtml(visual.kicker)}</span>` : ''}
  <h1 class="heading" ${enter(1)}>${rich(visual.title)}</h1>
  ${visual.lead ? `<p class="lead" ${enter(2)}>${rich(visual.lead)}</p>` : ''}
</div>`;
  },
};
