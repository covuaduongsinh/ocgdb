// OCGDB web UI -- game browser: filter form + paginated results table.
// Filter/sort/page state lives in the URL hash (#browse?white=...&offset=...)
// so it's shareable and back/forward just works via the router in app.js.
'use strict';

(function () {
  const t = () => window.I18N;
  const PAGE_SIZE = 25;
  const PRESET_KEY = 'ocgdbBrowsePreset';
  const FILTER_FIELDS = [
    'player', 'white', 'black', 'event', 'site', 'eco', 'result',
    'minElo', 'maxElo', 'dateFrom', 'dateTo', 'minPly',
  ];
  // th column key -> the `sort` value the server understands (apiGamesJson,
  // src/server.cpp). All eight browse columns are sortable server-side.
  const SORT_COLS = {
    colId: 'id', colWhite: 'white', colBlack: 'black', colResult: 'result',
    colEvent: 'event', colDate: 'date', colEco: 'eco', colPly: 'plycount',
  };

  function loadPreset() {
    try {
      const raw = localStorage.getItem(PRESET_KEY);
      return raw ? JSON.parse(raw) : null;
    } catch (e) { return null; }
  }

  function savePreset(form) {
    const fd = new FormData(form);
    const data = {};
    FILTER_FIELDS.forEach((k) => { const v = fd.get(k); if (v) data[k] = v; });
    localStorage.setItem(PRESET_KEY, JSON.stringify(data));
  }

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
      result: '', eco: '', minElo: '', maxElo: '', dateFrom: '', dateTo: '',
      minPly: '', sort: 'id', dir: 'asc',
    }, params);

    const hasPreset = !!loadPreset();

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
        field('maxElo', t().t('browse.maxElo'), p.maxElo, false, 'number') +
        field('dateFrom', t().t('browse.dateFrom'), p.dateFrom, false, 'text', '1990-01-01') +
        field('dateTo', t().t('browse.dateTo'), p.dateTo, false, 'text', '2030-12-31') +
        field('minPly', t().t('browse.minPly'), p.minPly, false, 'number') +
        '<div class="field field-actions">' +
          '<button type="submit" class="btn btn-primary">' + t().t('common.apply') + '</button>' +
          '<button type="button" class="btn" data-act="reset">' + t().t('common.reset') + '</button>' +
          '<button type="button" class="btn" data-act="save-preset">' + t().t('browse.savePreset') + '</button>' +
          '<button type="button" class="btn" data-act="load-preset"' + (hasPreset ? '' : ' disabled') + '>' + t().t('browse.loadPreset') + '</button>' +
          '<span class="preset-saved muted small" data-role="preset-saved" hidden>' + t().t('browse.presetSaved') + '</span>' +
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
      // The sort/dir controls live on the results table's column headers now
      // (see renderTable), not in this filter form -- carry the current
      // sort over so applying a filter doesn't silently reset it to ID asc.
      if (p.sort && p.sort !== 'id') next.sort = p.sort;
      if (p.dir && p.dir !== 'asc') next.dir = p.dir;
      next.offset = '0';
      window.OcgdbNav.navigate('browse', next);
    });
    form.querySelector('[data-act="reset"]').addEventListener('click', () => {
      // Clear the fields directly rather than relying on the resulting
      // navigate() to re-render the form: if the URL hash has no filter
      // params yet (e.g. the user typed into a field but never hit Apply),
      // navigating to the already-current "#browse" hash is a no-op --
      // location.hash assignment doesn't fire hashchange when unchanged --
      // so the stale typed values would otherwise stick around.
      form.querySelectorAll('input[name], select[name]').forEach((el) => { el.value = ''; });
      window.OcgdbNav.navigate('browse', {});
    });
    form.querySelector('[data-act="save-preset"]').addEventListener('click', () => {
      savePreset(form);
      form.querySelector('[data-act="load-preset"]').disabled = false;
      const savedEl = form.querySelector('[data-role="preset-saved"]');
      savedEl.hidden = false;
      clearTimeout(savedEl._hideTimer);
      savedEl._hideTimer = setTimeout(() => { savedEl.hidden = true; }, 2500);
    });
    form.querySelector('[data-act="load-preset"]').addEventListener('click', () => {
      const preset = loadPreset();
      if (!preset) return;
      window.OcgdbNav.navigate('browse', preset);
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

  async function loadResults(panel, p) {
    const resultsEl = panel.querySelector('.browse-results');
    const pagerEl = panel.querySelector('.browse-pager');
    const warnEl = panel.querySelector('[data-role="warning"]');
    try {
      const data = await window.OcgdbApi.games(Object.assign({}, p, { limit: PAGE_SIZE }));
      warnEl.textContent = data.exactTotal ? '' : t().t('browse.approxTotal');
      renderTable(resultsEl, data.games, p);
      renderPager(pagerEl, data, p);
    } catch (e) {
      resultsEl.innerHTML = '<div class="panel-error">' + t().t('common.dbNotLoaded') + '</div>';
      pagerEl.innerHTML = '';
    }
  }

  function renderTable(el, games, p) {
    if (!games.length) {
      el.innerHTML = '<div class="panel-empty">' + t().t('common.noData') + '</div>';
      return;
    }
    let html = '<div class="table-scroll"><table class="games-table"><thead><tr>' +
      ['colId', 'colWhite', 'colBlack', 'colResult', 'colEvent', 'colDate', 'colEco', 'colPly']
        .map((k) => {
          const sortKey = SORT_COLS[k];
          const active = p.sort === sortKey;
          const arrow = active ? (p.dir === 'desc' ? ' &#9660;' : ' &#9650;') : '';
          return '<th><button type="button" class="sort-th' + (active ? ' sort-th-active' : '') +
            '" data-sort-col="' + sortKey + '">' + t().t('browse.' + k) + arrow + '</button></th>';
        }).join('') +
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
    el.querySelectorAll('.sort-th').forEach((btn) => {
      btn.addEventListener('click', () => {
        const col = btn.dataset.sortCol;
        const nextDir = (p.sort === col && p.dir === 'asc') ? 'desc' : 'asc';
        window.OcgdbNav.navigate('browse', Object.assign({}, p, { sort: col, dir: nextDir, offset: '0' }));
      });
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
