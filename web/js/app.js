// OCGDB web UI -- app shell: theme, language wiring, hash router, a small
// modal helper (used by the game viewer), and one shared /api/info fetch
// that every view can read from `App.info`.
'use strict';

(function () {
  const VIEWS = ['intro', 'browse', 'pql', 'stats', 'admin'];
  const DEFAULT_VIEW = 'intro';

  const App = {
    info: null,
    infoError: null,
  };

  // ---------------------------------------------------------------- theme
  function applyTheme(theme) {
    if (theme === 'light' || theme === 'dark') {
      document.documentElement.setAttribute('data-theme', theme);
    } else {
      document.documentElement.removeAttribute('data-theme');
    }
  }

  function currentTheme() {
    return localStorage.getItem('ocgdbTheme') || '';
  }

  function toggleTheme() {
    const cur = currentTheme();
    const prefersDark = window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches;
    const effectiveNow = cur || (prefersDark ? 'dark' : 'light');
    const next = effectiveNow === 'dark' ? 'light' : 'dark';
    localStorage.setItem('ocgdbTheme', next);
    applyTheme(next);
  }

  // --------------------------------------------------------------- modal
  let modalBackdrop = null;
  function openModal(contentEl, opts) {
    closeModal();
    modalBackdrop = document.createElement('div');
    modalBackdrop.className = 'modal-backdrop';
    const box = document.createElement('div');
    box.className = 'modal-box' + ((opts && opts.wide) ? ' modal-wide' : '');
    const closeBtn = document.createElement('button');
    closeBtn.type = 'button';
    closeBtn.className = 'modal-close';
    closeBtn.setAttribute('aria-label', window.I18N.t('common.close'));
    closeBtn.textContent = '×';
    closeBtn.addEventListener('click', closeModal);
    box.appendChild(closeBtn);
    box.appendChild(contentEl);
    modalBackdrop.appendChild(box);
    modalBackdrop.addEventListener('click', (e) => {
      if (e.target === modalBackdrop) closeModal();
    });
    document.body.appendChild(modalBackdrop);
    document.body.classList.add('modal-open');
    return box;
  }
  let modalCloseHandler = null;
  function onModalClose(fn) { modalCloseHandler = fn; }
  function closeModal() {
    if (modalCloseHandler) { try { modalCloseHandler(); } catch (e) { /* ignore */ } }
    modalCloseHandler = null;
    if (modalBackdrop) {
      modalBackdrop.remove();
      modalBackdrop = null;
      document.body.classList.remove('modal-open');
    }
  }

  function openGameViewer(gameId) {
    const container = document.createElement('div');
    const box = openModal(container, { wide: true });
    box.classList.add('modal-viewer');
    const viewer = new window.OcgdbViewer.GameViewer(container);
    viewer.load(gameId);
    onModalClose(() => viewer.unmount());
  }

  // -------------------------------------------------------------- router
  const viewInstances = {};

  function currentView() {
    const h = (location.hash || '').replace('#', '').split('?')[0];
    return VIEWS.includes(h) ? h : DEFAULT_VIEW;
  }

  function currentViewParams() {
    const h = location.hash || '';
    const q = h.indexOf('?');
    const params = {};
    if (q >= 0) {
      new URLSearchParams(h.slice(q + 1)).forEach((v, k) => { params[k] = v; });
    }
    return params;
  }

  function navigate(view, params) {
    let hash = '#' + view;
    if (params && Object.keys(params).length) {
      hash += '?' + new URLSearchParams(params).toString();
    }
    location.hash = hash;
  }

  function renderRoute() {
    closeModal();
    const view = currentView();
    document.querySelectorAll('.nav-tab').forEach((el) => {
      el.classList.toggle('active', el.dataset.view === view);
    });
    document.querySelectorAll('.view-panel').forEach((el) => {
      el.hidden = el.dataset.view !== view;
    });

    const mod = {
      intro: window.OcgdbIntro,
      browse: window.OcgdbBrowse,
      pql: window.OcgdbPql,
      stats: window.OcgdbStats,
      admin: window.OcgdbAdmin,
    }[view];
    if (mod && mod.mount) {
      const panel = document.querySelector('.view-panel[data-view="' + view + '"]');
      mod.mount(panel, currentViewParams());
    }
  }

  // ------------------------------------------------------------- startup
  async function loadInfo() {
    try {
      App.info = await window.OcgdbApi.info();
      App.infoError = null;
    } catch (e) {
      App.info = null;
      App.infoError = e;
    }
    document.dispatchEvent(new CustomEvent('ocgdb:info', { detail: { info: App.info, error: App.infoError } }));
    updateBanner();
  }

  function updateBanner() {
    const banner = document.getElementById('db-error-banner');
    if (banner) {
      if (App.infoError) {
        banner.hidden = false;
        banner.textContent = window.I18N.t('common.dbNotLoaded');
      } else {
        banner.hidden = true;
      }
    }

    const staleBanner = document.getElementById('db-stale-banner');
    if (staleBanner) {
      staleBanner.hidden = !(App.info && App.info.derivedStale);
    }
  }

  // A link to the admin view may carry `?token=...` (printed on server
  // startup, see server.cpp) so pasting the URL logs you straight in.
  // Consume it into sessionStorage and scrub it from the address bar right
  // away -- it shouldn't linger in history/bookmarks/screen-shares.
  function consumeTokenFromHash() {
    const h = location.hash || '';
    if (!h.startsWith('#admin')) return;
    const q = h.indexOf('?');
    if (q < 0) return;
    const params = new URLSearchParams(h.slice(q + 1));
    const token = params.get('token');
    if (!token) return;
    window.OcgdbApi.setAdminToken(token);
    params.delete('token');
    const rest = params.toString();
    const cleanHash = '#admin' + (rest ? '?' + rest : '');
    history.replaceState(null, '', location.pathname + location.search + cleanHash);
  }

  function init() {
    consumeTokenFromHash();
    applyTheme(currentTheme());
    window.I18N.applyI18n();

    document.getElementById('theme-toggle').addEventListener('click', toggleTheme);
    document.getElementById('lang-toggle').addEventListener('click', () => {
      window.I18N.setLang(window.I18N.lang() === 'vi' ? 'en' : 'vi');
    });
    document.addEventListener('ocgdb:langchange', () => {
      updateBanner();
      renderRoute();
    });

    document.querySelectorAll('.nav-tab').forEach((el) => {
      el.addEventListener('click', () => navigate(el.dataset.view));
    });

    document.addEventListener('keydown', (e) => {
      if (e.key === 'Escape') closeModal();
    });

    window.addEventListener('hashchange', renderRoute);

    loadInfo().then(renderRoute);
  }

  document.addEventListener('DOMContentLoaded', init);

  // Exposed so admin.js can refresh the shared /api/info snapshot (and
  // re-render whichever view is currently visible) right after switching
  // the active database -- otherwise the schema/columns every other view
  // reads from App.info would stay stale until a manual page reload.
  App.reload = () => loadInfo().then(renderRoute);

  // Same /api/info refresh but without re-mounting the current view --
  // used for silent background polling (e.g. the admin panel's job poll,
  // to flip the derivedStale banner off once a rebuild job finishes)
  // where re-rendering mid-poll would interrupt whatever the user is doing.
  App.refreshInfo = loadInfo;

  window.OcgdbApp = App;
  window.OcgdbNav = { navigate, openGameViewer, openModal, closeModal };
})();
