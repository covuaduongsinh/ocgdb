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
    adminJobResults(id, offset, limit) { return adminFetch('GET', '/api/admin/jobs/' + encodeURIComponent(id) + '/results', { offset: offset || 0, limit: limit || 200 }); },
    adminSubmitJob(params) { return adminFetch('POST', '/api/admin/jobs/submit', params); },
    adminCancelJob(id) { return adminFetch('POST', '/api/admin/jobs/' + encodeURIComponent(id) + '/cancel'); },
    adminClearJobs() { return adminFetch('POST', '/api/admin/jobs/clear'); },

    // Uploads `file` to POST /api/admin/upload (multipart/form-data) and
    // resolves to the server-side path the job form should submit as
    // "pgn". Uses XMLHttpRequest rather than fetch() specifically for
    // xhr.upload.onprogress -- fetch() has no equivalent for outgoing
    // (request body) progress, only incoming.
    adminUpload(file, onProgress) {
      return new Promise((resolve, reject) => {
        const xhr = new XMLHttpRequest();
        xhr.open('POST', '/api/admin/upload');
        xhr.setRequestHeader('X-OCGDB-Token', getAdminToken());
        if (onProgress) {
          xhr.upload.onprogress = (e) => { if (e.lengthComputable) onProgress(e.loaded, e.total); };
        }
        xhr.onload = () => {
          let body = null;
          try { body = JSON.parse(xhr.responseText); } catch (e) { /* fall through to status check */ }
          if (xhr.status === 401) document.dispatchEvent(new CustomEvent('ocgdb:admin-unauthorized'));
          if (xhr.status >= 200 && xhr.status < 300 && body) resolve(body);
          else reject(new ApiError((body && body.error) || ('HTTP ' + xhr.status), xhr.status));
        };
        xhr.onerror = () => reject(new ApiError('network error', 0));
        const fd = new FormData();
        fd.append('file', file, file.name);
        xhr.send(fd);
      });
    },

    // Reads GET /api/admin/jobs/:id/stream (newline-delimited JSON chunks;
    // see server.cpp) via fetch() + ReadableStream -- deliberately not
    // EventSource, which cannot set the X-OCGDB-Token header this server's
    // auth depends on. Calls onEvent(obj) for each parsed line as it
    // arrives. Returns an AbortController-like handle whose stop() ends the
    // read loop (call it when the UI showing this stream goes away).
    adminJobStream(id, fromSeq, onEvent) {
      const controller = new AbortController();
      (async () => {
        let res;
        try {
          res = await fetch('/api/admin/jobs/' + encodeURIComponent(id) + '/stream?from=' + (fromSeq || 0), {
            headers: { 'X-OCGDB-Token': getAdminToken() },
            signal: controller.signal,
          });
        } catch (e) {
          return; // aborted, or the connection never came up -- caller has no fallback here
        }
        if (res.status === 401) document.dispatchEvent(new CustomEvent('ocgdb:admin-unauthorized'));
        if (!res.ok || !res.body) return;

        const reader = res.body.getReader();
        const decoder = new TextDecoder();
        let buf = '';
        try {
          for (;;) {
            const { done, value } = await reader.read();
            if (done) break;
            buf += decoder.decode(value, { stream: true });
            let nl;
            while ((nl = buf.indexOf('\n')) >= 0) {
              const line = buf.slice(0, nl);
              buf = buf.slice(nl + 1);
              if (!line) continue;
              try { onEvent(JSON.parse(line)); } catch (e) { /* skip a malformed line */ }
            }
          }
        } catch (e) { /* aborted mid-read, or connection dropped -- nothing more to do */ }
      })();
      return { stop: () => controller.abort() };
    },

    adminEngines() { return adminFetch('GET', '/api/admin/engines'); },
    adminAddEngine(name, path, options) { return adminFetch('POST', '/api/admin/engines/add', { name, path, options }); },
    adminRemoveEngine(id) { return adminFetch('POST', '/api/admin/engines/remove', { id }); },

    // Streams POST /api/analyse the same way adminJobStream() streams the
    // job log/status endpoint -- NDJSON chunks over fetch() +
    // ReadableStream (not EventSource, same reasoning: it can't set
    // X-OCGDB-Token). One line per engine "info" update, then a final
    // {"type":"bestmove",...} line before the connection closes on its
    // own (the server ends the stream once the engine's search
    // finishes -- there's no separate "done" signal to look for here the
    // way adminJobStream's status events have "final").
    adminAnalyse(engineId, fen, opts, onEvent) {
      const controller = new AbortController();
      (async () => {
        const usp = new URLSearchParams();
        usp.set('engineId', engineId);
        usp.set('fen', fen || '');
        if (opts && opts.depth) usp.set('depth', opts.depth);
        if (opts && opts.movetimeMs) usp.set('movetimeMs', opts.movetimeMs);
        if (opts && opts.multipv) usp.set('multipv', opts.multipv);

        let res;
        try {
          res = await fetch('/api/analyse', {
            method: 'POST',
            headers: { 'X-OCGDB-Token': getAdminToken(), 'Content-Type': 'application/x-www-form-urlencoded' },
            body: usp.toString(),
            signal: controller.signal,
          });
        } catch (e) {
          return;
        }
        if (res.status === 401) document.dispatchEvent(new CustomEvent('ocgdb:admin-unauthorized'));
        if (!res.ok || !res.body) {
          let body = null;
          try { body = await res.json(); } catch (e) { /* ignore */ }
          onEvent({ type: 'error', error: (body && body.error) || ('HTTP ' + res.status) });
          return;
        }

        const reader = res.body.getReader();
        const decoder = new TextDecoder();
        let buf = '';
        try {
          for (;;) {
            const { done, value } = await reader.read();
            if (done) break;
            buf += decoder.decode(value, { stream: true });
            let nl;
            while ((nl = buf.indexOf('\n')) >= 0) {
              const line = buf.slice(0, nl);
              buf = buf.slice(nl + 1);
              if (!line) continue;
              try { onEvent(JSON.parse(line)); } catch (e) { /* skip a malformed line */ }
            }
          }
        } catch (e) { /* aborted mid-read, or connection dropped */ }
      })();
      return { stop: () => controller.abort() };
    },
  };

  window.OcgdbApi = API;
  window.ApiError = ApiError;
})();
