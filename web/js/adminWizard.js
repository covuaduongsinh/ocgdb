// OCGDB web UI -- Admin tab: 3-step first-run wizard (Admin UX Mục 3).
// Chains three existing admin APIs that would otherwise require the user to
// operate three separate forms by hand: adminUpload() -> adminSubmitJob()
// (task=create) -> adminAddDatabase()+adminActivateDatabase(). No new server
// routes -- this is purely a guided front-end flow over what admin.js's own
// upload field and task form already call.
'use strict';

(function () {
  const t = () => window.I18N;

  function esc(s) {
    return String(s == null ? '' : s).replace(/[&<>"']/g, (c) => ({
      '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
    }[c]));
  }

  // Directory part of a server-side path -- kept separator-agnostic since
  // paths returned by the server may use either '/' or '\\' depending on
  // how -root/the admin DB location were configured (see server.cpp's
  // /api/admin/upload handler, which builds paths with fs::path on
  // whichever OS the server runs on).
  function dirOf(p) {
    const i = Math.max(p.lastIndexOf('/'), p.lastIndexOf('\\'));
    return i >= 0 ? p.slice(0, i + 1) : '';
  }

  function suggestName(fileName) {
    const base = String(fileName || 'games').replace(/\.[^.]+$/, '');
    const cleaned = base.replace(/[^A-Za-z0-9_-]+/g, '_').replace(/^_+|_+$/g, '');
    return (cleaned || 'games').slice(0, 40);
  }

  function open() {
    const container = document.createElement('div');
    container.className = 'admin-wizard';
    const box = window.OcgdbNav.openModal(container);
    box.classList.add('modal-wizard');

    const state = { pgnPath: '', fileName: '', dbPath: '' };
    let stream = null;
    const stopStream = () => { if (stream) stream.stop(); };
    const observer = new MutationObserver(() => {
      if (!document.body.contains(container)) { stopStream(); observer.disconnect(); }
    });
    observer.observe(document.body, { childList: true, subtree: true });

    renderStep1();

    function stepHeader(n) {
      return '<div class="wizard-steps muted small">' +
        [1, 2, 3].map((i) => '<span class="wizard-step' + (i === n ? ' wizard-step-active' : '') + '">' + i + '</span>').join('&rarr;') +
        '</div>';
    }

    function renderStep1() {
      container.innerHTML =
        '<h2>' + t().t('admin.wizard.title') + '</h2>' +
        stepHeader(1) +
        '<h3>' + t().t('admin.wizard.step1Title') + '</h3>' +
        '<p class="muted small">' + t().t('admin.wizard.step1Desc') + '</p>' +
        '<div class="field field-wide admin-upload">' +
          '<label class="btn btn-primary admin-upload-btn">' + t().t('admin.wizard.chooseFile') +
            '<input type="file" data-role="pgn-upload" accept=".pgn,.txt" hidden>' +
          '</label>' +
          '<span class="muted small" data-role="upload-status"></span>' +
        '</div>' +
        '<div class="panel-error" data-role="wizard-error" hidden></div>';

      const input = container.querySelector('[data-role="pgn-upload"]');
      const statusEl = container.querySelector('[data-role="upload-status"]');
      const errEl = container.querySelector('[data-role="wizard-error"]');

      input.addEventListener('change', async () => {
        const file = (input.files || [])[0];
        input.value = '';
        if (!file) return;
        errEl.hidden = true;
        try {
          const res = await window.OcgdbApi.adminUpload(file, (loaded, total) => {
            statusEl.textContent = t().t('admin.uploading', { name: file.name, pct: Math.round((loaded / total) * 100) });
          });
          state.pgnPath = res.path;
          state.fileName = file.name;
          renderStep2();
        } catch (err) {
          errEl.textContent = window.OcgdbAdmin.localizeError(err.message);
          errEl.hidden = false;
        }
      });
    }

    function renderStep2() {
      const suggested = suggestName(state.fileName);
      container.innerHTML =
        '<h2>' + t().t('admin.wizard.title') + '</h2>' +
        stepHeader(2) +
        '<h3>' + t().t('admin.wizard.step2Title') + '</h3>' +
        '<p class="muted small">' + t().t('admin.wizard.step2Desc') + '</p>' +
        '<form class="wizard-name-form">' +
          '<label class="field field-wide">' + t().t('admin.wizard.dbNameLabel') +
            '<input type="text" name="name" value="' + esc(suggested) + '" autocomplete="off" required></label>' +
          '<div class="muted small" data-role="path-hint"></div>' +
          '<div class="panel-error" data-role="wizard-error" hidden></div>' +
          '<div class="field field-actions">' +
            '<button type="button" class="btn" data-act="back">' + t().t('admin.wizard.back') + '</button>' +
            '<button type="submit" class="btn btn-primary">' + t().t('admin.wizard.createBtn') + '</button>' +
          '</div>' +
        '</form>';

      const form = container.querySelector('.wizard-name-form');
      const nameInput = form.querySelector('[name="name"]');
      const hintEl = container.querySelector('[data-role="path-hint"]');
      const errEl = container.querySelector('[data-role="wizard-error"]');
      const dir = dirOf(state.pgnPath);

      const updateHint = () => {
        const name = suggestName(nameInput.value) || 'games';
        hintEl.textContent = t().t('admin.wizard.dbPathHint', { path: dir + name + '.ocgdb.db3' });
      };
      nameInput.addEventListener('input', updateHint);
      updateHint();

      container.querySelector('[data-act="back"]').addEventListener('click', renderStep1);

      form.addEventListener('submit', (e) => {
        e.preventDefault();
        const raw = nameInput.value.trim();
        if (!raw) {
          errEl.textContent = t().t('admin.wizard.nameRequired');
          errEl.hidden = false;
          return;
        }
        const clean = suggestName(raw);
        if (!clean) {
          errEl.textContent = t().t('admin.wizard.nameInvalid');
          errEl.hidden = false;
          return;
        }
        state.dbPath = dir + clean + '.ocgdb.db3';
        renderStep3();
      });
    }

    function renderStep3() {
      container.innerHTML =
        '<h2>' + t().t('admin.wizard.title') + '</h2>' +
        stepHeader(3) +
        '<h3>' + t().t('admin.wizard.step3Title') + '</h3>' +
        '<div class="admin-progress"><div class="admin-progress-bar" data-role="bar" style="width:0%"></div></div>' +
        '<div class="muted small" data-role="progress-text"></div>' +
        '<div class="admin-job-log" data-role="log"></div>' +
        '<div class="panel-error" data-role="wizard-error" hidden></div>';

      const barEl = container.querySelector('[data-role="bar"]');
      const progressText = container.querySelector('[data-role="progress-text"]');
      const logEl = container.querySelector('[data-role="log"]');
      const errEl = container.querySelector('[data-role="wizard-error"]');

      // moves2 (2-byte binary move encoding) isn't optional here the way it
      // is in the manual create form's checkbox list -- skip it and the
      // resulting database has games with no move data at all ("WARNING:
      // there is not any column for storing moves", jobs.cpp/builder.cpp),
      // silently useless for browsing/PQL/the board viewer. index is added
      // too so the guided flow lands on an already-fast database.
      window.OcgdbApi.adminSubmitJob({ task: 'create', pgn: state.pgnPath, db: state.dbPath, opts: 'moves2,index' })
        .then(({ id }) => {
          stream = window.OcgdbApi.adminJobStream(id, 0, (evt) => {
            if (!document.body.contains(container)) return;
            if (evt.type === 'log') {
              const line = document.createElement('div');
              line.textContent = evt.line;
              logEl.appendChild(line);
              logEl.scrollTop = logEl.scrollHeight;
            } else if (evt.type === 'status') {
              if (evt.progressTotal > 0) {
                const pct = Math.min(100, Math.round((evt.progress / evt.progressTotal) * 100));
                barEl.style.width = pct + '%';
              }
              progressText.textContent = Number(evt.gameCnt || 0).toLocaleString() + ' ' + t().t('common.games');
              if (evt.final) {
                if (evt.state === 'succeeded') {
                  finish();
                } else {
                  errEl.textContent = evt.error || t().t('admin.wizard.createFailed');
                  errEl.hidden = false;
                }
              }
            }
          });
        })
        .catch((err) => {
          errEl.textContent = window.OcgdbAdmin.localizeError(err.message);
          errEl.hidden = false;
        });

      async function finish() {
        progressText.textContent = t().t('admin.wizard.step3Done');
        try {
          const added = await window.OcgdbApi.adminAddDatabase(state.dbPath, suggestName(state.fileName));
          await window.OcgdbApi.adminActivateDatabase(added.id);
          await window.OcgdbApp.reload();
          window.OcgdbNav.closeModal();
          window.OcgdbNav.navigate('browse', {});
        } catch (err) {
          errEl.textContent = window.OcgdbAdmin.localizeError(err.message);
          errEl.hidden = false;
        }
      }
    }
  }

  window.OcgdbAdminWizard = { open };
})();
