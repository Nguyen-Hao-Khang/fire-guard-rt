/* Help page: theme toggle and current-section highlight */
(function () {
  'use strict';

  var ICONS = {
    sun: '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/></svg>',
    moon: '<svg viewBox="0 0 24 24"><path d="M20 14.5A8 8 0 1 1 9.5 4a6.5 6.5 0 0 0 10.5 10.5z"/></svg>'
  };
  var btn = document.getElementById('theme-toggle');

  function current() {
    var t = document.documentElement.dataset.theme;
    if (t) return t;
    return window.matchMedia && matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
  }

  function paint() {
    var dark = current() === 'dark';
    btn.innerHTML = dark ? ICONS.sun : ICONS.moon;
    btn.setAttribute('aria-label', dark ? 'Switch to light theme' : 'Switch to dark theme');
  }

  btn.addEventListener('click', function () {
    var next = current() === 'dark' ? 'light' : 'dark';
    document.documentElement.dataset.theme = next;
    try { localStorage.setItem('fg.theme', next); } catch (e) { /* storage unavailable */ }
    paint();
  });
  paint();

  /* mark the section being read in the table of contents */
  var links = Array.prototype.slice.call(document.querySelectorAll('.help__toc a'));
  if (!('IntersectionObserver' in window)) return;
  var io = new IntersectionObserver(function (entries) {
    entries.forEach(function (e) {
      if (!e.isIntersecting) return;
      links.forEach(function (a) {
        if (a.getAttribute('href') === '#' + e.target.id) a.setAttribute('aria-current', 'true');
        else a.removeAttribute('aria-current');
      });
    });
  }, { rootMargin: '-20% 0px -70% 0px' });
  document.querySelectorAll('.help__body section[id]').forEach(function (s) { io.observe(s); });
})();
