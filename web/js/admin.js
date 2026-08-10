// OCGDB web UI -- Admin tab: the control plane for `ocgdb -server`.
// Three things live here: the registry of known .ocgdb.db3 files (switch
// which one every other view browses), a form to submit CLI tasks
// (create/merge/export/dup/bench/query) as background jobs, and a live
// table of those jobs with log/cancel. Every /api/admin/* call requires
// the token printed on server startup (see server.cpp) -- see api.js's
// adminFetch() for how that's attached.
'use strict';

(function () {
  const t = () => window.I18N;

  const CREATE_OPTS = [
    'moves', 'moves1', 'moves2', 'acceptnewtags', 'discardcomments',
    'discardsites', 'discardnoelo', 'discardfen', 'reseteco', 'nobot', 'bot', 'index',
  ];

  let pollTimer = null;

  function esc(s) {
    return String(s == null ? '' : s).replace(/[&<>"']/g, (c) => ({
      '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
    }[c]));
  }

  function formatBytes(n) {
    if (n == null || n < 0) return '?';
    const units = ['B', 'KB', 'MB', 'GB', 'TB'];
    let v = n, i = 0;
    while (v >= 1024 && i < units.length - 1) { v /= 1024; i++; }
    return (i === 0 ? v : v.toFixed(1)) + ' ' + units[i];
  }

  function splitLines(s) {
    return String(s || '').split('\n').map((x) => x.trim()).filter(Boolean);
  }
  function joinLines(s) { return splitLines(s).join('|'); }

  // Validation errors for /api/admin/* are generated server-side in C++
  // (buildJobArgv in jobs.cpp, pathAllowed/route handlers in server.cpp)
  // and always come back as plain English text -- there's no i18n on the
  // server. Rather than leave every error message stuck in English when
  // the UI is set to Vietnamese, translate the known ones here by prefix
  // (many end in a dynamic path/name that must pass through unchanged).
  // Anything not in this table is shown as-is: an unrecognized English
  // string beats a wrong or missing translation.
  const ERROR_PREFIXES = [
    ['database already exists (set overwrite to replace it): ', 'CSDL đã tồn tại (tick "cho phép ghi đè" để thay thế): '],
    ['output file already exists (set overwrite to replace it): ', 'File đích đã tồn tại (tick "cho phép ghi đè" để thay thế): '],
    ['unsupported option: ', 'Tuỳ chọn không hỗ trợ: '],
    ['path does not exist: ', 'Đường dẫn không tồn tại: '],
    ['unknown task: ', 'Tác vụ không xác định: '],
    ['path is outside -root: ', 'Đường dẫn nằm ngoài thư mục gốc cho phép (-root): '],
    ['invalid path: ', 'Đường dẫn không hợp lệ: '],
    ['could not open as SQLite database: ', 'Không mở được như CSDL SQLite: '],
    ['at least one PGN file is required', 'Cần ít nhất một file PGN'],
    ['an output database path is required', 'Cần đường dẫn CSDL đích'],
    ['a destination database path is required', 'Cần đường dẫn CSDL đích'],
    ['at least one source database or PGN file is required', 'Cần ít nhất một CSDL nguồn hoặc file PGN'],
    ['at least one source database is required', 'Cần ít nhất một CSDL nguồn'],
    ['an output PGN path is required', 'Cần đường dẫn file PGN xuất ra'],
    ['at least one database is required', 'Cần ít nhất một CSDL'],
    ['removing duplicates modifies the database in place; confirm is required', 'Xoá ván trùng sẽ ghi đè CSDL — hãy tick xác nhận.'],
    ['a database path is required', 'Cần đường dẫn CSDL'],
    ['a PQL query is required', 'Cần một biểu thức PQL'],
    ['could not persist job', 'Không lưu được tác vụ'],
    ['invalid -root directory', 'Thư mục -root không hợp lệ'],
    ['empty path', 'Đường dẫn trống'],
    ['path is required', 'Cần nhập đường dẫn'],
    ['not an OCGDB database (no Moves/Moves1/Moves2 field)', 'Không phải CSDL OCGDB hợp lệ (thiếu cột Moves/Moves1/Moves2)'],
    ['dir is required', 'Cần nhập thư mục'],
    ['unknown database id', 'Không tìm thấy CSDL với id này'],
  ];

  function localizeError(msg) {
    const s = String(msg == null ? '' : msg);
    if (t().lang() !== 'vi') return s;
    for (const [en, vi] of ERROR_PREFIXES) {
      if (s === en) return vi;
      if (s.startsWith(en)) return vi + s.slice(en.length);
    }
    return s;
  }

  // ------------------------------------------------------------- mounting

  function mount(panel) {
    if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }

    if (!window.OcgdbApi.hasAdminToken()) {
      renderTokenGate(panel);
      return;
    }
    renderShell(panel);
    refreshAll(panel);
    startPolling(panel);
  }

  function startPolling(panel) {
    pollTimer = setInterval(() => {
      if (panel.hidden || !location.hash.startsWith('#admin')) {
        clearInterval(pollTimer);
        pollTimer = null;
        return;
      }
      refreshStatusAndJobs(panel);
    }, 1500);
  }

  // --------------------------------------------------------- token gate

  function renderTokenGate(panel) {
    panel.innerHTML =
      '<h1>' + t().t('admin.title') + '</h1>' +
      '<div class="admin-token-gate">' +
        '<p class="muted">' + t().t('admin.tokenDesc') + '</p>' +
        '<form class="admin-token-form">' +
          '<label class="field">' + t().t('admin.tokenLabel') +
            '<input type="password" name="token" autocomplete="off" spellcheck="false"></label>' +
          '<button type="submit" class="btn btn-primary">' + t().t('admin.tokenSubmit') + '</button>' +
        '</form>' +
        '<div class="panel-error" data-role="token-error" hidden></div>' +
      '</div>';

    const form = panel.querySelector('.admin-token-form');
    const errEl = panel.querySelector('[data-role="token-error"]');
    form.addEventListener('submit', async (e) => {
      e.preventDefault();
      const token = form.querySelector('[name="token"]').value.trim();
      if (!token) return;
      window.OcgdbApi.setAdminToken(token);
      try {
        await window.OcgdbApi.adminStatus();
        mount(panel);
      } catch (err) {
        window.OcgdbApi.setAdminToken('');
        errEl.textContent = t().t('admin.tokenInvalid');
        errEl.hidden = false;
      }
    });
  }

  // ------------------------------------------------------------- shell

  function renderShell(panel) {
    panel.innerHTML =
      '<h1>' + t().t('admin.title') + '</h1>' +
      '<div class="admin-status-bar" data-role="status"></div>' +

      '<section class="admin-section">' +
        '<h2>' + t().t('admin.dbSection') + '</h2>' +
        '<div class="admin-db-toolbar">' +
          '<form class="admin-add-db-form">' +
            '<label class="field">' + t().t('admin.addPath') + '<input type="text" name="path" required></label>' +
            '<label class="field">' + t().t('admin.addLabel') + '<input type="text" name="label"></label>' +
            '<button type="submit" class="btn btn-small">' + t().t('admin.add') + '</button>' +
          '</form>' +
          '<form class="admin-scan-form">' +
            '<label class="field">' + t().t('admin.scanDir') + '<input type="text" name="dir" required></label>' +
            '<button type="submit" class="btn btn-small">' + t().t('admin.scan') + '</button>' +
          '</form>' +
        '</div>' +
        '<div class="panel-error" data-role="db-error" hidden></div>' +
        '<div data-role="db-table"><div class="panel-loading">' + t().t('common.loading') + '</div></div>' +
      '</section>' +

      '<section class="admin-section">' +
        '<h2>' + t().t('admin.taskSection') + '</h2>' +
        '<div data-role="task-form"></div>' +
      '</section>' +

      '<section class="admin-section">' +
        '<div class="admin-jobs-header">' +
          '<h2>' + t().t('admin.jobsSection') + '</h2>' +
          '<button type="button" class="btn btn-small" data-act="clear-jobs">' + t().t('admin.clearFinished') + '</button>' +
        '</div>' +
        '<div data-role="jobs-table"><div class="panel-loading">' + t().t('common.loading') + '</div></div>' +
      '</section>';

    panel.querySelector('.admin-add-db-form').addEventListener('submit', async (e) => {
      e.preventDefault();
      const form = e.target;
      const errEl = panel.querySelector('[data-role="db-error"]');
      errEl.hidden = true;
      try {
        await window.OcgdbApi.adminAddDatabase(form.path.value.trim(), form.label.value.trim());
        form.reset();
        await refreshDatabases(panel);
      } catch (err) {
        errEl.textContent = localizeError(err.message);
        errEl.hidden = false;
      }
    });

    panel.querySelector('.admin-scan-form').addEventListener('submit', async (e) => {
      e.preventDefault();
      const form = e.target;
      const errEl = panel.querySelector('[data-role="db-error"]');
      errEl.hidden = true;
      try {
        await window.OcgdbApi.adminScanDatabases(form.dir.value.trim());
        await refreshDatabases(panel);
      } catch (err) {
        errEl.textContent = localizeError(err.message);
        errEl.hidden = false;
      }
    });

    panel.querySelector('[data-act="clear-jobs"]').addEventListener('click', async () => {
      try {
        await window.OcgdbApi.adminClearJobs();
        await refreshJobs(panel);
      } catch (err) { /* best-effort */ }
    });

    renderTaskForm(panel, 'create');
  }

  async function refreshAll(panel) {
    await Promise.all([
      refreshStatus(panel),
      refreshDatabases(panel),
      refreshJobs(panel),
    ]);
  }

  async function refreshStatusAndJobs(panel) {
    await Promise.all([refreshStatus(panel), refreshJobs(panel)]);
  }

  // ------------------------------------------------------------- status

  async function refreshStatus(panel) {
    const el = panel.querySelector('[data-role="status"]');
    if (!el) return;
    try {
      const s = await window.OcgdbApi.adminStatus();
      const uptimeMin = Math.floor(s.uptimeMs / 60000);
      el.innerHTML =
        '<span class="admin-status-chip">PID ' + s.pid + '</span>' +
        '<span class="admin-status-chip">' + t().t('admin.uptime') + ': ' + uptimeMin + ' ' + t().t('admin.minutes') + '</span>' +
        '<span class="admin-status-chip">' + s.cpu + ' ' + t().t('admin.cores') + '</span>' +
        '<span class="admin-status-chip">' + (s.activeDb.path ? esc(s.activeDb.path) : t().t('admin.noActiveDb')) +
          (s.activeDb.open ? '' : ' <span class="admin-warning">(' + t().t('admin.closed') + ')</span>') + '</span>' +
        (s.busy ? '<span class="admin-status-chip admin-status-busy">' + t().t('admin.busy') + ' (#' + s.activeJobId + ')</span>' : '');
    } catch (err) {
      if (err.status === 401) { onUnauthorized(panel); return; }
      el.innerHTML = '<span class="admin-warning">' + esc(localizeError(err.message)) + '</span>';
    }
  }

  function onUnauthorized(panel) {
    window.OcgdbApi.setAdminToken('');
    if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
    renderTokenGate(panel);
  }

  // ---------------------------------------------------------- databases

  async function refreshDatabases(panel) {
    const el = panel.querySelector('[data-role="db-table"]');
    if (!el) return;
    try {
      const data = await window.OcgdbApi.adminDatabases();
      renderDatabases(panel, el, data.databases || []);
    } catch (err) {
      if (err.status === 401) { onUnauthorized(panel); return; }
      el.innerHTML = '<div class="panel-error">' + esc(localizeError(err.message)) + '</div>';
    }
  }

  function renderDatabases(panel, el, dbs) {
    if (!dbs.length) {
      el.innerHTML = '<div class="panel-empty">' + t().t('admin.noDatabases') + '</div>';
      return;
    }
    let html = '<div class="table-scroll"><table class="admin-db-table"><thead><tr>' +
      '<th>' + t().t('admin.colDatabase') + '</th>' +
      '<th>' + t().t('admin.colGames') + '</th>' +
      '<th>' + t().t('admin.colMoveField') + '</th>' +
      '<th>' + t().t('admin.colSize') + '</th>' +
      '<th></th>' +
      '</tr></thead><tbody>';
    dbs.forEach((d) => {
      html += '<tr' + (d.isActive ? ' class="admin-row-active"' : '') + '>' +
        '<td><div class="admin-db-label">' + esc(d.label || d.path) + '</div>' +
          '<div class="muted small">' + esc(d.path) + '</div>' +
          (!d.exists ? '<div class="admin-warning small">' + t().t('admin.missing') + '</div>' : '') +
          (d.note ? '<div class="admin-warning small">' + esc(d.note) + '</div>' : '') +
        '</td>' +
        '<td>' + (d.gameCount >= 0 ? Number(d.gameCount).toLocaleString() : '?') + '</td>' +
        '<td>' + esc(d.moveField || '?') + '</td>' +
        '<td>' + formatBytes(d.sizeBytes) + '</td>' +
        '<td class="admin-db-actions">' +
          (d.isActive
            ? '<span class="badge">' + t().t('admin.active') + '</span>'
            : '<button type="button" class="btn btn-small" data-act="activate" data-id="' + d.id + '">' + t().t('admin.activate') + '</button>') +
          ' <button type="button" class="btn btn-small" data-act="remove" data-id="' + d.id + '">' + t().t('admin.remove') + '</button>' +
        '</td></tr>';
    });
    html += '</tbody></table></div>';
    el.innerHTML = html;

    el.querySelectorAll('[data-act="activate"]').forEach((btn) => {
      btn.addEventListener('click', async () => {
        btn.disabled = true;
        try {
          await window.OcgdbApi.adminActivateDatabase(btn.dataset.id);
          await window.OcgdbApp.reload();
          await refreshDatabases(panel);
        } catch (err) {
          alert(localizeError(err.message));
          btn.disabled = false;
        }
      });
    });
    el.querySelectorAll('[data-act="remove"]').forEach((btn) => {
      btn.addEventListener('click', async () => {
        if (!confirm(t().t('admin.confirmRemove'))) return;
        btn.disabled = true;
        try {
          await window.OcgdbApi.adminRemoveDatabase(btn.dataset.id);
          await refreshDatabases(panel);
        } catch (err) {
          alert(localizeError(err.message));
          btn.disabled = false;
        }
      });
    });
  }

  // -------------------------------------------------------- task form

  function renderTaskForm(panel, task) {
    const host = panel.querySelector('[data-role="task-form"]');
    host.innerHTML =
      '<label class="field admin-task-select">' + t().t('admin.task') +
        '<select name="task">' +
          ['create', 'merge', 'export', 'dup', 'bench', 'query', 'index', 'optimize', 'material', 'tree'].map((k) =>
            '<option value="' + k + '"' + (k === task ? ' selected' : '') + '>' + t().t('admin.task.' + k) + '</option>').join('') +
        '</select></label>' +
      '<form class="admin-task-form" data-task="' + task + '">' + taskFieldsHtml(task) + '</form>' +
      '<div class="admin-cmdline-preview"><span class="muted small">' + t().t('admin.previewLabel') + '</span><code data-role="preview"></code></div>' +
      '<div class="panel-error" data-role="task-error" hidden></div>' +
      '<div class="panel-loading" data-role="task-ok" hidden></div>';

    host.querySelector('[name="task"]').addEventListener('change', (e) => {
      renderTaskForm(panel, e.target.value);
    });

    const form = host.querySelector('.admin-task-form');
    const preview = host.querySelector('[data-role="preview"]');
    const errEl = host.querySelector('[data-role="task-error"]');
    const okEl = host.querySelector('[data-role="task-ok"]');

    const updatePreview = () => { preview.textContent = previewCmdline(task, form); };
    form.addEventListener('input', updatePreview);
    form.addEventListener('change', updatePreview);
    updatePreview();

    const uploadInput = form.querySelector('[data-role="pgn-upload"]');
    if (uploadInput) {
      const statusEl = form.querySelector('[data-role="pgn-upload-status"]');
      const pgnField = form.querySelector('[name="pgn"]');
      uploadInput.addEventListener('change', async () => {
        const files = Array.from(uploadInput.files || []);
        uploadInput.value = ''; // allow re-selecting the same file later
        for (const file of files) {
          statusEl.textContent = t().t('admin.uploading', { name: file.name, pct: 0 });
          try {
            const res = await window.OcgdbApi.adminUpload(file, (loaded, total) => {
              statusEl.textContent = t().t('admin.uploading', { name: file.name, pct: Math.round((loaded / total) * 100) });
            });
            const cur = pgnField.value.replace(/\s+$/, '');
            pgnField.value = cur ? cur + '\n' + res.path : res.path;
            pgnField.dispatchEvent(new Event('input', { bubbles: true }));
            statusEl.textContent = t().t('admin.uploadDone', { name: file.name });
          } catch (err) {
            statusEl.textContent = localizeError(err.message);
          }
        }
      });
    }

    form.addEventListener('submit', async (e) => {
      e.preventDefault();
      errEl.hidden = true;
      okEl.hidden = true;

      if (task === 'dup' && form.querySelector('[name="remove"]').checked && !form.querySelector('[name="confirm"]').checked) {
        errEl.textContent = t().t('admin.dupConfirmRequired');
        errEl.hidden = false;
        return;
      }

      const params = collectTaskParams(task, form);
      const submitBtn = form.querySelector('[type="submit"]');
      submitBtn.disabled = true;
      try {
        const res = await window.OcgdbApi.adminSubmitJob(params);
        okEl.textContent = t().t('admin.jobSubmitted', { id: res.id });
        okEl.hidden = false;
        form.reset();
        updatePreview();
        await refreshJobs(panel);
      } catch (err) {
        errEl.textContent = localizeError(err.message);
        errEl.hidden = false;
      } finally {
        submitBtn.disabled = false;
      }
    });
  }

  function taskFieldsHtml(task) {
    const parts = [];
    const text = (name, key, required) =>
      '<label class="field"><span>' + t().t('admin.f.' + key) + '</span>' +
        '<input type="text" name="' + name + '"' + (required ? ' required' : '') + '></label>';
    const num = (name, key) =>
      '<label class="field"><span>' + t().t('admin.f.' + key) + '</span><input type="number" name="' + name + '" min="0"></label>';
    const area = (name, key, required) =>
      '<label class="field field-wide"><span>' + t().t('admin.f.' + key) + '</span>' +
        '<span class="field-hint muted small">' + t().t('admin.oneLineEach') + '</span>' +
        '<textarea name="' + name + '" rows="3"' + (required ? ' required' : '') + '></textarea></label>';
    const check = (name, key) =>
      '<label class="field field-checkbox"><input type="checkbox" name="' + name + '"> ' + t().t('admin.f.' + key) + '</label>';
    // File input styled as a button (label wraps a hidden <input type=file>).
    // Uploaded files land server-side under <root>/uploads (see
    // POST /api/admin/upload, server.cpp) and their returned path is
    // appended to the adjacent "pgn" textarea -- job submission always
    // takes a server path either way, this just gets one there without the
    // user needing filesystem/shell access to the machine running -server.
    const pgnUpload = () =>
      '<div class="field field-wide admin-upload">' +
        '<label class="btn btn-small admin-upload-btn">' + t().t('admin.uploadPgn') +
          '<input type="file" data-role="pgn-upload" accept=".pgn,.txt" multiple hidden>' +
        '</label>' +
        '<span class="small muted" data-role="pgn-upload-status"></span>' +
      '</div>';

    if (task === 'create') {
      parts.push(area('pgn', 'pgn', true));
      parts.push(pgnUpload());
      parts.push(text('db', 'dbOut', true));
      parts.push('<div class="field field-wide"><span>' + t().t('admin.f.opts') + '</span><div class="checkgroup">' +
        CREATE_OPTS.map((o) => '<label class="chip-check"><input type="checkbox" name="opt_' + o + '" value="' + o + '"> ' + o + '</label>').join('') +
        '</div></div>');
      parts.push(num('elo', 'elo'));
      parts.push(num('plycount', 'plycount'));
      parts.push(num('cpu', 'cpu'));
      parts.push(text('desc', 'desc', false));
      parts.push(check('overwrite', 'overwrite'));
    } else if (task === 'merge') {
      parts.push(text('dbDest', 'dbDest', true));
      parts.push(area('dbSources', 'dbSources', false));
      parts.push(area('pgn', 'pgn', false));
      parts.push(pgnUpload());
    } else if (task === 'export') {
      parts.push(area('db', 'dbIn', true));
      parts.push(text('pgnOut', 'pgnOut', true));
      parts.push(check('overwrite', 'overwrite'));
    } else if (task === 'dup') {
      parts.push(area('db', 'dbIn', true));
      parts.push(check('remove', 'remove'));
      parts.push(check('confirm', 'confirm'));
      parts.push(check('printall', 'printall'));
      parts.push(text('report', 'report', false));
    } else if (task === 'bench') {
      parts.push(text('db', 'dbOne', true));
      parts.push(num('cpu', 'cpu'));
    } else if (task === 'query') {
      parts.push(text('db', 'dbOne', true));
      parts.push(text('pql', 'pql', true));
      parts.push('<label class="field"><span>' + t().t('admin.f.printFormat') + '</span><select name="printFormat">' +
        '<option value="">' + t().t('admin.f.printFormatNone') + '</option>' +
        '<option value="printpgn">printpgn</option>' +
        '<option value="printall">printall</option>' +
        '</select></label>');
      parts.push(text('report', 'report', false));
    } else if (task === 'index') {
      parts.push(area('db', 'dbIn', true));
    } else if (task === 'material') {
      parts.push(area('db', 'dbIn', true));
    } else if (task === 'tree') {
      parts.push(area('db', 'dbIn', true));
      parts.push(num('depth', 'depth'));
    } else if (task === 'optimize') {
      parts.push(area('db', 'dbIn', true));
      parts.push(check('vacuum', 'vacuum'));
      parts.push(check('integrity', 'integrity'));
    }

    parts.push('<div class="field field-actions"><button type="submit" class="btn btn-primary">' + t().t('admin.submitJob') + '</button></div>');
    return parts.join('');
  }

  function collectTaskParams(task, form) {
    const val = (name) => { const el = form.querySelector('[name="' + name + '"]'); return el ? el.value.trim() : ''; };
    const checked = (name) => { const el = form.querySelector('[name="' + name + '"]'); return !!(el && el.checked); };
    const params = { task };

    if (task === 'create') {
      params.pgn = joinLines(val('pgn'));
      params.db = val('db');
      const opts = Array.from(form.querySelectorAll('input[name^="opt_"]:checked')).map((el) => el.value);
      if (opts.length) params.opts = opts.join(',');
      if (val('elo')) params.elo = val('elo');
      if (val('plycount')) params.plycount = val('plycount');
      if (val('cpu')) params.cpu = val('cpu');
      if (val('desc')) params.desc = val('desc');
      if (checked('overwrite')) params.overwrite = '1';
    } else if (task === 'merge') {
      params.dbDest = val('dbDest');
      const src = joinLines(val('dbSources')); if (src) params.dbSources = src;
      const pgn = joinLines(val('pgn')); if (pgn) params.pgn = pgn;
    } else if (task === 'export') {
      params.db = joinLines(val('db'));
      params.pgnOut = val('pgnOut');
      if (checked('overwrite')) params.overwrite = '1';
    } else if (task === 'dup') {
      params.db = joinLines(val('db'));
      if (checked('remove')) params.remove = '1';
      if (checked('confirm')) params.confirm = '1';
      if (checked('printall')) params.printall = '1';
      if (val('report')) params.report = val('report');
    } else if (task === 'bench') {
      params.db = val('db');
      if (val('cpu')) params.cpu = val('cpu');
    } else if (task === 'query') {
      params.db = val('db');
      params.pql = val('pql');
      if (val('printFormat')) params.printFormat = val('printFormat');
      if (val('report')) params.report = val('report');
    } else if (task === 'index') {
      params.db = joinLines(val('db'));
    } else if (task === 'material') {
      params.db = joinLines(val('db'));
    } else if (task === 'tree') {
      params.db = joinLines(val('db'));
      if (val('depth')) params.depth = val('depth');
    } else if (task === 'optimize') {
      params.db = joinLines(val('db'));
      const opts = [];
      if (checked('vacuum')) opts.push('vacuum');
      if (checked('integrity')) opts.push('integrity');
      if (opts.length) params.opts = opts.join(',');
    }
    return params;
  }

  // Best-effort client-side mirror of buildJobArgv() (src/jobs.cpp) purely
  // for a live "here's what will run" preview -- the server independently
  // validates and builds the real argv; this never runs anything itself.
  function previewCmdline(task, form) {
    const val = (name) => { const el = form.querySelector('[name="' + name + '"]'); return el ? el.value.trim() : ''; };
    const checked = (name) => { const el = form.querySelector('[name="' + name + '"]'); return !!(el && el.checked); };
    const q = (s) => (s.indexOf(' ') >= 0 ? '"' + s + '"' : s);
    const parts = ['ocgdb'];

    if (task === 'create') {
      parts.push('-create');
      splitLines(val('pgn')).forEach((p) => parts.push('-pgn', q(p)));
      parts.push('-db', q(val('db') || '<db>'));
      const opts = Array.from(form.querySelectorAll('input[name^="opt_"]:checked')).map((el) => el.value);
      if (opts.length) parts.push('-o', opts.join(','));
      if (val('elo')) parts.push('-elo', val('elo'));
      if (val('plycount')) parts.push('-plycount', val('plycount'));
      if (val('cpu')) parts.push('-cpu', val('cpu'));
      if (val('desc')) parts.push('-desc', q(val('desc')));
    } else if (task === 'merge') {
      parts.push('-merge', '-db', q(val('dbDest') || '<dbDest>'));
      splitLines(val('dbSources')).forEach((p) => parts.push('-db', q(p)));
      splitLines(val('pgn')).forEach((p) => parts.push('-pgn', q(p)));
    } else if (task === 'export') {
      parts.push('-export');
      splitLines(val('db')).forEach((p) => parts.push('-db', q(p)));
      parts.push('-pgn', q(val('pgnOut') || '<pgnOut>'));
    } else if (task === 'dup') {
      parts.push('-dup');
      splitLines(val('db')).forEach((p) => parts.push('-db', q(p)));
      const opts = [];
      if (checked('remove')) opts.push('remove');
      if (checked('printall')) opts.push('printall');
      if (opts.length) parts.push('-o', opts.join(','));
      if (val('report')) parts.push('-r', q(val('report')));
    } else if (task === 'bench') {
      parts.push('-bench', '-db', q(val('db') || '<db>'));
      if (val('cpu')) parts.push('-cpu', val('cpu'));
    } else if (task === 'query') {
      parts.push('-db', q(val('db') || '<db>'), '-q', q(val('pql') || '<pql>'));
      if (val('printFormat')) parts.push('-o', val('printFormat'));
      if (val('report')) parts.push('-r', q(val('report')));
    } else if (task === 'index') {
      parts.push('-index');
      splitLines(val('db')).forEach((p) => parts.push('-db', q(p)));
    } else if (task === 'material') {
      parts.push('-material');
      splitLines(val('db')).forEach((p) => parts.push('-db', q(p)));
    } else if (task === 'tree') {
      parts.push('-tree');
      splitLines(val('db')).forEach((p) => parts.push('-db', q(p)));
      if (val('depth')) parts.push('-depth', val('depth'));
    } else if (task === 'optimize') {
      parts.push('-optimize');
      splitLines(val('db')).forEach((p) => parts.push('-db', q(p)));
      const opts = [];
      if (checked('vacuum')) opts.push('vacuum');
      if (checked('integrity')) opts.push('integrity');
      if (opts.length) parts.push('-o', opts.join(','));
    }
    parts.push('-progress');
    return parts.join(' ');
  }

  // ---------------------------------------------------------------- jobs

  async function refreshJobs(panel) {
    const el = panel.querySelector('[data-role="jobs-table"]');
    if (!el) return;
    try {
      const data = await window.OcgdbApi.adminJobs();
      renderJobs(panel, el, data.jobs || []);
    } catch (err) {
      if (err.status === 401) { onUnauthorized(panel); return; }
      el.innerHTML = '<div class="panel-error">' + esc(localizeError(err.message)) + '</div>';
    }
  }

  function renderJobs(panel, el, jobs) {
    if (!jobs.length) {
      el.innerHTML = '<div class="panel-empty">' + t().t('admin.noJobs') + '</div>';
      return;
    }
    let html = '<div class="table-scroll"><table class="admin-jobs-table"><thead><tr>' +
      '<th>#</th><th>' + t().t('admin.colTask') + '</th><th>' + t().t('admin.colParams') + '</th>' +
      '<th>' + t().t('admin.colState') + '</th><th>' + t().t('admin.colProgress') + '</th><th></th>' +
      '</tr></thead><tbody>';
    jobs.forEach((j) => {
      html += '<tr class="admin-job-row" data-id="' + j.id + '">' +
        '<td>' + j.id + '</td>' +
        '<td>' + esc(j.task) + '</td>' +
        '<td class="admin-job-params">' + esc(j.paramsText) + '</td>' +
        '<td>' + stateChip(j.state) + '</td>' +
        '<td>' + progressCell(j) + '</td>' +
        '<td>' + (['queued', 'running'].includes(j.state)
          ? '<button type="button" class="btn btn-small" data-act="cancel" data-id="' + j.id + '">' + t().t('admin.cancel') + '</button>'
          : '') + '</td>' +
        '</tr>';
    });
    html += '</tbody></table></div>';
    el.innerHTML = html;

    el.querySelectorAll('.admin-job-row').forEach((tr) => {
      tr.addEventListener('click', (e) => {
        if (e.target.closest('[data-act="cancel"]')) return;
        openJobDetail(panel, Number(tr.dataset.id));
      });
    });
    el.querySelectorAll('[data-act="cancel"]').forEach((btn) => {
      btn.addEventListener('click', async (e) => {
        e.stopPropagation();
        btn.disabled = true;
        try {
          await window.OcgdbApi.adminCancelJob(btn.dataset.id);
          await refreshJobs(panel);
        } catch (err) {
          alert(localizeError(err.message));
          btn.disabled = false;
        }
      });
    });
  }

  function stateChip(state) {
    return '<span class="chip-state chip-state-' + state + '">' + t().t('admin.state.' + state) + '</span>';
  }

  function progressCell(j) {
    if (j.progressTotal > 0) {
      const pct = Math.min(100, Math.round((j.progress / j.progressTotal) * 100));
      return '<div class="admin-progress"><div class="admin-progress-bar" style="width:' + pct + '%"></div></div>' +
        '<span class="small muted">' + pct + '% &middot; ' + Number(j.gameCnt).toLocaleString() + ' ' + t().t('common.games') + '</span>';
    }
    if (j.gameCnt > 0) {
      return '<span class="small muted">' + Number(j.gameCnt).toLocaleString() + ' ' + t().t('common.games') + '</span>';
    }
    return j.state === 'running' ? '<span class="small muted">' + t().t('admin.working') + '&hellip;</span>' : '';
  }

  // ----------------------------------------------------------- job modal

  function openJobDetail(panel, id) {
    const container = document.createElement('div');
    container.className = 'admin-job-detail';
    container.innerHTML = '<div class="panel-loading">' + t().t('common.loading') + '</div>';
    window.OcgdbNav.openModal(container, { wide: true });

    let stream = null;

    function renderShell(job) {
      container.innerHTML =
        '<h2>' + t().t('admin.job') + ' #' + job.id + ' &mdash; ' + esc(job.task) + ' <span data-role="chip">' + stateChip(job.state) + '</span></h2>' +
        '<div class="admin-job-cmdline"><code>' + esc(job.cmdline) + '</code></div>' +
        '<div class="admin-warning" data-role="warning" hidden></div>' +
        '<div class="admin-job-log" data-role="log"></div>' +
        '<div class="admin-job-detail-actions" data-role="actions">' +
          (['queued', 'running'].includes(job.state)
            ? '<button type="button" class="btn" data-act="cancel">' + t().t('admin.cancel') + '</button>' : '') +
        '</div>';

      const cancelBtn = container.querySelector('[data-act="cancel"]');
      if (cancelBtn) {
        cancelBtn.addEventListener('click', async () => {
          cancelBtn.disabled = true;
          try { await window.OcgdbApi.adminCancelJob(id); } catch (err) { /* reflected on the next status event */ }
        });
      }
    }

    // The task/cmdline/id fields are immutable once a job exists, so one
    // plain fetch is enough for those; state/progress/log come from the
    // streamed connection below instead of being re-fetched alongside them.
    window.OcgdbApi.adminJob(id).then(({ job }) => {
      renderShell(job);

      stream = window.OcgdbApi.adminJobStream(id, 0, (evt) => {
        if (!document.body.contains(container)) return; // modal already closed
        if (evt.type === 'log') {
          const logEl = container.querySelector('[data-role="log"]');
          if (!logEl) return;
          const atBottom = logEl.scrollTop + logEl.clientHeight >= logEl.scrollHeight - 4;
          const line = document.createElement('div');
          line.textContent = evt.line;
          logEl.appendChild(line);
          if (atBottom) logEl.scrollTop = logEl.scrollHeight;
        } else if (evt.type === 'status') {
          const chipEl = container.querySelector('[data-role="chip"]');
          if (chipEl) chipEl.innerHTML = stateChip(evt.state);
          const warnEl = container.querySelector('[data-role="warning"]');
          if (warnEl) {
            if (evt.error) { warnEl.textContent = evt.error; warnEl.hidden = false; }
            else warnEl.hidden = true;
          }
          if (evt.final) {
            const actionsEl = container.querySelector('[data-role="actions"]');
            if (actionsEl) actionsEl.innerHTML = '';
            if (evt.state === 'succeeded' || evt.state === 'failed') refreshJobs(panel);
          }
        }
      });
    }).catch((err) => {
      container.innerHTML = '<div class="panel-error">' + esc(localizeError(err.message)) + '</div>';
    });

    const stopStream = () => { if (stream) stream.stop(); };
    // Piggyback on the existing modal close hook via app.js's onModalClose,
    // which isn't exported -- instead, stop the stream whenever the modal
    // backdrop is removed from the DOM.
    const observer = new MutationObserver(() => {
      if (!document.body.contains(container)) {
        stopStream();
        observer.disconnect();
      }
    });
    observer.observe(document.body, { childList: true, subtree: true });
  }

  window.OcgdbAdmin = { mount };
})();
