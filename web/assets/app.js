/* FireGuard-RT Monitor */
(function () {
  'use strict';

  var Api = CoreIoT.Api;
  var $ = function (s, r) { return (r || document).querySelector(s); };
  var $$ = function (s, r) { return Array.prototype.slice.call((r || document).querySelectorAll(s)); };

  /* Firmware vocabulary */
  var STATES = {
    INIT: 'Starting', WARMUP: 'Warming up', NORMAL: 'Normal', PRE_ALARM: 'Pre-alarm',
    ALARM_FIRE: 'Fire alarm', ALARM_GAS: 'Gas alarm', DRILL: 'Drill'
  };
  var STATE_IDS = ['INIT', 'WARMUP', 'NORMAL', 'PRE_ALARM', 'ALARM_FIRE', 'ALARM_GAS', 'DRILL'];
  var CAUSES = ['', 'Boot', 'Warm-up complete', 'Light surge', 'Gas rising', 'Heat rising',
    'Fire confirmed', 'Gas confirmed', 'Gas above danger level', 'Temperature above limit',
    'Not confirmed', 'Card override', 'Drill started', 'Drill ended', 'Restored after reset'];
  var SENSORS = [
    { id: 'light', name: 'Light sensor', part: 'LDR' },
    { id: 'gas', name: 'Gas sensor', part: 'MQ-2' },
    { id: 'dht22', name: 'Temperature and humidity', part: 'DHT22' },
    { id: 'ultrasonic', name: 'Presence sensor', part: 'HC-SR04' },
    { id: 'rfid', name: 'Card reader', part: 'RC522' }
  ];
  var FAULT_KIND = { 1: 'stopped responding', 2: 'implausible reading' };

  var TELEMETRY = ['state', 'light', 'gas', 'temp', 'hum', 'faults', 'trapped'];
  var EVENT_KEYS = ['event', 'detail', 'a', 'b', 'seq', 'boot', 'uptime_ms'];

  var CONFIG = [
    { group: 'Timing', key: 'sample_period_ms', label: 'Sample period', unit: 'ms', def: 1000, min: 1000, max: 2000, step: 100 },
    { group: 'Timing', key: 'callin_period_s', label: 'Call-in period', unit: 's', def: 60, min: 10, max: 600, step: 5 },
    { group: 'Timing', key: 'confirm_window_ms', label: 'Confirmation window', unit: 'ms', def: 4000, min: 1000, max: 4000, step: 100 },
    { group: 'Smoke and gas', key: 'th_gas_rise_mv', label: 'Gas rise', unit: 'mV', def: 400, min: 1, max: 3300 },
    { group: 'Smoke and gas', key: 'th_gas_warn_mv', label: 'Gas warning level', unit: 'mV', def: 1500, min: 1, max: 3300 },
    { group: 'Smoke and gas', key: 'th_gas_danger_mv', label: 'Gas danger level', unit: 'mV', def: 2500, min: 1, max: 3300 },
    { group: 'Flame and heat', key: 'th_light_delta_mv', label: 'Light surge', unit: 'mV', def: 800, min: 1, max: 3300 },
    { group: 'Flame and heat', key: 'th_temp_max_x10', label: 'Temperature limit', unit: '°C', def: 570, min: 1, max: 1250, scale: 10 },
    { group: 'Flame and heat', key: 'th_temp_rise_x10', label: 'Temperature rise in 10 s', unit: '°C', def: 30, min: 1, max: 500, scale: 10 }
  ];

  var ICONS = {
    sun: '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/></svg>',
    moon: '<svg viewBox="0 0 24 24"><path d="M20 14.5A8 8 0 1 1 9.5 4a6.5 6.5 0 0 0 10.5 10.5z"/></svg>',
    out: '<svg viewBox="0 0 24 24"><path d="M15 4h3a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2h-3M10 16l-4-4 4-4M6 12h10"/></svg>',
    power: '<svg viewBox="0 0 24 24"><path d="M12 3v8M6.3 7a8 8 0 1 0 11.4 0"/></svg>',
    fan: '<svg viewBox="0 0 24 24"><g class="spin"><circle cx="12" cy="12" r="1.6"/><path d="M12 10.4C11 6 13 3.5 15.5 4.5s1 5-3.5 5.9M13.6 12c4.4-1 6.9 1 5.9 3.5s-5 1-5.9-3.5M12 13.6c1 4.4-1 6.9-3.5 5.9s-1-5 3.5-5.9M10.4 12C6 13 3.5 11 4.5 8.5s5-1 5.9 3.5"/></g></svg>',
    bell: '<svg viewBox="0 0 24 24"><path d="M6 16V11a6 6 0 0 1 12 0v5l1.5 2h-15zM10 20.5a2 2 0 0 0 4 0"/></svg>',
    light: '<svg viewBox="0 0 24 24"><path d="M9 18h6M10 21h4M12 3a6 6 0 0 0-3.5 10.9c.6.5 1 1.2 1 2V16h5v-.1c0-.8.4-1.5 1-2A6 6 0 0 0 12 3z"/></svg>'
  };

  /* App state */
  var S = {
    devices: [],
    device: null,
    live: null,
    t: {},              /* latest telemetry { key: { ts, value } } */
    events: [],         /* recent events, newest first */
    shared: {},
    client: {},
    server: {},
    range: 3600000,
    filter: 'all',
    history: [],
    charts: {},
    wsState: 'down'
  };

  /* Utilities */
  function num(v) { var n = parseFloat(v); return isFinite(n) ? n : null; }
  function esc(s) {
    return String(s == null ? '' : s).replace(/[&<>"']/g, function (c) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
    });
  }
  function pad(n) { return n < 10 ? '0' + n : '' + n; }
  function fmtTime(ts) {
    var d = new Date(ts);
    return pad(d.getHours()) + ':' + pad(d.getMinutes()) + ':' + pad(d.getSeconds());
  }
  function fmtDateTime(ts) {
    var d = new Date(ts), now = new Date();
    var same = d.toDateString() === now.toDateString();
    return (same ? '' : d.toLocaleDateString(undefined, { month: 'short', day: 'numeric' }) + ' ') + fmtTime(ts);
  }
  function ago(ts) {
    if (!ts) return '–';
    var s = Math.max(0, Math.round((Date.now() - ts) / 1000));
    if (s < 60) return s + ' s ago';
    if (s < 3600) return Math.floor(s / 60) + ' min ago';
    if (s < 86400) return Math.floor(s / 3600) + ' h ago';
    return Math.floor(s / 86400) + ' d ago';
  }
  function val(k) { return S.t[k] ? S.t[k].value : undefined; }
  function cfg(key) {
    var c = CONFIG.filter(function (f) { return f.key === key; })[0];
    var v = S.shared[key] ? num(S.shared[key].value) : null;
    return v == null ? c.def : v;
  }

  var toastTimer;
  function toast(msg, isError) {
    var el = $('#toast');
    el.textContent = msg;
    el.classList.toggle('is-error', !!isError);
    el.hidden = false;
    clearTimeout(toastTimer);
    toastTimer = setTimeout(function () { el.hidden = true; }, 4000);
  }

  function busy(btn, on) {
    btn.classList.toggle('is-busy', on);
    btn.disabled = on;
  }

  /* Theme */
  function currentTheme() {
    var t = document.documentElement.dataset.theme;
    if (t) return t;
    return window.matchMedia && matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
  }
  function paintThemeButton() {
    var dark = currentTheme() === 'dark';
    var b = $('#theme-toggle');
    b.innerHTML = dark ? ICONS.sun : ICONS.moon;
    b.setAttribute('aria-label', dark ? 'Switch to light theme' : 'Switch to dark theme');
  }
  function toggleTheme() {
    var next = currentTheme() === 'dark' ? 'light' : 'dark';
    document.documentElement.dataset.theme = next;
    try { localStorage.setItem('fg.theme', next); } catch (e) { /* storage unavailable */ }
    paintThemeButton();
    restyleCharts();
  }

  /* Sign in */
  function showLogin(message) {
    stopLive();
    $('#app-view').hidden = true;
    $('#login-view').hidden = false;
    $('#login-host').value = Api.host;
    var err = $('#login-error');
    err.hidden = !message;
    err.textContent = message || '';
    setLoginMode(loginMode());
    syncTokenLink();
  }

  function loginMode() {
    var m = null;
    try { m = localStorage.getItem('fg.login'); } catch (e) { /* storage unavailable */ }
    return m === 'token' ? 'token' : 'password';
  }

  function setLoginMode(mode) {
    try { localStorage.setItem('fg.login', mode); } catch (e) { /* storage unavailable */ }
    $$('#login-mode [data-mode]').forEach(function (b) { b.setAttribute('aria-checked', String(b.dataset.mode === mode)); });
    $$('.login__group').forEach(function (g) { g.hidden = g.dataset.mode !== mode; });
    $('#login-user').required = $('#login-pass').required = mode === 'password';
    var first = mode === 'token' ? $('#login-token') : ($('#login-user').value ? $('#login-pass') : $('#login-user'));
    setTimeout(function () { first.focus(); }, 0);
  }

  function syncTokenLink() {
    var h = $('#login-host').value.trim().replace(/\/+$/, '') || 'https://app.coreiot.io';
    $('#token-link').href = h + '/account/security';
  }

  function onLogin(ev) {
    ev.preventDefault();
    var mode = loginMode();
    var host = $('#login-host'), user = $('#login-user'), pass = $('#login-pass'), token = $('#login-token');
    var err = $('#login-error');
    var fields = mode === 'token' ? [host, token] : [host, user, pass];
    var bad = fields.filter(function (i) { return !i.value.trim(); });
    [host, user, pass, token].forEach(function (i) { i.setAttribute('aria-invalid', bad.indexOf(i) >= 0 ? 'true' : 'false'); });
    if (bad.length) {
      err.textContent = mode === 'token' ? 'Fill in the server and paste a JWT token.' : 'Fill in the server, email and password.';
      err.hidden = false;
      bad[0].focus();
      return;
    }
    err.hidden = true;
    var btn = $('#login-submit');
    busy(btn, true);
    btn.querySelector('.btn__text').textContent = 'Signing in';
    var attempt = mode === 'token'
      ? Api.loginToken(host.value, token.value, $('#login-refresh').value)
      : Api.login(host.value, user.value.trim(), pass.value);
    attempt.then(function () {
      pass.value = '';
      token.value = '';
      $('#login-refresh').value = '';
      startApp();
    }, function (e) {
      err.textContent = e.message || 'Sign-in failed.';
      err.hidden = false;
      (mode === 'token' ? token : pass).select();
    }).finally(function () {
      busy(btn, false);
      btn.querySelector('.btn__text').textContent = 'Sign in';
    });
  }

  function logout() {
    clearTimeout(expiryTimer);
    Api.clear();
    S.device = null;
    showLogin();
  }

  /* App start */
  var expiryTimer;
  function watchExpiry() {
    clearTimeout(expiryTimer);
    if (Api.refreshToken) return;
    var exp = Api.expiresAt();
    if (!exp) return;
    var warnIn = exp - Date.now() - 5 * 60000;
    expiryTimer = setTimeout(function () {
      toast('Your token expires in a few minutes. Copy a new one from CoreIoT to stay signed in.', true);
      expiryTimer = setTimeout(function () { Api.endSession(); }, Math.max(0, exp - Date.now()));
    }, Math.max(0, warnIn));
  }

  function startApp() {
    watchExpiry();
    $('#login-view').hidden = true;
    $('#app-view').hidden = false;
    var ready = Api.user ? Promise.resolve(Api.user) : Api.me();
    ready.then(function () { return Api.devices(); }).then(function (list) {
      S.devices = list;
      var sel = $('#device-select');
      if (!list.length) {
        sel.innerHTML = '<option>No devices</option>';
        sel.disabled = true;
        toast('This account has no devices yet.', true);
        return;
      }
      sel.disabled = false;
      sel.innerHTML = list.map(function (d) {
        return '<option value="' + esc(d.id.id) + '">' + esc(d.label || d.name) + '</option>';
      }).join('');
      var saved = null;
      try { saved = localStorage.getItem('fg.device'); } catch (e) { /* storage unavailable */ }
      var pick = list.filter(function (d) { return d.id.id === saved; })[0] ||
        list.filter(function (d) { return /fireguard/i.test(d.name); })[0] || list[0];
      sel.value = pick.id.id;
      selectDevice(pick.id.id);
    }).catch(function (e) {
      if (e.status === 401) return;
      toast(e.message, true);
    });
    route();
  }

  function selectDevice(id) {
    S.device = S.devices.filter(function (d) { return d.id.id === id; })[0];
    try { localStorage.setItem('fg.device', id); } catch (e) { /* storage unavailable */ }
    S.t = {};
    S.events = [];
    S.shared = {};
    S.client = {};
    S.server = {};
    renderOverview();
    stopLive();

    Promise.all([
      Api.latest(id, TELEMETRY).then(function (t) { S.t = t; }),
      loadAttributes(),
      Api.series(id, EVENT_KEYS, Date.now() - 7 * 86400000, Date.now(), { limit: 40, orderBy: 'DESC' })
        .then(function (r) { S.events = joinEvents(r).reverse(); })
    ]).catch(function (e) { toast(e.message, true); }).then(function () {
      renderOverview();
      if (currentRoute() === 'settings') renderSettings();
      if (currentRoute() === 'history') loadHistory();
    });

    S.live = new CoreIoT.Live(id, TELEMETRY.concat(EVENT_KEYS), {
      status: function (st) { S.wsState = st; paintConn(); },
      data: onLiveData
    });
  }

  function stopLive() {
    if (S.live) S.live.close();
    S.live = null;
    S.wsState = 'down';
  }

  function loadAttributes() {
    var id = S.device.id.id;
    return Promise.all([
      Api.attributes(id, 'SHARED_SCOPE').then(function (a) { S.shared = a; }),
      Api.attributes(id, 'CLIENT_SCOPE').then(function (a) { S.client = a; }),
      Api.attributes(id, 'SERVER_SCOPE').then(function (a) { S.server = a; })
    ]);
  }

  function onLiveData(data) {
    var ev = null;
    Object.keys(data).forEach(function (k) {
      var p = data[k] && data[k][0];
      if (!p) return;
      var point = { ts: p[0], value: p[1] };
      if (EVENT_KEYS.indexOf(k) >= 0) {
        ev = ev || { ts: point.ts };
        ev[k] = point.value;
      } else if (!S.t[k] || S.t[k].ts <= point.ts) {
        S.t[k] = point;
      }
    });
    if (ev && ev.event) {
      var e = normEvent(ev);
      var dup = S.events.some(function (x) { return x.ts === e.ts && x.seq === e.seq; });
      if (!dup) {
        S.events.unshift(e);
        S.events.length = Math.min(S.events.length, 40);
        if (currentRoute() === 'history' && Date.now() - e.ts <= S.range) {
          S.history.unshift(e);
          renderEvents();
        }
      }
    }
    renderOverview();
  }

  /* Events */
  function normEvent(o) {
    return {
      ts: o.ts, type: String(o.event || ''), detail: o.detail == null ? '' : String(o.detail),
      a: num(o.a) || 0, b: num(o.b) || 0, seq: num(o.seq), boot: num(o.boot)
    };
  }

  /* timeseries per key -> list of events joined by timestamp, ascending */
  function joinEvents(r) {
    var by = {};
    EVENT_KEYS.forEach(function (k) {
      (r[k] || []).forEach(function (p) {
        (by[p.ts] = by[p.ts] || { ts: p.ts })[k] = p.value;
      });
    });
    return Object.keys(by).map(function (ts) { return by[ts]; })
      .filter(function (o) { return o.event; })
      .map(normEvent)
      .sort(function (x, y) { return x.ts - y.ts || (x.seq || 0) - (y.seq || 0); });
  }

  function describe(e) {
    var s;
    switch (e.type) {
      case 'state':
        s = STATES[e.detail] || e.detail;
        return { kind: 'State', text: s + (CAUSES[e.b] ? ' · ' + CAUSES[e.b] : ''),
          tone: /ALARM_/.test(e.detail) ? 'alarm' : e.detail === 'PRE_ALARM' ? 'warn' : '' };
      case 'fault':
        var sensor = SENSORS.filter(function (x) { return x.id === e.detail; })[0];
        var active = (e.b & 0x80) !== 0;
        var name = sensor ? sensor.name : e.detail;
        return { kind: 'Fault', text: name + (active ? ': ' + (FAULT_KIND[e.b & 0x7f] || 'fault') : ' recovered'),
          tone: active ? 'warn' : '' };
      case 'card':
        return { kind: 'Card', text: (e.a ? 'Authorized card ' : 'Rejected card ') + e.detail,
          tone: e.a ? '' : 'warn' };
      case 'trapped':
        return { kind: 'Presence', text: e.a ? 'Person detected in the danger zone' : 'Danger zone clear',
          tone: e.a ? 'alarm' : '' };
      case 'boot':
        return { kind: 'Boot', text: e.a ? 'Restarted by the watchdog' : 'Device started', tone: e.a ? 'warn' : '' };
      default:
        return { kind: e.type, text: e.detail, tone: '' };
    }
  }

  /* Overview */
  function paintConn() {
    var dot = $('#conn-dot');
    var last = S.t.state ? S.t.state.ts : 0;
    var limit = cfg('callin_period_s') * 2000 + 15000;
    var active = S.server.active ? String(S.server.active.value) === 'true' : null;
    var st = 'down';
    if (last && Date.now() - last <= limit && active !== false) st = 'live';
    else if (last) st = 'stale';
    dot.dataset.conn = st;
    dot.title = st === 'live' ? 'Reporting' : st === 'stale' ? 'No recent report' : 'No data';
  }

  function lastStateEvent() {
    for (var i = 0; i < S.events.length; i++) if (S.events[i].type === 'state') return S.events[i];
    return null;
  }

  function fanPct(state) {
    if (state === 'ALARM_FIRE') return 100;
    if (state === 'DRILL') return 50;
    if (state !== 'ALARM_GAS') return 0;
    var gas = num(val('gas')), warn = cfg('th_gas_warn_mv'), danger = cfg('th_gas_danger_mv');
    if (gas == null || danger <= warn) return 100;
    return Math.round(Math.min(100, Math.max(60, 60 + (gas - warn) * 40 / (danger - warn))));
  }

  function renderOverview() {
    var state = val('state');
    var known = state && STATES[state];
    var alarm = state === 'ALARM_FIRE' || state === 'ALARM_GAS';
    var faults = num(val('faults')) || 0;
    var trapped = String(val('trapped')) === 'true';
    document.body.classList.toggle('is-alarm', !!alarm);

    var dial = $('#dial');
    dial.dataset.state = known ? state : 'UNKNOWN';
    dial.classList.toggle('is-alarm', !!alarm);
    var frac = !known ? 0 : state === 'INIT' ? 0.1 : state === 'WARMUP' ? 0.4 : 1;
    $('.dial__arc').style.strokeDashoffset = String(578 * (1 - frac));
    $('#dial-state').textContent = known ? STATES[state] : (S.device ? 'No data' : 'Loading');
    var se = lastStateEvent();
    $('#dial-cause').textContent = se && se.detail === state && CAUSES[se.b] ? CAUSES[se.b] : '';
    $('#meta-since').textContent = se && se.detail === state ? fmtDateTime(se.ts) : '–';
    $('#meta-last').textContent = S.t.state ? ago(S.t.state.ts) : '–';
    $('#meta-device').textContent = S.device ? (S.device.label || S.device.name) : '–';
    $('#meta-device').title = $('#meta-device').textContent;
    $('#trapped').hidden = !trapped;
    document.title = (alarm ? STATES[state] + ' · ' : '') + 'FireGuard-RT Monitor';

    renderGauges(faults);
    renderSwitches(state, faults, trapped);
    renderHealth(faults);
    renderRecent();
    paintConn();
    var ts = Math.max.apply(null, ['light', 'gas', 'temp', 'hum'].map(function (k) { return S.t[k] ? S.t[k].ts : 0; }));
    $('#readings-age').textContent = ts ? 'updated ' + ago(ts) : '';
  }

  function gaugeHtml(g) {
    var ticks = (g.ticks || []).map(function (t) {
      return '<span class="gauge__tick" style="left:' + Math.min(100, t / g.max * 100).toFixed(1) + '%" title="' + esc(t + ' ' + g.unit) + '"></span>';
    }).join('');
    var v = g.value;
    var pct = v == null ? 0 : Math.max(0, Math.min(100, v / g.max * 100));
    return '<li class="gauge' + (v == null && !g.fault ? ' is-loading' : '') + '" data-level="' + g.level +
      '" data-fault="' + !!g.fault + '">' +
      '<span class="gauge__name"><span>' + g.name + '</span><span>' + g.part + '</span></span>' +
      '<span class="gauge__value">' + (v == null ? '–' : g.fmt(v)) + '<span class="gauge__unit">' + g.unit + '</span></span>' +
      '<span class="gauge__bar"><span class="gauge__fill" style="transform:scaleX(' + (pct / 100).toFixed(3) + ')"></span>' + ticks + '</span>' +
      '<span class="gauge__note">' + esc(g.fault ? 'Sensor fault' : g.note || '') + '</span></li>';
  }

  function renderGauges(faults) {
    var gas = num(val('gas')), light = num(val('light')), temp = num(val('temp')), hum = num(val('hum'));
    var warn = cfg('th_gas_warn_mv'), danger = cfg('th_gas_danger_mv'), tmax = cfg('th_temp_max_x10') / 10;
    var int0 = function (v) { return Math.round(v).toString(); };
    var one = function (v) { return v.toFixed(1); };
    var list = [
      { name: 'Gas', part: 'MQ-2', unit: 'mV', value: gas, max: 3300, ticks: [warn, danger], fmt: int0, fault: faults & 2,
        level: gas == null ? '' : gas >= danger ? 'alarm' : gas >= warn ? 'warn' : '',
        note: gas == null ? '' : gas >= danger ? 'Above danger level' : gas >= warn ? 'Above warning level' : 'Below warning level' },
      { name: 'Temperature', part: 'DHT22', unit: '°C', value: temp, max: 80, ticks: [tmax], fmt: one, fault: faults & 4,
        level: temp == null ? '' : temp >= tmax ? 'alarm' : temp >= tmax - 10 ? 'warn' : '',
        note: 'Limit ' + tmax.toFixed(1) + ' °C' },
      { name: 'Light', part: 'LDR', unit: 'mV', value: light, max: 3300, fmt: int0, fault: faults & 1, level: '',
        note: 'Surge at +' + cfg('th_light_delta_mv') + ' mV' },
      { name: 'Humidity', part: 'DHT22', unit: '%', value: hum, max: 100, fmt: one, fault: faults & 4, level: '', note: 'Relative humidity' }
    ];
    $('#gauges').innerHTML = list.map(gaugeHtml).join('');
  }

  function renderSwitches(state, faults, trapped) {
    var alarm = state === 'ALARM_FIRE' || state === 'ALARM_GAS';
    var fan = fanPct(state);
    var siren = state === 'ALARM_FIRE' ? 'Continuous' : state === 'ALARM_GAS' ? 'Pulsed 1 Hz'
      : state === 'DRILL' ? 'Drill tone' : state === 'PRE_ALARM' ? 'Chirp 5 s'
      : state === 'NORMAL' && faults ? 'Fault chirp' : 'Off';
    var led = alarm ? (trapped ? 'Red / green fast' : 'Red / green') : state === 'PRE_ALARM' ? 'Amber blink'
      : state === 'DRILL' ? 'Violet blink' : state === 'NORMAL' ? (faults ? 'Yellow blink' : 'Green')
      : state ? 'Blue blink' : '–';
    var rows = [
      { icon: ICONS.power, name: 'Mains power', value: !state ? '–' : alarm ? 'Cut' : 'On', on: alarm },
      { icon: ICONS.fan, name: 'Smoke fan', value: !state ? '–' : fan ? fan + ' %' : 'Off', on: fan > 0 },
      { icon: ICONS.bell, name: 'Siren', value: !state ? '–' : siren, on: siren !== 'Off' },
      { icon: ICONS.light, name: 'Status light', value: led, on: alarm || state === 'DRILL' || state === 'PRE_ALARM' }
    ];
    $('#switches').innerHTML = rows.map(function (r) {
      return '<li class="switch" data-on="' + r.on + '"><span class="switch__icon" aria-hidden="true">' + r.icon +
        '</span><span class="switch__name">' + r.name + '</span><span class="switch__value">' + esc(r.value) + '</span></li>';
    }).join('');
  }

  function renderHealth(faults) {
    var has = S.t.faults != null;
    $('#health').innerHTML = SENSORS.map(function (s, i) {
      var ok = !has ? 'unknown' : (faults & (1 << i)) ? 'false' : 'true';
      return '<li class="health__item" data-ok="' + ok + '"><span class="health__dot" aria-hidden="true"></span>' +
        '<span>' + s.name + '</span><span class="health__state">' +
        (ok === 'unknown' ? 'No data' : ok === 'true' ? 'OK' : 'Fault') + '</span></li>';
    }).join('');
  }

  function renderRecent() {
    var list = S.events.slice(0, 8);
    $('#recent').innerHTML = list.length ? list.map(function (e) {
      var d = describe(e);
      return '<li class="tl" data-tone="' + d.tone + '"><span class="tl__time">' + fmtDateTime(e.ts) + '</span>' +
        '<span class="tl__text">' + esc(d.text) + '</span></li>';
    }).join('') : '<li class="empty">No events in the last 7 days.</li>';
  }

  /* History */
  function bucket(range) {
    return range <= 3600000 ? 60000 : range <= 86400000 ? 900000 : 7200000;
  }

  function loadHistory() {
    if (!S.device) return;
    var id = S.device.id.id, end = Date.now(), start = end - S.range;
    $$('.chart__well').forEach(function (w) { w.classList.add('is-loading'); });
    $('#events-body').innerHTML = '<tr><td colspan="5" class="empty">Loading events</td></tr>';
    Api.series(id, ['light', 'gas', 'temp', 'hum'], start, end,
      { agg: 'AVG', interval: bucket(S.range), limit: 5000 }).then(function (r) {
      drawCharts(r, start, end);
    }, function (e) { toast(e.message, true); }).then(function () {
      $$('.chart__well').forEach(function (w) { w.classList.remove('is-loading'); });
    });
    Api.series(id, EVENT_KEYS, start, end, { limit: 5000, orderBy: 'DESC' }).then(function (r) {
      S.history = joinEvents(r).reverse();
      renderEvents();
    }, function (e) {
      $('#events-body').innerHTML = '<tr><td colspan="5" class="empty">' + esc(e.message) + '</td></tr>';
    });
  }

  function renderEvents() {
    var rows = S.history.filter(function (e) { return S.filter === 'all' || e.type === S.filter; });
    $('#events-body').innerHTML = rows.length ? rows.map(function (e) {
      var d = describe(e);
      return '<tr data-tone="' + d.tone + '"><td>' + fmtDateTime(e.ts) + '</td><td><span class="tag">' + esc(d.kind) +
        '</span></td><td>' + esc(d.text) + '</td><td class="num">' + (e.boot == null ? '' : e.boot) +
        '</td><td class="num">' + (e.seq == null ? '' : e.seq) + '</td></tr>';
    }).join('') : '<tr><td colspan="5" class="empty">No events in this range.</td></tr>';
  }

  function css(name) { return getComputedStyle(document.documentElement).getPropertyValue(name).trim(); }

  function chartOptions(start, end, yLeft, yRight) {
    var ink = css('--ink-3'), line = css('--line');
    var unit = S.range <= 3600000 ? 'minute' : S.range <= 86400000 ? 'hour' : 'day';
    return {
      responsive: true, maintainAspectRatio: false, animation: { duration: 300 },
      interaction: { mode: 'index', intersect: false },
      plugins: {
        legend: { position: 'bottom', labels: { color: ink, boxWidth: 10, boxHeight: 10, usePointStyle: true, font: { size: 12 } } },
        tooltip: { backgroundColor: css('--surface'), titleColor: css('--ink'), bodyColor: css('--ink-2'),
          borderColor: line, borderWidth: 1, padding: 10, bodyFont: { family: css('--mono') }, 
          callbacks: { title: function (it) { return it.length ? fmtDateTime(it[0].parsed.x) : ''; } } }
      },
      scales: {
        x: { type: 'linear', min: start, max: end, grid: { color: line }, border: { display: false },
          ticks: { color: ink, maxTicksLimit: 6, font: { family: css('--mono'), size: 11 },
            callback: function (v) {
              var d = new Date(v);
              return unit === 'day' ? d.toLocaleDateString(undefined, { month: 'short', day: 'numeric' })
                : pad(d.getHours()) + ':' + pad(d.getMinutes());
            } } },
        y: { position: 'left', title: { display: true, text: yLeft, color: ink, font: { size: 11 } }, grid: { color: line },
          border: { display: false }, ticks: { color: ink, font: { family: css('--mono'), size: 11 } } },
        y1: { position: 'right', display: !!yRight, title: { display: !!yRight, text: yRight || '', color: ink, font: { size: 11 } },
          grid: { display: false }, border: { display: false }, ticks: { color: ink, font: { family: css('--mono'), size: 11 } } }
      }
    };
  }

  function pts(arr) {
    return (arr || []).map(function (p) { return { x: p.ts, y: num(p.value) }; })
      .sort(function (a, b) { return a.x - b.x; });
  }

  function ds(label, data, color, axis, dashed) {
    return { label: label, data: data, borderColor: color, backgroundColor: color, yAxisID: axis || 'y',
      borderWidth: 1.8, pointRadius: 0, pointHoverRadius: 3, tension: 0.25, spanGaps: false,
      borderDash: dashed ? [4, 4] : undefined };
  }

  function drawCharts(r, start, end) {
    if (typeof Chart !== 'undefined') Chart.defaults.font.family = css('--font');
    if (typeof Chart === 'undefined') {
      $$('.chart__well').forEach(function (w) { w.innerHTML = '<p class="empty">Charts could not be loaded.</p>'; });
      return;
    }
    var inkC = css('--ink'), mute = css('--ink-3'), alarm = css('--alarm');
    var specs = {
      air: { el: '#chart-air', left: 'mV', sets: [ds('Gas', pts(r.gas), alarm), ds('Light', pts(r.light), mute, 'y', true)] },
      climate: { el: '#chart-climate', left: '°C', right: '%RH',
        sets: [ds('Temperature', pts(r.temp), inkC), ds('Humidity', pts(r.hum), mute, 'y1', true)] }
    };
    Object.keys(specs).forEach(function (k) {
      var sp = specs[k];
      var empty = sp.sets.every(function (s) { return !s.data.length; });
      var well = $(sp.el).parentNode;
      var note = well.querySelector('.empty');
      if (note) note.remove();
      if (empty) well.insertAdjacentHTML('beforeend', '<p class="empty">No readings in this range.</p>');
      if (S.charts[k]) S.charts[k].destroy();
      S.charts[k] = new Chart($(sp.el), { type: 'line', data: { datasets: sp.sets }, options: chartOptions(start, end, sp.left, sp.right) });
    });
  }

  function restyleCharts() {
    if (currentRoute() === 'history' && S.device) loadHistory();
  }

  /* Settings */
  function renderSettings() {
    var groups = {};
    CONFIG.forEach(function (c) { (groups[c.group] = groups[c.group] || []).push(c); });
    $('#config-fields').innerHTML = Object.keys(groups).map(function (g) {
      return '<fieldset class="cgroup"><legend class="cgroup__title">' + g + '</legend><div class="cgroup__grid">' +
        groups[g].map(function (c) {
          var v = cfg(c.key) / (c.scale || 1);
          var ack = S.client['cfg_' + c.key];
          var ackText = ack ? (ack.value === 'applied' ? 'Applied ' : 'Rejected ') + ago(ack.lastUpdateTs) : '';
          var step = c.scale ? 0.1 : (c.step || 1);
          return '<div class="cfield" data-key="' + c.key + '">' +
            '<label class="cfield__top" for="cf-' + c.key + '"><span>' + c.label + '</span>' +
            '<span class="cfield__ack" data-ack="' + (ack ? esc(ack.value) : '') + '">' + ackText + '</span></label>' +
            '<div class="cfield__input"><input class="well" id="cf-' + c.key + '" type="number" inputmode="decimal" step="' + step +
            '" min="' + c.min / (c.scale || 1) + '" max="' + c.max / (c.scale || 1) + '" value="' + v + '" data-orig="' + v + '">' +
            '<span class="cfield__unit">' + c.unit + '</span></div>' +
            '<span class="cfield__hint" id="hint-' + c.key + '">' + c.min / (c.scale || 1) + ' to ' + c.max / (c.scale || 1) + ' ' + c.unit + '</span></div>';
        }).join('') + '</div></fieldset>';
    }).join('');
    var fw = S.client.fw_version;
    $('#fw-version').textContent = fw ? 'Firmware ' + fw.value : '';
    validateConfig();
  }

  function readConfig() {
    var out = {};
    CONFIG.forEach(function (c) {
      var el = $('#cf-' + c.key);
      if (!el) return;
      var raw = num(el.value);
      out[c.key] = { def: c, raw: raw, value: raw == null ? null : Math.round(raw * (c.scale || 1)),
        dirty: el.value !== el.dataset.orig, el: el };
    });
    return out;
  }

  function validateConfig() {
    var f = readConfig();
    var ok = true, dirty = false;
    Object.keys(f).forEach(function (k) {
      var x = f[k], c = x.def, msg = '';
      if (x.value == null) msg = 'Enter a number';
      else if (x.value < c.min || x.value > c.max) msg = 'Must be ' + c.min / (c.scale || 1) + ' to ' + c.max / (c.scale || 1) + ' ' + c.unit;
      if (!msg && k === 'th_gas_danger_mv' && f.th_gas_warn_mv.value != null && x.value <= f.th_gas_warn_mv.value)
        msg = 'Must be above the warning level';
      var hint = $('#hint-' + k);
      hint.textContent = msg || (c.min / (c.scale || 1) + ' to ' + c.max / (c.scale || 1) + ' ' + c.unit);
      hint.classList.toggle('is-error', !!msg);
      x.el.setAttribute('aria-invalid', msg ? 'true' : 'false');
      x.el.closest('.cfield').classList.toggle('is-dirty', x.dirty);
      if (msg) ok = false;
      if (x.dirty) dirty = true;
    });
    $('#config-save').disabled = !(ok && dirty);
    return { ok: ok, fields: f };
  }

  function saveConfig(ev) {
    ev.preventDefault();
    var v = validateConfig();
    if (!v.ok || !S.device) return;
    var body = {};
    Object.keys(v.fields).forEach(function (k) { if (v.fields[k].dirty) body[k] = v.fields[k].value; });
    var btn = $('#config-save'), status = $('#config-status');
    busy(btn, true);
    status.classList.remove('is-error');
    status.textContent = 'Saving';
    Api.saveShared(S.device.id.id, body).then(function () {
      status.textContent = 'Saved. Waiting for the device to confirm.';
      Object.keys(body).forEach(function (k) {
        S.shared[k] = { key: k, value: body[k], lastUpdateTs: Date.now() };
      });
      renderSettings();
      setTimeout(function () {
        Api.attributes(S.device.id.id, 'CLIENT_SCOPE').then(function (a) {
          S.client = a;
          renderSettings();
          var rejected = Object.keys(body).filter(function (k) { return a['cfg_' + k] && a['cfg_' + k].value === 'rejected'; });
          status.textContent = rejected.length ? 'The device rejected ' + rejected.length + ' value(s).' : 'Saved.';
          status.classList.toggle('is-error', rejected.length > 0);
        });
      }, 4000);
    }, function (e) {
      status.textContent = e.message;
      status.classList.add('is-error');
      btn.disabled = false;
    }).finally(function () { btn.classList.remove('is-busy'); });
  }

  function rpcMessage(e) {
    if (e.status === 408 || e.status === 504) return 'The device did not answer. It may be offline.';
    if (e.status === 409) return 'The device is offline.';
    return e.message;
  }

  function drill(method) {
    if (!S.device) return;
    var dur = parseInt($('#drill-duration').value, 10);
    var btn = method === 'startDrill' ? $('#drill-start') : $('#drill-stop');
    var status = $('#drill-status');
    busy(btn, true);
    status.classList.remove('is-error');
    status.textContent = method === 'startDrill' ? 'Sending start command' : 'Sending stop command';
    Api.rpc(S.device.id.id, method, method === 'startDrill' ? { duration_s: dur } : {}).then(function (r) {
      var ok = r && r.result === 'ok';
      status.textContent = ok ? (method === 'startDrill' ? 'Drill running for ' + dur + ' s.' : 'Drill stopped.')
        : 'The device refused the command.';
      status.classList.toggle('is-error', !ok);
    }, function (e) {
      status.textContent = rpcMessage(e);
      status.classList.add('is-error');
    }).finally(function () { busy(btn, false); });
  }

  /* Router */
  function currentRoute() {
    var r = (location.hash || '').replace(/^#\/?/, '');
    return ['overview', 'history', 'settings'].indexOf(r) >= 0 ? r : 'overview';
  }

  function route() {
    var r = currentRoute();
    $$('.page').forEach(function (p) { p.hidden = p.dataset.page !== r; });
    $$('.topbar .segmented__item').forEach(function (a) {
      if (a.dataset.route === r) a.setAttribute('aria-current', 'page');
      else a.removeAttribute('aria-current');
    });
    if (r === 'history') loadHistory();
    if (r === 'settings') {
      renderSettings();
      if (S.device) loadAttributes().then(renderSettings, function () { /* keep current view */ });
    }
    if (r === 'overview') renderOverview();
  }

  /* Wiring */
  function init() {
    $('#login-form').addEventListener('submit', onLogin);
    $$('#login-mode [data-mode]').forEach(function (b) {
      b.addEventListener('click', function () { setLoginMode(b.dataset.mode); $('#login-error').hidden = true; });
    });
    $('#login-host').addEventListener('input', syncTokenLink);
    $('#logout').innerHTML = ICONS.out;
    $('#logout').addEventListener('click', logout);
    $('#theme-toggle').addEventListener('click', toggleTheme);
    paintThemeButton();
    $('#device-select').addEventListener('change', function (e) { selectDevice(e.target.value); });
    window.addEventListener('hashchange', route);

    $$('#range [data-range]').forEach(function (b) {
      b.setAttribute('aria-checked', String(+b.dataset.range === S.range));
      b.addEventListener('click', function () {
        S.range = +b.dataset.range;
        $$('#range [data-range]').forEach(function (x) { x.setAttribute('aria-checked', String(x === b)); });
        loadHistory();
      });
    });
    $$('#event-filter .chip').forEach(function (c) {
      c.addEventListener('click', function () {
        S.filter = c.dataset.type;
        $$('#event-filter .chip').forEach(function (x) { x.setAttribute('aria-pressed', String(x === c)); });
        renderEvents();
      });
    });

    $('#config-form').addEventListener('input', validateConfig);
    $('#config-form').addEventListener('submit', saveConfig);
    $('#config-reset').addEventListener('click', function () {
      renderSettings();
      $('#config-status').textContent = '';
    });
    var dd = $('#drill-duration');
    dd.addEventListener('input', function () { $('#drill-out').textContent = dd.value + ' s'; });
    $('#drill-start').addEventListener('click', function () { drill('startDrill'); });
    $('#drill-stop').addEventListener('click', function () { drill('stopDrill'); });

    if (window.matchMedia) {
      matchMedia('(prefers-color-scheme: dark)').addEventListener('change', function () { paintThemeButton(); restyleCharts(); });
    }

    /* refresh relative times */
    setInterval(function () {
      if (!$('#app-view').hidden && currentRoute() === 'overview') {
        $('#meta-last').textContent = S.t.state ? ago(S.t.state.ts) : '–';
        paintConn();
      }
    }, 1000);

    Api.onSessionEnd = function () {
      clearTimeout(expiryTimer);
      showLogin('Your session has expired. Sign in again.');
    };
    if (Api.hasSession()) {
      Api.me().then(startApp, function (e) {
        if (e.status !== 401) showLogin(e.message);
      });
    } else {
      showLogin();
    }
  }

  init();
})();
