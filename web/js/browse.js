// OCGDB web UI -- game browser: filter form + paginated results table.
// Filter/sort/page state lives in the URL hash (#browse?white=...&offset=...)
// so it's shareable and back/forward just works via the router in app.js.
'use strict';

(function () {
  const t = () => window.I18N;
  const PAGE_SIZE = 25;

  function esc(s) {
    return String(s == null ? '' : s).replace(/[&<>"']/g, (c) => ({
      '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
    }[c]));
  }

  let debounceTimer = null;
  function wireAutocomplete(input, listEl, fetchFn) {
    input.addEventListener('input', () => {
      clearTimeout(debounceTimer);
      const q = input.value.trim();
      if (q.length < 2) return;
      debounceTimer = setTimeout(async () => {
        try {
          const items = await fetchFn(q, 10);
          listEl.innerHTML = items.map((it) => '<option value="' + esc(it.name) + '"></option>').join('');
        } catch (e) { /* autocomplete is best-effort */ }
      }, 200);
    });
  }

  function mount(panel, params) {
    const p = Object.assign({
      offset: '0', white: '', black: '', player: '', event: '', site: '',
      result: '', eco: '', minElo: '', dateFrom: '', dateTo: '',
      sort: 'id', dir: 'asc',
    }, params);

    panel.innerHTML =
      '<h1>' + t().t('browse.title') + '</h1>' +
      '<form class="filters-form">' +
        field('player', t().t('browse.player'), p.player, true) +
        field('white', t().t('browse.white'), p.white, true) +
        field('black', t().t('browse.black'), p.black, true) +
        field('event', t().t('browse.event'), p.event, true) +
        field('site', t().t('browse.site'), p.site, true) +
        field('eco', t().t('browse.eco'), p.eco, false) +
        '<label class="field">' + t().t('browse.result') +
          '<select name="result">' +
            '<option value=""' + (p.result === '' ? ' selected' : '') + '>' + t().t('browse.resultAny') + '</option>' +
            ['1-0', '0-1', '1/2-1/2', '*'].map((r) =>
              '<option value="' + r + '"' + (p.result === r ? ' selected' : '') + '>' + r + '</option>').join('') +
          '</select></label>' +
        field('minElo', t().t('browse.minElo'), p.minElo, false, 'number') +
        field('dateFrom', t().t('browse.dateFrom'), p.dateFrom, false, 'text', '1990-01-01') +
        field('dateTo', t().t('browse.dateTo'), p.dateTo, false, 'text', '2030-12-31') +
        '<label class="field">' + t().t('browse.sort') +
          '<select name="sort">' +
            ['id', 'date', 'elo', 'plycount'].map((s) =>
              '<option value="' + s + '"' + (p.sort === s ? ' selected' : '') + '>' + t().t('browse.sort' + cap(s)) + '</option>').join('') +
          '</select></label>' +
        '<label class="field">' + t().t('browse.dir') +
          '<select name="dir">' +
            '<option value="asc"' + (p.dir === 'asc' ? ' selected' : '') + '>' + t().t('browse.dirAsc') + '</option>' +
            '<option value="desc"' + (p.dir === 'desc' ? ' selected' : '') + '>' + t().t('browse.dirDesc') + '</option>' +
          '</select></label>' +
        '<div class="field field-actions">' +
          '<button type="submit" class="btn btn-primary">' + t().t('common.apply') + '</button>' +
          '<button type="button" class="btn" data-act="reset">' + t().t('common.reset') + '</button>' +
        '</div>' +
      '</form>' +
      '<datalist id="dl-players"></datalist>' +
      '<datalist id="dl-events"></datalist>' +
      '<datalist id="dl-sites"></datalist>' +
      '<div class="browse-warning muted small" data-role="warning"></div>' +
      '<div class="browse-results"><div class="panel-loading">' + t().t('common.loading') + '</div></div>' +
      '<div class="browse-pager"></div>';

    const form = panel.querySelector('.filters-form');
    ['player', 'white', 'black'].forEach((name) => {
      wireAutocomplete(form.querySelector('[name="' + name + '"]'), panel.querySelector('#dl-players'), window.OcgdbApi.players);
    });
    wireAutocomplete(form.querySelector('[name="event"]'), panel.querySelector('#dl-events'), window.OcgdbApi.events);
    wireAutocomplete(form.querySelector('[name="site"]'), panel.querySelector('#dl-sites'), window.OcgdbApi.sites);

    form.addEventListener('submit', (e) => {
      e.preventDefault();
      const next = {};
      new FormData(form).forEach((v, k) => { if (v) next[k] = v; });
      next.offset = '0';
      window.OcgdbNav.navigate('browse', next);
    });
    form.querySelector('[data-act="reset"]').addEventListener('click', () => {
      window.OcgdbNav.navigate('browse', {});
    });

    loadResults(panel, p);
  }

  function field(name, label, value, autocomplete, type, placeholder) {
    return '<label class="field">' + label +
      '<input type="' + (type || 'text') + '" name="' + name + '" value="' + esc(value) + '"' +
      (autocomplete ? ' list="dl-' + (name === 'player' || name === 'white' || name === 'black' ? 'players' : name + 's') + '" autocomplete="off"' : '') +
      (placeholder ? ' placeholder="' + esc(placeholder) + '"' : '') +
      '></label>';
  }

  function cap(s) { return s.charAt(0).toUpperCase() + s.slice(1); }

  async function loadResults(panel, p) {
    const resultsEl = panel.querySelector('.browse-results');
    const pagerEl = panel.querySelector('.browse-pager');
    const warnEl = panel.querySelector('[data-role="warning"]');
    try {
      const data = await window.OcgdbApi.games(Object.assign({}, p, { limit: PAGE_SIZE }));
      warnEl.textContent = data.exactTotal ? '' : t().t('browse.approxTotal');
      renderTable(resultsEl, data.games);
      renderPager(pagerEl, data, p);
    } catch (e) {
      resultsEl.innerHTML = '<div class="panel-error">' + t().t('common.dbNotLoaded') + '</div>';
      pagerEl.innerHTML = '';
    }
  }

  function renderTable(el, games) {
    if (!games.length) {
      el.innerHTML = '<div class="panel-empty">' + t().t('common.noData') + '</div>';
      return;
    }
    let html = '<div class="table-scroll"><table class="games-table"><thead><tr>' +
      ['colId', 'colWhite', 'colBlack', 'colResult', 'colEvent', 'colDate', 'colEco', 'colPly']
        .map((k) => '<th>' + t().t('browse.' + k) + '</th>').join('') +
      '</tr></thead><tbody>';
    games.forEach((g) => {
      html += '<tr class="game-row" data-id="' + g.id + '">' +
        '<td>' + g.id + '</td>' +
        '<td>' + esc(g.white) + (g.whiteElo ? ' <span class="muted">(' + g.whiteElo + ')</span>' : '') + '</td>' +
        '<td>' + esc(g.black) + (g.blackElo ? ' <span class="muted">(' + g.blackElo + ')</span>' : '') + '</td>' +
        '<td>' + esc(g.result || '') + '</td>' +
        '<td>' + esc(g.event) + '</td>' +
        '<td>' + esc(g.date || '') + '</td>' +
        '<td>' + esc(g.eco || '') + '</td>' +
        '<td>' + (g.plyCount == null ? '' : g.plyCount) + '</td>' +
        '</tr>';
    });
    html += '</tbody></table></div>';
    el.innerHTML = html;
    el.querySelectorAll('.game-row').forEach((tr) => {
      tr.addEventListener('click', () => window.OcgdbNav.openGameViewer(Number(tr.dataset.id)));
    });
  }

  function renderPager(el, data, p) {
    const offset = Number(p.offset) || 0;
    const total = data.total;
    const hasPrev = offset > 0;
    const hasNext = offset + data.games.length < total;
    el.innerHTML =
      '<button type="button" class="btn" data-act="prev"' + (hasPrev ? '' : ' disabled') + '>' + t().t('common.prev') + '</button>' +
      '<span class="pager-info">' + (offset + 1) + '&ndash;' + (offset + data.games.length) + ' ' + t().t('common.of') +
        ' ' + (data.exactTotal ? '' : t().t('common.approx')) + total + ' ' + t().t('common.games') + '</span>' +
      '<button type="button" class="btn" data-act="next"' + (hasNext ? '' : ' disabled') + '>' + t().t('common.next') + '</button>';
    const prevBtn = el.querySelector('[data-act="prev"]');
    const nextBtn = el.querySelector('[data-act="next"]');
    if (hasPrev) prevBtn.addEventListener('click', () => {
      window.OcgdbNav.navigate('browse', Object.assign({}, p, { offset: String(Math.max(0, offset - PAGE_SIZE)) }));
    });
    if (hasNext) nextBtn.addEventListener('click', () => {
      window.OcgdbNav.navigate('browse', Object.assign({}, p, { offset: String(offset + PAGE_SIZE) }));
    });
  }

  window.OcgdbBrowse = { mount };
})();
