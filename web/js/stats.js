// OCGDB web UI -- statistics view: a handful of hand-rolled inline-SVG
// charts (bar / line / donut). No charting library; each chart is ~30-40
// lines and scales via viewBox so it stays responsive without JS resize
// handling.
'use strict';

(function () {
  const t = () => window.I18N;
  const PALETTE = ['#4f7cff', '#33b679', '#f2a93b', '#e8604c', '#9a6bd6', '#37bcc4', '#c95fa0', '#8a9a5b'];

  function esc(s) {
    return String(s == null ? '' : s).replace(/[&<>"']/g, (c) => ({
      '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
    }[c]));
  }

  function svg(inner, w, h) {
    return '<svg viewBox="0 0 ' + w + ' ' + h + '" class="chart-svg" role="img">' + inner + '</svg>';
  }

  function barChart(data, labelKey, valueKey, opts) {
    opts = opts || {};
    const w = 640, h = opts.height || 220, padL = 36, padB = 42, padT = 10, padR = 10;
    if (!data.length) return '';
    const max = Math.max(1, ...data.map((d) => d[valueKey]));
    const innerW = w - padL - padR;
    const innerH = h - padT - padB;
    const barW = innerW / data.length;
    let bars = '';
    let labels = '';
    data.forEach((d, i) => {
      const val = d[valueKey];
      const bh = (val / max) * innerH;
      const x = padL + i * barW;
      const y = padT + (innerH - bh);
      bars += '<rect x="' + (x + barW * 0.12) + '" y="' + y + '" width="' + (barW * 0.76) + '" height="' + bh +
        '" fill="' + PALETTE[i % PALETTE.length] + '" rx="2"><title>' + esc(d[labelKey]) + ': ' + val + '</title></rect>';
      if (data.length <= 24 || i % Math.ceil(data.length / 24) === 0) {
        labels += '<text x="' + (x + barW / 2) + '" y="' + (h - padB + 16) + '" class="chart-axis-label" text-anchor="middle">' +
          esc(String(d[labelKey])) + '</text>';
      }
    });
    const axis = '<line x1="' + padL + '" y1="' + (padT + innerH) + '" x2="' + (w - padR) + '" y2="' + (padT + innerH) + '" class="chart-axis"/>';
    return svg(axis + bars + labels, w, h);
  }

  function lineChart(data, labelKey, valueKey) {
    const w = 640, h = 220, padL = 40, padB = 30, padT = 10, padR = 10;
    if (!data.length) return '';
    const max = Math.max(1, ...data.map((d) => d[valueKey]));
    const innerW = w - padL - padR;
    const innerH = h - padT - padB;
    const stepX = data.length > 1 ? innerW / (data.length - 1) : 0;
    let points = data.map((d, i) => {
      const x = padL + i * stepX;
      const y = padT + innerH - (d[valueKey] / max) * innerH;
      return x + ',' + y;
    });
    const path = 'M' + points.join(' L');
    const area = 'M' + padL + ',' + (padT + innerH) + ' L' + points.join(' L') + ' L' + (padL + innerW) + ',' + (padT + innerH) + ' Z';
    const dots = data.map((d, i) => {
      const [x, y] = points[i].split(',');
      return '<circle cx="' + x + '" cy="' + y + '" r="2.6" class="chart-dot"><title>' + esc(d[labelKey]) + ': ' + d[valueKey] + '</title></circle>';
    }).join('');
    const step = Math.max(1, Math.ceil(data.length / 12));
    const labels = data.map((d, i) => {
      if (i % step !== 0 && i !== data.length - 1) return '';
      const [x] = points[i].split(',');
      return '<text x="' + x + '" y="' + (h - padB + 16) + '" class="chart-axis-label" text-anchor="middle">' + esc(d[labelKey]) + '</text>';
    }).join('');
    const axis = '<line x1="' + padL + '" y1="' + (padT + innerH) + '" x2="' + (w - padR) + '" y2="' + (padT + innerH) + '" class="chart-axis"/>';
    return svg(axis + '<path d="' + area + '" class="chart-area"/><path d="' + path + '" class="chart-line"/>' + dots + labels, w, h);
  }

  function donutChart(data, labelKey, valueKey) {
    const w = 260, h = 220;
    const cx = 110, cy = 110, r = 80, r0 = 46;
    const total = data.reduce((s, d) => s + d[valueKey], 0) || 1;
    let angle = -Math.PI / 2;
    let paths = '';
    let legend = '';
    data.forEach((d, i) => {
      const frac = d[valueKey] / total;
      const a0 = angle;
      const a1 = angle + frac * Math.PI * 2;
      angle = a1;
      const large = a1 - a0 > Math.PI ? 1 : 0;
      const x0 = cx + r * Math.cos(a0), y0 = cy + r * Math.sin(a0);
      const x1 = cx + r * Math.cos(a1), y1 = cy + r * Math.sin(a1);
      const xi0 = cx + r0 * Math.cos(a1), yi0 = cy + r0 * Math.sin(a1);
      const xi1 = cx + r0 * Math.cos(a0), yi1 = cy + r0 * Math.sin(a0);
      const d0 = 'M' + x0 + ',' + y0 + ' A' + r + ',' + r + ' 0 ' + large + ' 1 ' + x1 + ',' + y1 +
        ' L' + xi0 + ',' + yi0 + ' A' + r0 + ',' + r0 + ' 0 ' + large + ' 0 ' + xi1 + ',' + yi1 + ' Z';
      const color = PALETTE[i % PALETTE.length];
      paths += '<path d="' + d0 + '" fill="' + color + '"><title>' + esc(d[labelKey]) + ': ' + d[valueKey] + '</title></path>';
      const pct = Math.round((d[valueKey] / total) * 100);
      legend += '<div class="chart-legend-item"><span class="chart-swatch" style="background:' + color + '"></span>' +
        esc(d[labelKey]) + ' <span class="muted">(' + pct + '%)</span></div>';
    });
    return '<div class="chart-donut-wrap">' + svg(paths, w, h) + '<div class="chart-legend">' + legend + '</div></div>';
  }

  function card(titleKey, bodyHtml) {
    return '<section class="stats-card">' +
      '<h2>' + t().t(titleKey) + '</h2>' +
      (bodyHtml || '<div class="panel-empty small">' + t().t('stats.noColumn') + '</div>') +
      '</section>';
  }

  async function mount(panel) {
    panel.innerHTML =
      '<div class="stats-header"><h1>' + t().t('stats.title') + '</h1>' +
      '<button type="button" class="btn" data-act="refresh">' + t().t('stats.refresh') + '</button></div>' +
      '<div class="stats-grid"><div class="panel-loading">' + t().t('common.loading') + '</div></div>';

    panel.querySelector('[data-act="refresh"]').addEventListener('click', () => load(panel, true));
    load(panel, false);
  }

  async function load(panel, refresh) {
    const grid = panel.querySelector('.stats-grid');
    grid.innerHTML = '<div class="panel-loading">' + t().t('common.loading') + '</div>';
    try {
      const s = await window.OcgdbApi.stats(refresh);
      grid.innerHTML =
        card('stats.results', s.results.length ? donutChart(s.results, 'result', 'count') : '') +
        card('stats.perYear', s.perYear.length ? lineChart(s.perYear, 'year', 'count') : '') +
        card('stats.topEco', s.topEco.length ? barChart(s.topEco, 'eco', 'count') : '') +
        card('stats.eloDist', s.eloBuckets.length ? barChart(s.eloBuckets, 'bucket', 'count') : '') +
        card('stats.plyDist', s.plyBuckets.length ? barChart(s.plyBuckets, 'bucket', 'count') : '');
    } catch (e) {
      grid.innerHTML = '<div class="panel-error">' + t().t('common.dbNotLoaded') + '</div>';
    }
  }

  window.OcgdbStats = { mount };
})();
