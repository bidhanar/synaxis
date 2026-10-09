// Synaxis web UI: a single-page app over the local JSON API.
'use strict';

// ---------- Utilities ----------

const $ = (sel, root = document) => root.querySelector(sel);
const $$ = (sel, root = document) => [...root.querySelectorAll(sel)];
const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => (
  { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

const store = {
  get(k) { try { return localStorage.getItem(k); } catch { return null; } },
  set(k, v) { try { localStorage.setItem(k, v); } catch { /* private mode */ } },
  sget(k) { try { return sessionStorage.getItem(k); } catch { return null; } },
  sset(k, v) { try { sessionStorage.setItem(k, v); } catch { /* private mode */ } },
};

async function request(method, path, body) {
  const init = { method, headers: { 'X-Synaxis': '1' } };
  if (body !== undefined) {
    init.headers['Content-Type'] = 'application/json';
    init.body = JSON.stringify(body);
  }
  const res = await fetch(path, init);
  let data = null;
  try { data = await res.json(); } catch { /* empty body */ }
  if (!res.ok) throw new Error((data && data.error) || `Request failed (${res.status})`);
  return data;
}
const api = {
  get: (p) => request('GET', p),
  post: (p, b = {}) => request('POST', p, b),
  del: (p) => request('DELETE', p),
};

function fmtDuration(sec) {
  if (!sec || sec < 1) return '';
  const m = Math.round(sec / 60);
  if (m < 60) return `${Math.max(1, m)}m`;
  return `${Math.floor(m / 60)}h ${m % 60}m`;
}
function fmtClock(sec) {
  sec = Math.max(0, Math.floor(sec || 0));
  const h = Math.floor(sec / 3600), m = Math.floor(sec / 60) % 60, s = sec % 60;
  const mm = String(m).padStart(h ? 2 : 1, '0'), ss = String(s).padStart(2, '0');
  return h ? `${h}:${mm}:${ss}` : `${mm}:${ss}`;
}
function fmtSize(bytes) {
  if (!bytes) return '';
  const gb = bytes / 1024 ** 3;
  return gb >= 1 ? `${gb.toFixed(1)} GB` : `${Math.round(bytes / 1024 ** 2)} MB`;
}
function hue(s) {
  let h = 0;
  for (const c of String(s)) h = (h * 31 + c.charCodeAt(0)) % 360;
  return h;
}
function initials(name) {
  const words = String(name).split(/\s+/).filter((w) => /\w/.test(w));
  return words.slice(0, 2).map((w) => w[0].toUpperCase()).join('') || '?';
}
const plural = (n, word) => `${n} ${word}${n === 1 ? '' : 's'}`;

// ---------- Icons ----------

const svg = (body, extra = '') =>
  `<svg viewBox="0 0 24 24" aria-hidden="true" ${extra}>${body}</svg>`;
const stroke = 'fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"';
const I = {
  play: svg('<path d="M7 4.6v14.8L19.5 12z" fill="currentColor"/>'),
  plus: svg('<path d="M12 5v14M5 12h14"/>', stroke),
  check: svg('<path d="M5 12.5l4.5 4.5L19 7.5"/>', stroke),
  close: svg('<path d="M6 6l12 12M18 6L6 18"/>', stroke),
  info: svg('<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7.5v.01"/>', stroke),
  down: svg('<path d="M6 9l6 6 6-6"/>', stroke),
  left: svg('<path d="M15 5l-7 7 7 7"/>', stroke),
  right: svg('<path d="M9 5l7 7-7 7"/>', stroke),
  search: svg('<circle cx="10.5" cy="10.5" r="6.5"/><path d="M15.5 15.5L20 20"/>', stroke),
  pencil: svg('<path d="M4 20h4L19 9l-4-4L4 16z"/><path d="M13.5 6.5l4 4"/>', stroke),
  trash: svg('<path d="M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13"/>', stroke),
  folder: svg('<path d="M3 6.5A1.5 1.5 0 0 1 4.5 5H9l2 2.5h8.5A1.5 1.5 0 0 1 21 9v9.5a1.5 1.5 0 0 1-1.5 1.5h-15A1.5 1.5 0 0 1 3 18.5z"/>', stroke),
  refresh: svg('<path d="M20 11a8 8 0 0 0-14.5-4.5L4 8M4 4v4h4M4 13a8 8 0 0 0 14.5 4.5L20 16M20 20v-4h-4"/>', stroke),
  restart: svg('<path d="M4 12a8 8 0 1 0 2.5-5.8L4 8.5M4 4v4.5h4.5"/>', stroke),
  stop: svg('<rect x="6.5" y="6.5" width="11" height="11" rx="1.5" fill="currentColor"/>'),
  eye: svg('<path d="M2.5 12S6 5.5 12 5.5 21.5 12 21.5 12 18 18.5 12 18.5 2.5 12 2.5 12z"/><circle cx="12" cy="12" r="3"/>', stroke),
  gear: svg('<circle cx="12" cy="12" r="3"/><path d="M12 2.5v3M12 18.5v3M4.2 6.2l2.1 2.1M17.7 15.7l2.1 2.1M2.5 12h3M18.5 12h3M4.2 17.8l2.1-2.1M17.7 8.3l2.1-2.1"/>', stroke),
  swap: svg('<path d="M7 7h12l-3-3M17 17H5l3 3"/>', stroke),
  power: svg('<path d="M12 3v8M6.3 6.3a8 8 0 1 0 11.4 0"/>', stroke),
};

const LOGO = `<span class="logo-mark"><svg viewBox="0 0 64 64" aria-hidden="true"><defs><linearGradient id="lg" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#ff5a36"/><stop offset="1" stop-color="#ffb347"/></linearGradient></defs><path d="M14 46a22 22 0 0 1 0-28M50 18a22 22 0 0 1 0 28" fill="none" stroke="url(#lg)" stroke-width="5" stroke-linecap="round" opacity=".55"/><path d="M25 20.5v23l19-11.5z" fill="url(#lg)"/></svg></span><span class="logo-word">SYNAXIS</span>`;

// Profile avatars: a colour and an abstract glyph.
const AVATAR_COLORS = ['#e4573d', '#3c7be0', '#e9a92c', '#33a877', '#9256e0', '#de5a85', '#22a9b8', '#6f7d8f'];
const AVATAR_GLYPHS = [
  '<circle cx="50" cy="50" r="22" fill="#fff" opacity=".92"/><circle cx="50" cy="50" r="10" fill="COLOR"/>',
  '<path d="M50 24l26 46H24z" fill="#fff" opacity=".92"/>',
  '<path d="M18 58c10-14 22-14 32 0s22 14 32 0M18 40c10-14 22-14 32 0s22 14 32 0" fill="none" stroke="#fff" stroke-width="7" stroke-linecap="round" opacity=".92"/>',
  '<path d="M50 20l8.8 18 19.7 2.9-14.3 14 3.4 19.6L50 65.2 32.4 74.5l3.4-19.6-14.3-14L41.2 38z" fill="#fff" opacity=".92"/>',
];
const AVATAR_COUNT = AVATAR_COLORS.length * AVATAR_GLYPHS.length;
function avatar(i) {
  i = ((i % AVATAR_COUNT) + AVATAR_COUNT) % AVATAR_COUNT;
  const color = AVATAR_COLORS[i % AVATAR_COLORS.length];
  const glyph = AVATAR_GLYPHS[Math.floor(i / AVATAR_COLORS.length)].replace('COLOR', color);
  return `<svg viewBox="0 0 100 100" aria-hidden="true"><rect width="100" height="100" fill="${color}"/><rect width="100" height="100" fill="url(#av-sheen)" opacity=".35"/>${glyph}<defs><linearGradient id="av-sheen" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#fff" stop-opacity=".5"/><stop offset="1" stop-color="#000" stop-opacity=".4"/></linearGradient></defs></svg>`;
}

// ---------- State ----------

const S = {
  profiles: [],
  profile: null,
  settings: null,
  status: null,
  pageKey: null,
  modal: null,          // { id, data, season }
  draft: null,          // settings being edited
  lastRefresh: 0,
  refreshQueued: false,
};
const app = $('#app');
const modalRoot = $('#modal-root');

function route() {
  const hash = location.hash.slice(1) || '/home';
  const [path, qs] = hash.split('?');
  return { path, params: new URLSearchParams(qs || '') };
}
function setParam(key, value) {
  const r = route();
  if (value == null) r.params.delete(key); else r.params.set(key, value);
  const qs = r.params.toString();
  location.hash = r.path + (qs ? `?${qs}` : '');
}

// ---------- Toasts ----------

function toast(msg, kind = '') {
  const el = document.createElement('div');
  el.className = `toast ${kind}`;
  el.textContent = msg;
  $('#toasts').append(el);
  setTimeout(() => el.classList.add('out'), 2800);
  setTimeout(() => el.remove(), 3200);
}

// ---------- Profile gate ----------

function renderGate(manage = false) {
  S.pageKey = null;
  closeModal(true);
  const tiles = S.profiles.map((p) => `
    <button class="profile" data-action="${manage ? 'edit-profile' : 'select-profile'}" data-id="${p.id}">
      <span class="avatar">${avatar(p.avatar)}${manage ? `<span class="avatar-edit">${I.pencil}</span>` : ''}</span>
      <span class="profile-name">${esc(p.name)}</span>
    </button>`).join('');
  const add = S.profiles.length < 6 ? `
    <button class="profile" data-action="new-profile">
      <span class="avatar add-avatar">${I.plus}</span>
      <span class="profile-name">Add Profile</span>
    </button>` : '';
  app.innerHTML = `
    <div class="gate">
      <div class="gate-logo"><span class="logo">${LOGO}</span></div>
      <h1>${manage ? 'Manage Profiles' : 'Who’s watching?'}</h1>
      <div class="profiles">${tiles}${add}</div>
      <button class="btn btn-outline" data-action="${manage ? 'gate' : 'manage-profiles'}">${manage ? 'Done' : 'Manage Profiles'}</button>
    </div>`;
  $('.profile', app)?.focus();
}

function openProfileEditor(profile) {
  let selected = profile ? profile.avatar : Math.floor(Math.random() * AVATAR_COLORS.length);
  const options = Array.from({ length: 16 }, (_, i) => i);
  if (!options.includes(selected)) options.push(selected);
  modalRoot.innerHTML = `
    <div class="modal-backdrop" data-backdrop>
      <form class="dialog" data-form="profile" role="dialog" aria-modal="true">
        <h2>${profile ? 'Edit Profile' : 'Add Profile'}</h2>
        <div class="dialog-row">
          <span class="avatar" id="avatar-preview">${avatar(selected)}</span>
          <div class="dialog-fields">
            <input class="input" name="name" maxlength="24" placeholder="Name" value="${esc(profile?.name || '')}" required autocomplete="off">
            <div class="avatar-grid">${options.map((i) => `
              <button type="button" data-avatar="${i}" class="${i === selected ? 'selected' : ''}" aria-label="Avatar ${i + 1}">${avatar(i)}</button>`).join('')}
            </div>
          </div>
        </div>
        <div class="dialog-actions">
          <button class="btn btn-play" type="submit">Save</button>
          <button class="btn btn-secondary" type="button" data-action="close-dialog">Cancel</button>
          ${profile && S.profiles.length > 1 ? '<button class="btn btn-danger" type="button" data-action="delete-profile">Delete Profile</button>' : ''}
        </div>
      </form>
    </div>`;
  const form = $('form', modalRoot);
  form.dataset.id = profile?.id || '';
  form.dataset.avatar = selected;
  form.addEventListener('click', (e) => {
    const b = e.target.closest('[data-avatar]');
    if (!b) return;
    selected = Number(b.dataset.avatar);
    form.dataset.avatar = selected;
    $$('[data-avatar]', form).forEach((x) => x.classList.toggle('selected', x === b));
    $('#avatar-preview').innerHTML = avatar(selected);
  });
  $('input[name=name]', form).focus();
}

async function saveProfile(form) {
  const name = form.name.value.trim();
  if (!name) return;
  const body = { name, avatar: Number(form.dataset.avatar) };
  try {
    if (form.dataset.id) await api.post(`/api/profiles/${form.dataset.id}`, body);
    else await api.post('/api/profiles', body);
    modalRoot.innerHTML = '';
    await loadProfiles();
    renderGate(Boolean(form.dataset.id));
  } catch (err) { toast(err.message, 'error'); }
}

async function deleteProfile(id) {
  const p = S.profiles.find((x) => x.id === id);
  if (!p || !confirm(`Delete ${p.name}'s profile? Their watch history and list go with it.`)) return;
  try {
    await api.del(`/api/profiles/${id}`);
    modalRoot.innerHTML = '';
    if (S.profile?.id === id) S.profile = null;
    await loadProfiles();
    renderGate(true);
  } catch (err) { toast(err.message, 'error'); }
}

async function loadProfiles() {
  const st = await api.get('/api/state');
  S.profiles = st.profiles;
  S.settings = st.settings;
  if (S.profile) S.profile = S.profiles.find((p) => p.id === S.profile.id) || null;
}

function selectProfile(id) {
  S.profile = S.profiles.find((p) => p.id === id) || null;
  if (!S.profile) return renderGate();
  store.set('synaxis.profile', id);
  store.sset('synaxis.session', '1');
  renderShell();
  if (!location.hash || location.hash === '#') location.hash = '/home';
  render();
}

// ---------- Shell ----------

function renderShell() {
  const others = S.profiles.filter((p) => p.id !== S.profile.id);
  app.innerHTML = `
    <header class="nav" id="nav">
      <a class="logo" href="#/home" aria-label="Synaxis home">${LOGO}</a>
      <nav class="nav-links">
        <a href="#/home" data-nav="/home">Home</a>
        <a href="#/shows" data-nav="/shows">TV Shows</a>
        <a href="#/movies" data-nav="/movies">Movies</a>
        <a href="#/list" data-nav="/list">My List</a>
      </nav>
      <div class="nav-right">
        <span class="scan-indicator" id="scan-indicator" hidden></span>
        <label class="search" id="search" title="Search (/)">
          ${I.search}
          <input type="search" id="search-input" placeholder="Titles, episodes" aria-label="Search" autocomplete="off">
        </label>
        <div class="profile-menu" id="profile-menu">
          <button class="avatar-btn" data-action="toggle-menu" aria-label="Account menu" aria-haspopup="menu">
            <span class="avatar">${avatar(S.profile.avatar)}</span><span class="caret">${I.down}</span>
          </button>
          <div class="menu" id="menu" role="menu" hidden>
            ${others.map((p) => `<button data-action="select-profile" data-id="${p.id}"><span class="avatar">${avatar(p.avatar)}</span>${esc(p.name)}</button>`).join('')}
            <button data-action="manage-profiles">${I.pencil}Manage Profiles</button>
            <button data-action="gate">${I.swap}Switch Profile</button>
            <hr>
            <a href="#/settings" data-action="close-menu">${I.gear}Settings</a>
            <button data-action="rescan">${I.refresh}Rescan Library</button>
            <hr>
            <button data-action="quit">${I.power}Quit Synaxis</button>
          </div>
        </div>
      </div>
    </header>
    <main id="page"></main>
    <div id="now-playing"></div>`;

  const input = $('#search-input');
  let timer;
  input.addEventListener('input', () => {
    clearTimeout(timer);
    timer = setTimeout(() => {
      const q = input.value.trim();
      if (q) location.hash = `/search?q=${encodeURIComponent(q)}`;
      else if (route().path === '/search') location.hash = '/home';
    }, 220);
  });
  renderStatus();
}

function updateNav(path) {
  $$('[data-nav]').forEach((a) => a.classList.toggle('active', a.dataset.nav === path));
  const input = $('#search-input');
  if (input && path !== '/search' && document.activeElement !== input) input.value = '';
  onScroll();
}

function onScroll() {
  $('#nav')?.classList.toggle('solid', window.scrollY > 20 || route().path === '/settings');
}
window.addEventListener('scroll', onScroll, { passive: true });

// ---------- Routing ----------

async function render() {
  if (!S.profile) return renderGate();
  if (!$('#page')) renderShell();
  const r = route();
  const params = new URLSearchParams(r.params);
  params.delete('title');
  const key = `${r.path}?${params}`;
  if (key !== S.pageKey) {
    const navigated = S.pageKey !== null;  // null: soft refresh keeps its place
    S.pageKey = key;
    updateNav(r.path);
    await renderPage(r);
    if (navigated) window.scrollTo({ top: 0, behavior: 'instant' });
  }
  syncModal(r.params.get('title'));
}
window.addEventListener('hashchange', render);

async function renderPage(r) {
  const page = $('#page');
  try {
    switch (r.path) {
      case '/shows': return await renderBrowse(page, 'show');
      case '/movies': return await renderBrowse(page, 'movie');
      case '/list': return await renderMyList(page);
      case '/search': return await renderSearch(page, r.params.get('q') || '');
      case '/settings': return await renderSettings(page);
      default:
        if (r.path !== '/home') return void (location.hash = '/home');
        return await renderBrowse(page, 'all');
    }
  } catch (err) {
    page.innerHTML = `<div class="empty"><h1>Something went wrong</h1><p>${esc(err.message)}</p>
      <button class="btn btn-secondary" data-action="reload">Try again</button></div>`;
  } finally {
    S.lastRefresh = Date.now();
  }
}

// Re-render the current page in place (new progress, artwork, etc.),
// keeping the scroll position and each row's horizontal position.
async function softRefresh() {
  if (!S.profile || !$('#page')) return;
  const busy = $('.card:hover') || $('#menu:not([hidden])') ||
    (route().path === '/settings') || document.activeElement?.closest?.('.dialog');
  if (busy) {
    S.refreshQueued = true;
    return;
  }
  S.refreshQueued = false;
  const y = window.scrollY;
  const rows = Object.fromEntries($$('.row').map((r) => [r.dataset.row, $('.row-track', r).scrollLeft]));
  S.pageKey = null;
  await render();
  $$('.row').forEach((r) => {
    const x = rows[r.dataset.row];
    if (x) $('.row-track', r).scrollLeft = x;
    updateArrows(r);
  });
  window.scrollTo({ top: y, behavior: 'instant' });
  if (S.modal) refreshModal();
}

// ---------- Cards & rows ----------

function placeholder(name, extra = '') {
  return `<div class="ph ${extra}" style="--h:${hue(name)}"><span>${esc(initials(name))}</span></div>`;
}

function badges(c) {
  const out = [];
  if (c.quality) out.push(`<span class="badge">${c.quality}</span>`);
  if (c.hdr) out.push('<span class="badge hdr">HDR</span>');
  if (c.year) out.push(`<span>${c.year}</span>`);
  if (c.kind === 'show') {
    out.push(`<span>${c.seasons > 1 ? plural(c.seasons, 'Season') : plural(c.episodes, 'Episode')}</span>`);
  } else if (c.runtime) {
    out.push(`<span>${fmtDuration(c.runtime)}</span>`);
  }
  return out.join('');
}

function remaining(up) {
  if (!up.duration) return '';
  return `${fmtDuration(Math.max(60, up.duration - up.position))} left`;
}

function upNextText(c) {
  const up = c.up_next;
  const ep = c.kind === 'show' && up.label ? `${up.label}${up.episode_title ? ` · ${up.episode_title}` : ''}` : '';
  if (up.reason === 'resume') return [ep, remaining(up)].filter(Boolean).join(' · ');
  if (up.reason === 'next') return `Up next: ${ep}`;
  return ep;
}

function playLabel(c) {
  const up = c.up_next;
  if (up.reason === 'resume') return 'Resume';
  if (up.reason === 'next') return `Play ${up.label}`;
  if (up.reason === 'rewatch') return 'Watch Again';
  return 'Play';
}

function progressPct(up) {
  return up.duration ? Math.min(100, (up.position / up.duration) * 100) : 0;
}

function cardHtml(c, type = 'titles') {
  const cont = type === 'continue';
  const img = (cont && c.up_next.image) || c.image;
  const showBar = cont && c.up_next.reason === 'resume';
  const bar = showBar ? `<div class="card-progress"><span style="width:${progressPct(c.up_next)}%"></span></div>` : '';
  const sub = cont ? upNextText(c) : (c.up_next.reason === 'resume' || c.up_next.reason === 'next' ? upNextText(c) : '');
  return `
    <article class="card ${cont ? 'card-continue' : ''}" data-title="${c.id}" tabindex="0" aria-label="${esc(c.name)}">
      <div class="card-art">
        ${img ? `<img src="${img}" alt="" loading="lazy" decoding="async">` : placeholder(c.name)}
        <div class="card-name">${esc(c.name)}</div>
        ${bar}
      </div>
      <div class="card-hover">
        <div class="card-actions">
          <button class="icon-btn solid" data-action="play" data-title="${c.id}" title="${esc(playLabel(c))}">${I.play}</button>
          <button class="icon-btn" data-action="my-list" data-title="${c.id}" title="${c.in_list ? 'Remove from My List' : 'Add to My List'}">${c.in_list ? I.check : I.plus}</button>
          ${cont ? `<button class="icon-btn" data-action="dismiss" data-title="${c.id}" title="Remove from Continue Watching">${I.close}</button>` : ''}
          <button class="icon-btn push" data-action="info" data-title="${c.id}" title="Episodes &amp; info">${I.down}</button>
        </div>
        <div class="card-title">${esc(c.name)}</div>
        <div class="card-meta">${badges(c)}</div>
        ${sub ? `<div class="card-sub">${esc(sub)}</div>` : ''}
        ${bar}
      </div>
    </article>`;
}

function rowHtml(row) {
  const cards = row.items.map((c, i) => (row.type === 'top10'
    ? `<div class="top10-item"><span class="rank">${i + 1}</span>${cardHtml(c, row.type)}</div>`
    : cardHtml(c, row.type))).join('');
  return `
    <section class="row" data-row="${esc(row.id)}">
      <h2 class="row-title">${esc(row.name)}</h2>
      <div class="row-viewport at-start">
        <button class="row-arrow left" data-action="scroll-row" data-dir="-1" aria-label="Scroll left">${I.left}</button>
        <div class="row-track">${cards}</div>
        <button class="row-arrow right" data-action="scroll-row" data-dir="1" aria-label="Scroll right">${I.right}</button>
      </div>
    </section>`;
}

function updateArrows(row) {
  const track = $('.row-track', row), vp = $('.row-viewport', row);
  if (!track || !vp) return;
  vp.classList.toggle('at-start', track.scrollLeft < 8);
  vp.classList.toggle('at-end', track.scrollLeft + track.clientWidth >= track.scrollWidth - 8);
}

function wireRows(root) {
  $$('.row', root).forEach((row) => {
    $('.row-track', row).addEventListener('scroll', () => updateArrows(row), { passive: true });
    updateArrows(row);
  });
}

function summary(c) {
  const up = c.up_next;
  if (c.kind === 'show') {
    const parts = [`${plural(c.seasons, 'season')} and ${plural(c.episodes, 'episode')} in your library.`];
    if (up.reason === 'resume') parts.push(`You stopped partway through ${up.label}${up.episode_title ? `, “${up.episode_title}”` : ''}.`);
    else if (up.reason === 'next') parts.push(`Up next is ${up.label}${up.episode_title ? `, “${up.episode_title}”` : ''}.`);
    else if (up.reason === 'rewatch') parts.push('You’ve watched every episode.');
    return parts.join(' ');
  }
  const bits = [];
  if (c.runtime) bits.push(`Runs ${fmtDuration(c.runtime)}`);
  if (c.quality) bits.push(c.quality === '4K' ? 'in 4K Ultra HD' : c.quality === 'HD' ? 'in HD' : 'in SD');
  let s = bits.length ? `${bits.join(' ')}.` : '';
  if (up.reason === 'resume') s += ` ${remaining(up)} from where you stopped.`;
  else if (up.reason === 'rewatch') s += ' You’ve seen this one.';
  return s.trim();
}

function heroHtml(c) {
  if (!c) return '';
  const resume = c.up_next.reason === 'resume' ? `
    <div class="progress-inline"><div class="bar"><span style="width:${progressPct(c.up_next)}%"></span></div><span>${esc(remaining(c.up_next))}</span></div>` : '';
  return `
    <section class="hero">
      ${c.image ? `<img class="hero-img" src="${c.image}" alt="">` : placeholder(c.name, 'hero-img')}
      <div class="hero-shade"></div>
      <div class="hero-body">
        <div class="hero-kicker">${c.kind === 'show' ? 'Series' : 'Film'}</div>
        <h1 class="hero-title">${esc(c.name)}</h1>
        <div class="meta-line">${badges(c)}</div>
        <p class="hero-summary">${esc(summary(c))}</p>
        ${resume}
        <div class="hero-actions" style="margin-top:18px">
          <button class="btn btn-play" data-action="play" data-title="${c.id}">${I.play}<span>${esc(playLabel(c))}</span></button>
          <button class="btn btn-ghost" data-action="info" data-title="${c.id}">${I.info}<span>More Info</span></button>
        </div>
      </div>
    </section>`;
}

function emptyLibrary() {
  const scan = S.status?.scan;
  const roots = S.settings?.roots || [];
  if (scan?.running) {
    const detail = scan.phase === 'scanning' ? 'Searching your folders for videos…'
      : `Reading ${scan.done} of ${scan.total} videos…`;
    return `<div class="empty"><div class="spinner"></div><h1>Building your library</h1><p>${detail}</p></div>`;
  }
  return `<div class="empty">
      <h1>No videos found</h1>
      <p>Synaxis looks for videos larger than ${esc(S.settings?.min_size_mb ?? 200)} MB in
      ${roots.length ? roots.map((r) => `<code>${esc(r)}</code>`).join(', ') : 'no folders yet'}.</p>
      <a class="btn btn-play" href="#/settings">Choose folders</a>
    </div>`;
}

async function renderBrowse(page, filter) {
  const data = await api.get(`/api/home?profile=${S.profile.id}&filter=${filter}`);
  if (!data.count) {
    page.className = '';
    page.innerHTML = filter === 'all' ? emptyLibrary()
      : `<div class="empty"><h1>No ${filter === 'show' ? 'TV shows' : 'movies'} yet</h1><p>Anything Synaxis finds will show up here.</p></div>`;
    return;
  }
  page.className = data.hero ? '' : 'page-plain';
  page.innerHTML = heroHtml(data.hero) + `<div class="rows">${data.rows.map(rowHtml).join('')}</div>`;
  wireRows(page);
}

async function renderMyList(page) {
  const data = await api.get(`/api/my-list?profile=${S.profile.id}`);
  page.className = 'page-plain';
  page.innerHTML = `
    <div class="page-head"><h1>My List</h1></div>
    ${data.items.length ? `<div class="grid">${data.items.map((c) => cardHtml(c)).join('')}</div>`
      : `<div class="empty" style="padding-top:8vh"><h1>Your list is empty</h1>
         <p>Press ${I.plus.replace('<svg', '<svg style="display:inline;width:16px;height:16px;vertical-align:-3px"')} on any title to save it here for later.</p></div>`}`;
}

async function renderSearch(page, q) {
  const input = $('#search-input');
  if (input && document.activeElement !== input) input.value = q;
  if (!q.trim()) {
    page.className = 'page-plain';
    page.innerHTML = '<div class="page-head"><h1>Search</h1><p>Type a title or episode name.</p></div>';
    return;
  }
  const data = await api.get(`/api/search?profile=${S.profile.id}&q=${encodeURIComponent(q)}`);
  page.className = 'page-plain';
  page.innerHTML = `
    <div class="page-head"><h1>${data.items.length ? `Results for “${esc(q)}”` : `No matches for “${esc(q)}”`}</h1>
    ${data.items.length ? '' : '<p>Try a different spelling, or rescan if you just added files.</p>'}</div>
    <div class="grid">${data.items.map((c) => cardHtml(c)).join('')}</div>`;
}

// ---------- Title modal ----------

function openTitle(id) { setParam('title', id); }
function closeModal(silent = false) {
  if (!modalRoot.innerHTML) return;
  modalRoot.innerHTML = '';
  S.modal = null;
  document.body.classList.remove('modal-open');
  if (!silent && route().params.has('title')) setParam('title', null);
}

async function syncModal(id) {
  if (!id) {
    if (S.modal) closeModal(true);
    return;
  }
  if (S.modal?.id === id) return;
  try {
    const data = await api.get(`/api/titles/${id}?profile=${S.profile.id}`);
    const current = data.items.find((i) => i.file_id === data.up_next.file_id) ||
      data.items.find((i) => i.versions.some((v) => v.file_id === data.up_next.file_id));
    S.modal = { id, data, season: current?.season ?? data.items[0]?.season ?? null };
    renderModal();
  } catch (err) {
    toast(err.message, 'error');
    setParam('title', null);
  }
}

async function refreshModal() {
  if (!S.modal) return;
  try {
    S.modal.data = await api.get(`/api/titles/${S.modal.id}?profile=${S.profile.id}`);
    renderModal(true);
  } catch { closeModal(); }
}

function episodeHtml(d, ep) {
  const p = ep.progress;
  const pct = p ? (p.completed ? 100 : progressPct(p)) : 0;
  const current = d.up_next.file_id === ep.file_id || ep.versions.some((v) => v.file_id === d.up_next.file_id);
  const best = ep.versions[0] || {};
  const desc = [ep.quality, best.source, fmtSize(best.size)].filter(Boolean).join(' · ') +
    (ep.versions.length > 1 ? ` · ${ep.versions.length} copies` : '');
  return `
    <li class="episode ${current && d.up_next.reason !== 'rewatch' ? 'current' : ''}" data-action="play" data-title="${d.id}" data-file="${ep.file_id}" title="${esc(best.path || '')}">
      <div class="ep-num">${ep.episode}</div>
      <div class="ep-thumb">
        ${ep.image ? `<img src="${ep.image}" alt="" loading="lazy">` : placeholder(d.name)}
        <span class="ep-play"><i>${I.play}</i></span>
        ${pct ? `<div class="card-progress"><span style="width:${pct}%"></span></div>` : ''}
      </div>
      <div class="ep-info">
        <div class="ep-top"><h4>${esc(ep.episode_title || `Episode ${ep.episode}`)}</h4><span class="ep-dur">${fmtDuration(ep.duration)}</span></div>
        <p class="ep-desc">${esc(desc)}${p && !p.completed && p.position > 0 ? ` · ${esc(fmtDuration(Math.max(60, p.duration - p.position)))} left` : ''}</p>
      </div>
      <button class="icon-btn sm ep-watched ${p?.completed ? 'on' : ''}" data-action="watched" data-title="${d.id}" data-file="${ep.file_id}" data-watched="${p?.completed ? 0 : 1}" title="${p?.completed ? 'Mark as unwatched' : 'Mark as watched'}">${I.check}</button>
    </li>`;
}

function versionsHtml(d) {
  const item = d.items[0];
  if (!item) return '';
  const rows = item.versions.map((v) => `
    <div class="version" data-action="play" data-title="${d.id}" data-file="${v.file_id}">
      <div class="ep-thumb">${item.image ? `<img src="${item.image}" alt="" loading="lazy">` : placeholder(d.name)}<span class="ep-play"><i>${I.play}</i></span></div>
      <div>
        <div class="version-name">${[v.width ? `${v.width}×${v.height}` : v.quality, v.codec?.toUpperCase(), v.hdr ? 'HDR' : '', v.source, ...(v.tags || [])].filter(Boolean).map(esc).join(' · ')}</div>
        <div class="version-path">${esc(v.path)} · ${fmtSize(v.size)}</div>
      </div>
      <button class="icon-btn sm" data-action="play" data-title="${d.id}" data-file="${v.file_id}" title="Play this copy">${I.play}</button>
    </div>`).join('');
  return `<div class="section-head"><h3>${item.versions.length > 1 ? `${item.versions.length} copies in your library` : 'File'}</h3></div>
    <div class="versions">${rows}</div>`;
}

function renderModal(keepScroll = false) {
  const { data: d } = S.modal;
  const up = d.up_next;
  const backdropScroll = keepScroll ? $('.modal-backdrop', modalRoot)?.scrollTop : 0;
  const isShow = d.kind === 'show';
  const allWatched = d.items.length > 0 && d.items.every((i) => i.progress?.completed);

  let episodes = '';
  if (isShow) {
    const seasons = [...new Set(d.items.map((i) => i.season))];
    const season = seasons.includes(S.modal.season) ? S.modal.season : seasons[0];
    const name = (s) => (s === 0 ? 'Specials' : `Season ${s}`);
    const picker = seasons.length > 1
      ? `<select class="season-select" data-season-select aria-label="Season">${seasons.map((s) => `<option value="${s}" ${s === season ? 'selected' : ''}>${name(s)}</option>`).join('')}</select>`
      : `<span class="season-label">${name(season)}</span>`;
    episodes = `
      <div class="section-head"><h3>Episodes</h3>${picker}</div>
      <ol class="episode-list">${d.items.filter((i) => i.season === season).map((ep) => episodeHtml(d, ep)).join('')}</ol>`;
  }

  const resume = up.reason === 'resume' ? `
    <div class="resume">
      ${isShow ? `<div class="now">${esc(up.label)}${up.episode_title ? ` · ${esc(up.episode_title)}` : ''}</div>` : ''}
      <div class="progress-inline"><div class="bar"><span style="width:${progressPct(up)}%"></span></div><span>${fmtClock(up.position)} of ${fmtClock(up.duration)}</span></div>
    </div>` : '';
  const formats = [...new Set(d.items.flatMap((i) => i.versions.map((v) => v.source)).filter(Boolean))];
  const totalSize = d.items.reduce((sum, i) => sum + i.versions.reduce((a, v) => a + (v.size || 0), 0), 0);

  modalRoot.innerHTML = `
    <div class="modal-backdrop" data-backdrop>
      <div class="modal" role="dialog" aria-modal="true" aria-label="${esc(d.name)}">
        <div class="modal-hero">
          ${d.image ? `<img src="${d.image}" alt="">` : placeholder(d.name)}
          <div class="modal-shade"></div>
          <button class="icon-btn modal-close" data-action="close-modal" aria-label="Close">${I.close}</button>
          <div class="modal-hero-body">
            <h2 class="modal-title">${esc(d.name)}</h2>
            <div class="hero-actions">
              <button class="btn btn-play" data-action="play" data-title="${d.id}">${I.play}<span>${esc(playLabel(d))}</span></button>
              ${up.reason === 'resume' ? `<button class="btn btn-ghost" data-action="play" data-title="${d.id}" data-file="${up.file_id}" data-from-start="1">${I.restart}<span>Start Over</span></button>` : ''}
              <button class="icon-btn lg" data-action="my-list" data-title="${d.id}" title="${d.in_list ? 'Remove from My List' : 'Add to My List'}">${d.in_list ? I.check : I.plus}</button>
              <button class="icon-btn lg ${allWatched ? 'ep-watched on' : ''}" data-action="watched" data-title="${d.id}" data-watched="${allWatched ? 0 : 1}" title="${allWatched ? 'Mark as unwatched' : 'Mark all as watched'}">${I.eye}</button>
            </div>
          </div>
        </div>
        <div class="modal-body">
          <div class="modal-cols">
            <div>
              <div class="meta-line">${badges(d)}</div>
              ${resume}
              <p class="modal-summary">${esc(summary(d))}</p>
            </div>
            <div class="modal-facts">
              ${formats.length ? `<div><span class="label">Source:</span> ${esc(formats.join(', '))}</div>` : ''}
              <div><span class="label">On disk:</span> ${esc(fmtSize(totalSize))}</div>
              <div class="path"><span class="label">Folder:</span> ${esc(d.folder)}</div>
            </div>
          </div>
          ${isShow ? episodes : versionsHtml(d)}
        </div>
      </div>
    </div>`;
  document.body.classList.add('modal-open');
  const bd = $('.modal-backdrop', modalRoot);
  if (keepScroll) bd.scrollTop = backdropScroll;
  else $('.modal-close', modalRoot).focus({ preventScroll: true });
}

// ---------- Actions ----------

async function play(titleId, fileId, fromStart) {
  try {
    const body = { profile: S.profile.id, title: titleId };
    if (fileId) body.file = fileId;
    if (fromStart) body.from_start = true;
    const res = await api.post('/api/play', body);
    toast(res.start > 0 ? `Resuming at ${fmtClock(res.start)} in VLC…` : 'Opening in VLC…');
    setTimeout(pollStatus, 1200);
  } catch (err) { toast(err.message, 'error'); }
}

async function toggleMyList(titleId) {
  try {
    const { in_list: inList } = await api.post('/api/my-list', { profile: S.profile.id, title: titleId });
    $$(`[data-action="my-list"][data-title="${titleId}"]`).forEach((b) => {
      b.innerHTML = inList ? I.check : I.plus;
      b.title = inList ? 'Remove from My List' : 'Add to My List';
    });
    if (S.modal?.id === titleId) S.modal.data.in_list = inList;
    toast(inList ? 'Added to My List' : 'Removed from My List');
    if (route().path === '/list' || $('[data-row="my-list"]') || inList) softRefresh();
  } catch (err) { toast(err.message, 'error'); }
}

async function dismiss(titleId, card) {
  try {
    await api.post('/api/dismiss', { profile: S.profile.id, title: titleId });
    card?.animate([{ opacity: 1, transform: 'scale(1)' }, { opacity: 0, transform: 'scale(.9)' }], { duration: 200 })
      .finished.then(() => {
        const row = card.closest('.row');
        card.remove();
        if (row && !$('.card', row)) row.remove();
      });
    toast('Removed from Continue Watching');
  } catch (err) { toast(err.message, 'error'); }
}

async function setWatched(titleId, fileId, watched) {
  try {
    const body = { profile: S.profile.id, title: titleId, watched };
    if (fileId) body.file = fileId;
    await api.post('/api/watched', body);
    toast(watched ? 'Marked as watched' : 'Marked as unwatched');
    await refreshModal();
    softRefresh();
  } catch (err) { toast(err.message, 'error'); }
}

// ---------- Settings ----------

async function renderSettings(page) {
  await loadProfiles();
  S.draft = JSON.parse(JSON.stringify(S.settings));
  page.className = '';
  page.innerHTML = '<div class="settings" id="settings"></div>';
  drawSettings();
  onScroll();
}

function drawSettings() {
  const d = S.draft;
  const el = $('#settings');
  if (!el) return;
  el.innerHTML = `
    <h1>Settings</h1>
    <section class="panel">
      <h2>Library folders</h2>
      <p class="hint">Synaxis searches these folders, and every folder inside them, for videos.</p>
      <ul class="roots">
        ${d.roots.length ? d.roots.map((r, i) => `<li>${I.folder}<code>${esc(r)}</code><button class="icon-btn sm plain" data-action="remove-root" data-index="${i}" title="Remove folder">${I.trash}</button></li>`).join('')
          : '<li class="none">No folders yet. Add one below.</li>'}
      </ul>
      <form class="add-root" data-form="add-root">
        <input name="path" placeholder="/media/movies or ~/Videos" autocomplete="off" required>
        <button class="btn btn-secondary" type="submit">Add Folder</button>
      </form>
    </section>
    <section class="panel">
      <h2>What counts as a video</h2>
      <p class="hint">Skipping small files keeps out trailers, samples and clips.</p>
      <label class="field"><span>Ignore files smaller than</span>
        <span class="with-unit"><input type="number" name="min_size_mb" min="0" step="10" value="${esc(d.min_size_mb)}"> MB</span>
      </label>
    </section>
    <section class="panel">
      <h2>Playback</h2>
      <p class="hint">Videos open in VLC. Synaxis follows along to remember where you stopped and queues the next episodes.</p>
      <label class="switch-row"><input type="checkbox" name="fullscreen" ${d.fullscreen ? 'checked' : ''}><span>Open VLC in fullscreen</span></label>
      <label class="field"><span>VLC program</span><input name="vlc" value="${esc(d.vlc)}" autocomplete="off"></label>
    </section>
    <div class="settings-actions">
      <button class="btn btn-play" data-action="save-settings">Save</button>
      <button class="btn btn-ghost" data-action="rescan">${I.refresh}<span>Rescan Now</span></button>
      <span class="scan-status" id="settings-scan"></span>
    </div>
    <section class="panel about">
      <div class="stats" id="library-stats"></div>
      <p style="margin-top:18px">Settings: <code>~/.config/synaxis/config.json</code> · Library, history and thumbnails: <code>~/.local/share/synaxis</code></p>
      <p>Synaxis is free software under the GNU GPL v3 or later.</p>
    </section>`;
  renderStatus();
}

function readSettingsForm() {
  const el = $('#settings');
  if (!el || !S.draft) return;
  S.draft.min_size_mb = Number($('[name=min_size_mb]', el).value) || 0;
  S.draft.fullscreen = $('[name=fullscreen]', el).checked;
  S.draft.vlc = $('[name=vlc]', el).value.trim() || 'vlc';
}

async function saveSettings() {
  readSettingsForm();
  try {
    const res = await api.post('/api/settings', {
      roots: S.draft.roots, min_size_mb: S.draft.min_size_mb, fullscreen: S.draft.fullscreen, vlc: S.draft.vlc,
    });
    S.settings = res.settings;
    S.draft = JSON.parse(JSON.stringify(res.settings));
    drawSettings();
    toast(res.rescanning ? 'Saved. Rescanning your library…' : 'Settings saved');
    if (res.rescanning) setTimeout(pollStatus, 500);
  } catch (err) { toast(err.message, 'error'); }
}

// ---------- Status polling ----------

function renderStatus() {
  const st = S.status;
  if (!st) return;
  const scan = st.scan;
  const phase = { scanning: 'Finding videos', reading: 'Reading videos', thumbnails: 'Making artwork' }[scan.phase];
  const detail = scan.total ? `${phase} ${scan.done}/${scan.total}` : phase;

  const ind = $('#scan-indicator');
  if (ind) {
    ind.hidden = !scan.running;
    ind.innerHTML = scan.running ? `<i class="spinner sm"></i><span>${esc(detail || 'Updating library')}</span>` : '';
  }
  const ss = $('#settings-scan');
  if (ss) {
    ss.innerHTML = scan.running ? `<i class="spinner sm"></i>${esc(detail || 'Updating library')}`
      : scan.finished_at ? `Last scanned ${new Date(scan.finished_at * 1000).toLocaleString()}` : '';
  }
  const stats = $('#library-stats');
  if (stats) {
    stats.innerHTML = `<div class="stat"><b>${st.titles}</b><span>Titles</span></div>
      <div class="stat"><b>${scan.files}</b><span>Video files</span></div>
      <div class="stat"><b>${S.profiles.length}</b><span>${S.profiles.length === 1 ? 'Profile' : 'Profiles'}</span></div>`;
  }

  const np = st.now_playing;
  const box = $('#now-playing');
  if (box) {
    box.innerHTML = np ? `
      <div class="now-playing" role="status">
        <div class="np-icon">${I.play}</div>
        <div class="np-text">
          <div class="np-label">${np.paused ? 'Paused in VLC' : 'Playing in VLC'}</div>
          <div class="np-title">${esc(np.title)}</div>
          ${np.duration ? `<div class="bar np-bar"><span style="width:${Math.min(100, (np.position / np.duration) * 100)}%"></span></div>` : ''}
        </div>
        <div class="np-time">${np.duration ? `${fmtClock(np.position)} / ${fmtClock(np.duration)}` : ''}</div>
        <button class="icon-btn sm" data-action="stop" title="Close VLC">${I.stop}</button>
      </div>` : '';
  }
}

let pollTimer;
async function pollStatus() {
  clearTimeout(pollTimer);
  try {
    const st = await api.get('/api/status');
    const prev = S.status;
    S.status = st;
    renderStatus();
    if (prev && S.profile) {
      const np0 = prev.now_playing, np1 = st.now_playing;
      const playbackChanged = (np0 && !np1) || (np0 && np1 && np0.file_id !== np1.file_id);
      const libraryChanged = prev.scan.version !== st.scan.version || (prev.scan.running && !st.scan.running);
      const artworkArrived = st.scan.phase === 'thumbnails' && st.scan.done !== prev.scan.done &&
        $('#page .ph') && Date.now() - S.lastRefresh > 8000;
      if (playbackChanged || libraryChanged || artworkArrived || S.refreshQueued) softRefresh();
    }
  } catch { /* server restarting; try again shortly */ }
  pollTimer = setTimeout(pollStatus, document.hidden ? 6000 : 2000);
}
document.addEventListener('visibilitychange', () => { if (!document.hidden) pollStatus(); });

// ---------- Events ----------

document.addEventListener('click', (e) => {
  const menu = $('#profile-menu');
  if (menu && !menu.contains(e.target)) closeMenu();

  const el = e.target.closest('[data-action]');
  if (!el) {
    const card = e.target.closest('.card');
    if (card) openTitle(card.dataset.title);
    return;
  }
  const action = el.dataset.action;
  if (action === 'close-menu') return closeMenu();
  if (el.tagName !== 'A') e.preventDefault();
  e.stopPropagation();
  const { title, file } = el.dataset;

  switch (action) {
    case 'play': return play(title, file, el.dataset.fromStart === '1');
    case 'info': return openTitle(title);
    case 'my-list': return toggleMyList(title);
    case 'dismiss': return dismiss(title, el.closest('.card'));
    case 'watched': return setWatched(title, file, el.dataset.watched === '1');
    case 'close-modal': return closeModal();
    case 'close-dialog': modalRoot.innerHTML = ''; return undefined;
    case 'scroll-row': {
      const track = $('.row-track', el.closest('.row'));
      return track.scrollBy({ left: Number(el.dataset.dir) * track.clientWidth * 0.85, behavior: 'smooth' });
    }
    case 'stop': return api.post('/api/stop').then(pollStatus).catch((err) => toast(err.message, 'error'));
    case 'toggle-menu': {
      const m = $('#menu');
      m.hidden = !m.hidden;
      $('#profile-menu').classList.toggle('open', !m.hidden);
      return undefined;
    }
    case 'select-profile': closeMenu(); return selectProfile(el.dataset.id);
    case 'gate': return loadProfiles().then(() => renderGate(false));
    case 'manage-profiles': return loadProfiles().then(() => renderGate(true));
    case 'new-profile': return openProfileEditor(null);
    case 'edit-profile': return openProfileEditor(S.profiles.find((p) => p.id === el.dataset.id));
    case 'delete-profile': return deleteProfile($('form[data-form=profile]').dataset.id);
    case 'rescan':
      closeMenu();
      return api.post('/api/rescan').then(() => { toast('Rescanning your library…'); setTimeout(pollStatus, 400); })
        .catch((err) => toast(err.message, 'error'));
    case 'remove-root':
      readSettingsForm();
      S.draft.roots.splice(Number(el.dataset.index), 1);
      return drawSettings();
    case 'save-settings': return saveSettings();
    case 'quit': return quitApp();
    case 'reload': S.pageKey = null; return render();
    default: return undefined;
  }
});

// Clicking the dimmed area around a modal closes it.
document.addEventListener('mousedown', (e) => {
  if (e.target.matches('[data-backdrop]')) {
    if (S.modal) closeModal(); else modalRoot.innerHTML = '';
  }
});

document.addEventListener('change', (e) => {
  if (e.target.matches('[data-season-select]') && S.modal) {
    S.modal.season = Number(e.target.value);
    renderModal(true);
  }
});

document.addEventListener('submit', (e) => {
  const form = e.target;
  e.preventDefault();
  if (form.dataset.form === 'profile') saveProfile(form);
  if (form.dataset.form === 'add-root') {
    readSettingsForm();
    const path = form.path.value.trim();
    if (path && !S.draft.roots.includes(path)) S.draft.roots.push(path);
    drawSettings();
    $('.add-root input')?.focus();
  }
});

document.addEventListener('keydown', (e) => {
  const typing = e.target.matches('input, textarea, select');
  if (e.key === 'Escape') {
    if ($('form[data-form=profile]')) modalRoot.innerHTML = '';
    else if (S.modal) closeModal();
    else closeMenu();
    if (typing) e.target.blur();
    return;
  }
  if (typing) return;
  if (e.key === '/' && $('#search-input')) {
    e.preventDefault();
    $('#search-input').focus();
  } else if (e.key === 'Enter' && e.target.matches('.card')) {
    openTitle(e.target.dataset.title);
  }
});

async function quitApp() {
  closeMenu();
  if (!confirm('Quit Synaxis? Open it again from your app menu any time.')) return;
  try { await api.post('/api/quit'); } catch { /* already gone */ }
  clearTimeout(pollTimer);
  closeModal(true);
  app.innerHTML = `<div class="gate"><span class="logo">${LOGO}</span>
    <h1>See you next time</h1><p style="color:var(--muted);margin:0">Synaxis has quit. You can close this tab.</p></div>`;
}

function closeMenu() {
  const m = $('#menu');
  if (m && !m.hidden) {
    m.hidden = true;
    $('#profile-menu')?.classList.remove('open');
  }
}

// ---------- Boot ----------

(async function boot() {
  try {
    await loadProfiles();
  } catch (err) {
    app.innerHTML = `<div class="empty"><h1>Can’t reach Synaxis</h1><p>${esc(err.message)}. Is the app still running?</p>
      <button class="btn btn-secondary" onclick="location.reload()">Retry</button></div>`;
    return;
  }
  pollStatus();
  // One profile: straight in. Several: ask once per browser session.
  const saved = S.profiles.find((p) => p.id === store.get('synaxis.profile'));
  if (S.profiles.length === 1) return selectProfile(S.profiles[0].id);
  if (saved && store.sget('synaxis.session')) return selectProfile(saved.id);
  return renderGate();
}());
