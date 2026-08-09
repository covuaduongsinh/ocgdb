// OCGDB web UI -- thin fetch() wrapper around the /api/* endpoints exposed
// by `ocgdb -server` (see src/server.cpp for the exact JSON shapes).
'use strict';

(function () {
  function qs(params) {
    const usp = new URLSearchParams();
    Object.keys(params || {}).forEach((k) => {
      const v = params[k];
      if (v === undefined || v === null || v === '') return;
      usp.set(k, v);
    });
    const s = usp.toString();
    return s ? '?' + s : '';
  }

  async function getJson(path, params) {
    const res = await fetch(path + qs(params), { headers: { Accept: 'application/json' } });
    let body = null;
    try {
      body = await res.json();
    } catch (e) {
      throw new ApiError('Invalid JSON response', res.status);
    }
    if (!res.ok && !body) {
      throw new ApiError('HTTP ' + res.status, res.status);
    }
    return { ok: res.ok, status: res.status, body };
  }

  class ApiError extends Error {
    constructor(message, status) {
      super(message);
      this.status = status;
    }
  }

  // ---------------------------------------------------------- admin (auth)
  // The admin token is kept in sessionStorage (not localStorage/a cookie):
  // it's meant to be pasted once per browser tab, not silently persisted
  // forever, and a cookie would need its own CSRF story on top of the
  // X-OCGDB-Token header the server already checks (see server.cpp).
  const ADMIN_TOKEN_KEY = 'ocgdbAdminToken';

  function getAdminToken() {
    try { return sessionStorage.getItem(ADMIN_TOKEN_KEY) || ''; } catch (e) { return ''; }
  }
  function setAdminToken(tok) {
    try {
      if (tok) sessionStorage.setItem(ADMIN_TOKEN_KEY, tok);
      else sessionStorage.removeItem(ADMIN_TOKEN_KEY);
    } catch (e) { /* private-browsing storage denial, etc -- best effort */ }
  }

  async function adminFetch(method, path, params) {
    const opts = { method, headers: { Accept: 'application/json', 'X-OCGDB-Token': getAdminToken() } };
    let url = path;
    if (method === 'GET') {
      url += qs(params);
    } else {
      const usp = new URLSearchParams();
      Object.keys(params || {}).forEach((k) => {
        const v = params[k];
        if (v === undefined || v === null) return;
        usp.set(k, v);
      });
      opts.headers['Content-Type'] = 'application/x-www-form-urlencoded';
      opts.body = usp.toString();
    }
    const res = await fetch(url, opts);
    let body = null;
    try { body = await res.json(); } catch (e) { /* empty/non-JSON body */ }

    if (res.status === 401) {
      document.dispatchEvent(new CustomEvent('ocgdb:admin-unauthorized'));
    }
    if (!res.ok) {
      throw new ApiError((body && body.error) || ('HTTP ' + res.status), res.status);
    }
    return body;
  }

  const API = {
    info() {
      return getJson('/api/info').then((r) => r.body);
    },
    games(params) {
      return getJson('/api/games', params).then((r) => r.body);
    },
    async game(id) {
      const r = await getJson('/api/game/' + encodeURIComponent(id));
      if (r.status === 404) return null;
      return r.body;
    },
    pgnUrl(id) {
      return '/api/pgn/' + encodeURIComponent(id);
    },
    players(q, limit) {
      return getJson('/api/players', { q, limit }).then((r) => r.body.items || []);
    },
    events(q, limit) {
      return getJson('/api/events', { q, limit }).then((r) => r.body.items || []);
    },
    sites(q, limit) {
      return getJson('/api/sites', { q, limit }).then((r) => r.body.items || []);
    },
    stats(refresh) {
      return getJson('/api/stats', refresh ? { refresh: 1 } : null).then((r) => r.body);
    },
    query(pql, limit) {
      return getJson('/api/query', { pql, limit }).then((r) => r.body);
    },
    tree(fen) {
      return getJson('/api/tree', { fen }).then((r) => r.body);
    },

    // ---- admin (control plane) -----------------------------------------
    hasAdminToken() { return !!getAdminToken(); },
    getAdminToken,
    setAdminToken,
    adminStatus() { return adminFetch('GET', '/api/admin/status'); },
    adminDatabases() { return adminFetch('GET', '/api/admin/databases'); },
    adminAddDatabase(path, label) { return adminFetch('POST', '/api/admin/databases/add', { path, label }); },
    adminRemoveDatabase(id) { return adminFetch('POST', '/api/admin/databases/remove', { id }); },
    adminScanDatabases(dir) { return adminFetch('POST', '/api/admin/databases/scan', { dir }); },
    adminActivateDatabase(id) { return adminFetch('POST', '/api/admin/databases/activate', { id }); },
    adminJobs(state) { return adminFetch('GET', '/api/admin/jobs', state ? { state } : null); },
    adminJob(id) { return adminFetch('GET', '/api/admin/jobs/' + encodeURIComponent(id)); },
    adminJobLog(id, fromSeq) { return adminFetch('GET', '/api/admin/jobs/' + encodeURIComponent(id) + '/log', { from: fromSeq || 0 }); },
    adminSubmitJob(params) { return adminFetch('POST', '/api/admin/jobs/submit', params); },
    adminCancelJob(id) { return adminFetch('POST', '/api/admin/jobs/' + encodeURIComponent(id) + '/cancel'); },
    adminClearJobs() { return adminFetch('POST', '/api/admin/jobs/clear'); },
  };

  window.OcgdbApi = API;
  window.ApiError = ApiError;
})();
