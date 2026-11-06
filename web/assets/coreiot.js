/* CoreIoT (ThingsBoard) REST and WebSocket client */
(function (global) {
  'use strict';

  var store = {
    get: function (k) { try { return sessionStorage.getItem(k); } catch (e) { return null; } },
    set: function (k, v) { try { sessionStorage.setItem(k, v); } catch (e) { /* storage unavailable */ } },
    del: function (k) { try { sessionStorage.removeItem(k); } catch (e) { /* storage unavailable */ } }
  };

  function ApiError(status, message) {
    this.status = status;
    this.message = message;
  }
  ApiError.prototype = Object.create(Error.prototype);

  function tokenExpiry(jwt) {
    var parts = String(jwt || '').split('.');
    if (parts.length !== 3) return null;
    try {
      var json = atob(parts[1].replace(/-/g, '+').replace(/_/g, '/'));
      var exp = JSON.parse(json).exp;
      return typeof exp === 'number' ? exp * 1000 : null;
    } catch (e) {
      return null;
    }
  }

  var Api = {
    host: store.get('fg.host') || 'https://app.coreiot.io',
    token: store.get('fg.token'),
    refreshToken: store.get('fg.refresh'),
    user: null,
    onSessionEnd: null,
    refreshing: null,

    hasSession: function () { return !!this.token; },

    setHost: function (h) {
      this.host = String(h || '').trim().replace(/\/+$/, '');
      store.set('fg.host', this.host);
    },

    saveTokens: function (t) {
      this.token = t.token;
      this.refreshToken = t.refreshToken;
      store.set('fg.token', t.token);
      if (t.refreshToken) store.set('fg.refresh', t.refreshToken);
      else store.del('fg.refresh');
    },

    clear: function () {
      this.token = null;
      this.refreshToken = null;
      this.user = null;
      store.del('fg.token');
      store.del('fg.refresh');
    },

    login: function (host, username, password) {
      this.setHost(host);
      var self = this;
      return fetch(this.host + '/api/auth/login', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ username: username, password: password })
      }).then(function (r) {
        if (r.status === 401) throw new ApiError(401, 'Email or password is incorrect.');
        if (!r.ok) throw new ApiError(r.status, 'Sign-in failed (HTTP ' + r.status + ').');
        return r.json();
      }, function () {
        throw new ApiError(0, 'Cannot reach ' + self.host + '. Check the server address and your connection.');
      }).then(function (t) {
        self.saveTokens(t);
        return self.me();
      });
    },

    /* sign in with a JWT copied from the CoreIoT web UI (works for Google or GitHub accounts) */
    loginToken: function (host, token, refreshToken) {
      this.setHost(host);
      var jwt = String(token || '').trim().replace(/^Bearer\s+/i, '');
      var exp = tokenExpiry(jwt);
      if (exp === null) return Promise.reject(new ApiError(0, 'This does not look like a JWT token.'));
      if (exp <= Date.now()) return Promise.reject(new ApiError(0, 'This token has expired. Copy a new one from CoreIoT.'));
      this.saveTokens({ token: jwt, refreshToken: String(refreshToken || '').trim() || null });
      var self = this;
      return this.me().catch(function (e) {
        self.clear();
        if (e.status === 401) throw new ApiError(401, 'CoreIoT did not accept this token.');
        throw e;
      });
    },

    /* milliseconds since epoch when the current token stops working, or null */
    expiresAt: function () { return tokenExpiry(this.token); },

    refresh: function () {
      var self = this;
      if (!this.refreshToken) return Promise.reject(new ApiError(401, 'Session expired.'));
      if (!this.refreshing) {
        this.refreshing = fetch(this.host + '/api/auth/token', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ refreshToken: this.refreshToken })
        }).then(function (r) {
          if (!r.ok) throw new ApiError(401, 'Session expired.');
          return r.json();
        }).then(function (t) {
          self.saveTokens(t);
        }).finally(function () {
          self.refreshing = null;
        });
      }
      return this.refreshing;
    },

    request: function (method, path, body, retried) {
      var self = this;
      var opts = { method: method, headers: { 'X-Authorization': 'Bearer ' + this.token } };
      if (body !== undefined) {
        opts.headers['Content-Type'] = 'application/json';
        opts.body = JSON.stringify(body);
      }
      return fetch(this.host + path, opts).then(function (r) {
        if (r.status === 401 && !retried) {
          return self.refresh().then(function () {
            return self.request(method, path, body, true);
          }, function (err) {
            self.endSession();
            throw err;
          });
        }
        if (!r.ok) {
          return r.json().catch(function () { return {}; }).then(function (j) {
            throw new ApiError(r.status, j.message || ('HTTP ' + r.status));
          });
        }
        var type = r.headers.get('content-type') || '';
        return type.indexOf('json') >= 0 ? r.json() : r.text();
      }, function () {
        throw new ApiError(0, 'Network error. Check your connection.');
      });
    },

    endSession: function () {
      this.clear();
      if (this.onSessionEnd) this.onSessionEnd();
    },

    me: function () {
      var self = this;
      return this.request('GET', '/api/auth/user').then(function (u) {
        self.user = u;
        return u;
      });
    },

    devices: function () {
      var u = this.user || {};
      var path = u.authority === 'CUSTOMER_USER' && u.customerId
        ? '/api/customer/' + u.customerId.id + '/devices?pageSize=100&page=0&sortProperty=name&sortOrder=ASC'
        : '/api/tenant/devices?pageSize=100&page=0&sortProperty=name&sortOrder=ASC';
      return this.request('GET', path).then(function (p) { return p.data || []; });
    },

    /* latest value of each key: { key: { ts, value } } */
    latest: function (id, keys) {
      return this.request('GET', '/api/plugins/telemetry/DEVICE/' + id +
        '/values/timeseries?keys=' + encodeURIComponent(keys.join(','))).then(function (r) {
        var out = {};
        Object.keys(r || {}).forEach(function (k) {
          if (r[k] && r[k][0]) out[k] = r[k][0];
        });
        return out;
      });
    },

    /* time series: { key: [{ ts, value }] } ascending */
    series: function (id, keys, startTs, endTs, opt) {
      opt = opt || {};
      var q = 'keys=' + encodeURIComponent(keys.join(',')) +
        '&startTs=' + startTs + '&endTs=' + endTs +
        '&agg=' + (opt.agg || 'NONE') +
        '&limit=' + (opt.limit || 1000) +
        '&orderBy=' + (opt.orderBy || 'ASC') +
        (opt.interval ? '&interval=' + opt.interval : '');
      return this.request('GET', '/api/plugins/telemetry/DEVICE/' + id + '/values/timeseries?' + q);
    },

    /* attributes of a scope: { key: { value, lastUpdateTs } } */
    attributes: function (id, scope) {
      return this.request('GET', '/api/plugins/telemetry/DEVICE/' + id + '/values/attributes/' + scope)
        .then(function (list) {
          var out = {};
          (list || []).forEach(function (a) { out[a.key] = a; });
          return out;
        });
    },

    saveShared: function (id, values) {
      return this.request('POST', '/api/plugins/telemetry/DEVICE/' + id + '/attributes/SHARED_SCOPE', values);
    },

    rpc: function (id, method, params, timeoutMs) {
      return this.request('POST', '/api/plugins/rpc/twoway/' + id, {
        method: method,
        params: params || {},
        timeout: timeoutMs || 10000
      });
    }
  };

  /* Live telemetry over the ThingsBoard WebSocket API */
  function Live(deviceId, keys, handlers) {
    this.deviceId = deviceId;
    this.keys = keys;
    this.h = handlers || {};
    this.ws = null;
    this.closed = false;
    this.backoff = 1000;
    this.timer = null;
    this.open();
  }

  Live.prototype.open = function () {
    var self = this;
    var url = Api.host.replace(/^http/, 'ws') + '/api/ws';
    try {
      this.ws = new WebSocket(url);
    } catch (e) {
      this.retry();
      return;
    }
    this.ws.onopen = function () {
      self.backoff = 1000;
      self.ws.send(JSON.stringify({
        authCmd: { cmdId: 0, token: Api.token },
        cmds: [{
          type: 'TIMESERIES', entityType: 'DEVICE', entityId: self.deviceId,
          scope: 'LATEST_TELEMETRY', keys: self.keys.join(','), cmdId: 1
        }]
      }));
      if (self.h.status) self.h.status('live');
    };
    this.ws.onmessage = function (ev) {
      var msg;
      try { msg = JSON.parse(ev.data); } catch (e) { return; }
      if (msg.errorCode) {
        if (self.h.status) self.h.status('error', msg.errorMsg);
        return;
      }
      if (msg.data && self.h.data) self.h.data(msg.data);
    };
    this.ws.onclose = function (ev) {
      if (self.h.status) self.h.status('down');
      if (self.closed) return;
      /* token expiry closes the socket: refresh, then reconnect */
      if (ev.code === 1008 || ev.code === 4001) {
        Api.refresh().then(function () { self.retry(); }, function () { Api.endSession(); });
      } else {
        self.retry();
      }
    };
    this.ws.onerror = function () { /* onclose follows */ };
  };

  Live.prototype.retry = function () {
    var self = this;
    if (this.closed) return;
    clearTimeout(this.timer);
    this.timer = setTimeout(function () { self.open(); }, this.backoff);
    this.backoff = Math.min(this.backoff * 2, 30000);
  };

  Live.prototype.close = function () {
    this.closed = true;
    clearTimeout(this.timer);
    if (this.ws) {
      try { this.ws.close(); } catch (e) { /* already closed */ }
    }
  };

  global.CoreIoT = { Api: Api, Live: Live, ApiError: ApiError };
})(window);
