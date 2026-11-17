// Watch Together: a room of people, each playing their own copy of a video
// in VLC, kept in step through a sync server (Tandem's server/, see
// docs in that project: protocol.md).
//
// This page holds the connection to the sync server and runs the sync logic
// (sync-core.js, shared unchanged with Tandem). Synaxis's local API is its
// hands on this computer: it fingerprints files, finds the room's file in
// the library, and opens and drives VLC (/api/together/*).
//
// app.js owns the rest of the page and calls Together.onRoute() whenever the
// address changes. Everything here lives in #together, outside the part
// app.js redraws, so the room survives moving around the library.

import { SYNC_CONFIG } from './sync-config.js';
import { bestOffset, expectedPosition, isRolling, restoreInfo, SyncController } from './sync-core.js';

const root = document.getElementById('together');
const $ = (selector) => root.querySelector(selector);
const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => (
  { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const notify = (message, kind) => window.toast?.(message, kind);

// The Android app plays through VLC for Android, which another app can't
// control, so Watch Together is desktop only.
const SUPPORTED = !window.SynaxisAndroid;

/** How often VLC's state is read and handed to the sync logic. */
const PLAYER_POLL_MS = 250;

const remember = {
  get(key) { try { return localStorage.getItem(key); } catch { return null; } },
  set(key, value) { try { localStorage.setItem(key, value); } catch { /* private mode */ } },
  remove(key) { try { localStorage.removeItem(key); } catch { /* private mode */ } },
};

/** This computer's permanent id in rooms, so a reconnect is recognised as the same person. */
function clientId() {
  let id = remember.get('synaxis.together.client');
  if (!id) {
    id = crypto.randomUUID();
    remember.set('synaxis.together.client', id);
  }
  return id;
}

/** Synaxis's own API, on this computer. */
async function local(method, path, body) {
  const init = { method, headers: { 'X-Synaxis': '1' } };
  if (method === 'POST') body ??= {}; // the server wants a body with every POST
  if (body !== undefined) {
    init.headers['Content-Type'] = 'application/json';
    init.body = JSON.stringify(body);
  }
  const res = await fetch(path, init);
  let data = null;
  try { data = await res.json(); } catch { /* empty body */ }
  if (!res.ok) throw new Error(data?.error || `Request failed (${res.status})`);
  return data;
}

const IDLE = { mediaId: null, status: 'idle', position: 0, rate: 1 };

const T = {
  onPage: false, // the Watch Together page is the one showing
  name: 'Guest',
  profileId: null,
  serverUrl: '',
  socket: null,
  connection: 'offline', // 'offline' | 'connecting' | 'online' | 'reconnecting'
  clockOffset: 0, // server time minus performance.now()
  room: null, // { code, members, you }
  state: null, // the room's playback state
  queue: [], // the room's YouTube queue (Tandem users); kept only to restore the room
  controller: null,
  timers: [],

  // The room's file on this computer.
  view: { phase: 'idle' }, // see drawLocal()
  openMediaId: null, // the room media our VLC window is playing
  openGeneration: 0, // bumped on every open, so a stale reading is thrown away
  dismissed: null, // room media whose VLC window the person closed
  accepted: new Map(), // room media -> the other copy (file) the person chose to play
  reading: IDLE, // what VLC is doing, for the sync logic
  lastReady: null, // the last reading from VLC, for telling "finished" from "closed"
  polling: false,
};

const serverNow = () => performance.now() + T.clockOffset;

// ---------- Connection ----------

/** One question to the server, with its answer (or an error after 5 seconds). */
function ask(event, payload) {
  return new Promise((resolve, reject) => {
    T.socket.timeout(5000).emit(event, payload, (error, answer) => (error ? reject(error) : resolve(answer)));
  });
}

async function syncClock() {
  const samples = [];
  for (let i = 0; i < SYNC_CONFIG.clockSamples; i++) {
    const t0 = performance.now();
    try {
      const { serverTime } = await ask('time:ping', { t0 });
      samples.push({ t0, t1: performance.now(), serverTime });
    } catch { /* a lost ping just means one sample fewer */ }
  }
  if (samples.length) T.clockOffset = bestOffset(samples);
}

async function connect() {
  if (T.socket) return;
  T.connection = 'connecting';
  drawConnection();
  // Shipped with Synaxis rather than fetched from the sync server: code from
  // elsewhere must never run in this page, which can change local settings.
  const { io } = await import('./socket.io.esm.min.js');
  const socket = io(T.serverUrl, { transports: ['websocket', 'polling'] });
  T.socket = socket;

  let first = true;
  const ready = new Promise((resolve, reject) => {
    socket.on('connect', async () => {
      await syncClock();
      T.connection = 'online';
      // After a dropped connection, take our seat in the room again.
      if (!first && T.room) await enterRoom('room:join', { code: T.room.code }, true);
      first = false;
      drawConnection();
      clearTimeout(slow);
      clearTimeout(giveUp);
      showError('');
      resolve();
    });
    // A free host puts the server to sleep when nobody uses it; the first
    // connection wakes it, which takes a while. Socket.IO keeps retrying.
    const slow = setTimeout(() => showError('Waking up the sync server. This can take up to a minute…'), 4000);
    const giveUp = setTimeout(() => reject(new Error('unreachable')), 90_000);
  });
  socket.on('disconnect', () => {
    T.connection = 'reconnecting';
    drawConnection();
  });
  socket.on('state:changed', ({ state }) => applyState(state));
  socket.on('queue:changed', ({ queue }) => { T.queue = queue; });
  socket.on('member:joined', ({ members }) => membersChanged(members));
  socket.on('member:left', ({ members }) => membersChanged(members));

  T.timers.push(setInterval(() => { if (socket.connected) syncClock(); }, SYNC_CONFIG.clockResyncMs));

  await ready; // connected, and the clock is set
}

function disconnect() {
  T.timers.splice(0).forEach(clearInterval);
  T.socket?.disconnect();
  T.socket = null;
  T.connection = 'offline';
}

// ---------- Rooms ----------

async function enterRoom(event, payload, rejoining = false) {
  if (!T.serverUrl) return showError('Set the sync server address in Settings first.');
  let answer;
  try {
    await connect();
    // Coming back to a room: bring what we know, in case the server lost it.
    const restore = rejoining ? { restore: restoreInfo(T.state, T.queue, serverNow()) } : {};
    answer = await ask(event, { ...payload, ...restore, clientId: clientId(), name: T.name });
  } catch {
    disconnect();
    return showError(`Couldn't reach the sync server at ${T.serverUrl}. Check it in Settings.`);
  }
  if (!answer.ok) {
    if (!rejoining) {
      disconnect();
      return showError(answer.error);
    }
    // The room we were in no longer exists.
    const wasInside = Boolean(T.room);
    leaveRoom();
    return showError(wasInside ? 'The room has closed.' : '');
  }

  const fresh = !T.room;
  T.room = { code: answer.room.code, members: answer.room.members, you: answer.room.you };
  remember.set('synaxis.together.room', T.room.code);
  if (fresh) {
    drawRoom();
    startController();
  }
  T.queue = answer.room.queue ?? [];
  drawPeople();
  applyState(answer.room.state, true);
  window.dispatchEvent(new Event('together-changed'));
}

function leaveRoom() {
  T.socket?.emit('room:leave');
  disconnect();
  remember.remove('synaxis.together.room');
  closeOurVlc();
  Object.assign(T, { room: null, state: null, queue: [], controller: null, view: { phase: 'idle' }, dismissed: null });
  T.accepted.clear();
  drawLobby();
  layout();
  window.dispatchEvent(new Event('together-changed'));
}

function nameOf(id) {
  if (id === T.room?.you) return 'you';
  return T.room?.members.find((m) => m.id === id)?.name ?? 'someone';
}

function membersChanged(members) {
  if (!T.room) return;
  const before = new Map(T.room.members.map((m) => [m.id, m]));
  T.room.members = members;
  const now = new Set(members.map((m) => m.id));
  for (const member of members) {
    if (member.id === T.room.you) continue;
    const was = before.get(member.id);
    if (member.online && !was?.online) notify(`${member.name} joined`);
    if (!member.online && was?.online) notify(`${member.name} went offline`);
  }
  for (const [id, member] of before) {
    if (!now.has(id) && id !== T.room.you) notify(`${member.name} left the room`);
  }
  drawPeople();
  window.dispatchEvent(new Event('together-changed'));
}

// ---------- Sync ----------

function startController() {
  T.controller = new SyncController({
    player: vlcPlayer,
    clientId: T.room.you,
    mode: 'local', // drift is corrected by playing slightly faster or slower
    now: serverNow,
    send: (event, payload) => T.socket?.emit(event, payload),
  });
  T.timers.push(setInterval(async () => {
    await readPlayer();
    T.controller?.onSnapshot(T.reading);
    drawStatus();
  }, PLAYER_POLL_MS));
  T.timers.push(setInterval(() => T.controller?.tick(), SYNC_CONFIG.tickMs));
}

function applyState(state, rejoined = false) {
  if (!T.controller) return;
  // After a reconnect the server may have restarted its numbering: start over.
  if (rejoined) T.controller.state = null;
  T.state = state;
  T.controller.onRemoteState(state);
  layout();
  drawStatus();
}

/** Ask Synaxis what the room's VLC window is doing, as a reading for the sync logic. */
async function readPlayer() {
  if (!T.openMediaId || T.polling) return;
  const generation = T.openGeneration;
  T.polling = true;
  let r = null;
  try { r = await local('GET', '/api/together/player'); } catch { /* Synaxis is busy; next time */ }
  T.polling = false;
  if (!r || generation !== T.openGeneration || !T.openMediaId) return;

  if (!r.open) {
    // The window was closed, or the video ended. Don't reopen it behind the person's back.
    const last = T.lastReady;
    const finished = last && last.length > 0 && last.position >= last.length - 5;
    T.dismissed = T.openMediaId;
    T.openMediaId = null;
    T.reading = IDLE;
    setView({ phase: finished ? 'finished' : 'closed', file: T.view.file });
    return;
  }
  if (!r.ready) return; // VLC is still starting
  T.lastReady = r;
  T.reading = {
    mediaId: T.openMediaId,
    status: r.state === 'stopped' ? 'idle' : r.state !== 'playing' ? 'paused' : r.stalled ? 'buffering' : 'playing',
    position: r.position,
    rate: r.rate,
  };
}

/** VLC, as the sync logic sees it (see the top of sync-core.js). */
const vlcPlayer = {
  /** The room started playing something: find it here and open it. */
  async load(state) {
    if (state.mediaType !== 'local' || !state.mediaId) {
      closeOurVlc();
      setView({ phase: state.mediaType === 'youtube' ? 'youtube' : 'idle' });
      return;
    }
    if (T.dismissed === state.mediaId) return;
    const chosen = T.accepted.get(state.mediaId);
    if (chosen) return void open(chosen, state);

    setView({ phase: 'looking' });
    let match;
    try {
      match = await local('POST', '/api/together/find', { media_id: state.mediaId });
    } catch (err) {
      return setView({ phase: 'error', message: err.message });
    }
    if (T.state?.mediaId !== state.mediaId) return; // the room moved on meanwhile
    if (match.exact) await open(match.exact, state);
    else setView({ phase: 'missing', alternatives: match.alternatives, expected: match.expected });
  },
  play: () => control('play'),
  pause: () => control('pause'),
  seek: (seconds) => control('seek', seconds),
  setRate: (rate) => control('rate', rate),
};

function control(command, value = 0) {
  if (!T.openMediaId) return;
  local('POST', '/api/together/control', { command, value }).catch(() => {});
}

async function open(file, state) {
  if (!T.room || !T.controller?.state) return;
  const generation = ++T.openGeneration;
  T.reading = IDLE;
  T.lastReady = null;
  // Synaxis may already have it open for the room (this page was reloaded): keep that window.
  let current = null;
  try { current = await local('GET', '/api/together/player'); } catch { /* open it anew */ }
  if (generation !== T.openGeneration) return;
  if (!(current?.open && current.file_id === file.file_id)) {
    try {
      await local('POST', '/api/together/open', {
        profile: T.profileId,
        file: file.file_id,
        title: state.mediaTitle ?? file.name,
        position: expectedPosition(T.controller.state, serverNow()),
      });
    } catch (err) {
      return setView({ phase: 'error', message: err.message });
    }
    if (generation !== T.openGeneration) return;
  }
  T.openMediaId = state.mediaId;
  setView({ phase: 'playing', file });
}

function closeOurVlc() {
  T.openGeneration++;
  if (T.openMediaId) local('POST', '/api/together/close').catch(() => {});
  T.openMediaId = null;
  T.reading = IDLE;
}

// ---------- What app.js calls ----------

/** Play a title from the library for everyone in the room. */
async function playForRoom(body) {
  if (!T.socket?.connected) throw new Error('Not connected to the room yet. Try again in a moment.');
  const media = await local('POST', '/api/together/media', body);
  T.dismissed = null;
  T.socket.emit('state:request', {
    action: 'load', mediaType: 'local', mediaId: media.media_id, mediaTitle: media.title, position: media.start,
  });
}

// ---------- Drawing ----------

function showError(message) {
  const box = $('#tg-message');
  if (box) box.textContent = message || '';
}

function setView(view) {
  T.view = view;
  drawLocal();
}

function drawLobby() {
  root.innerHTML = `
    <div class="tg-lobby">
      <h1>Watch Together</h1>
      <p class="hint">Start a room and send its code to the people you want to watch with, or type in a code
        someone sent you. Each of you plays the video from your own library; Synaxis keeps everyone in step.
        Only play, pause and seek go over the network, never the video.</p>
      <div class="tg-cards">
        <section class="tg-card">
          <h2>Start a room</h2>
          <p class="hint">You'll get a 6-character code to share.</p>
          <button class="btn btn-play" data-tg="create">Create room</button>
        </section>
        <section class="tg-card">
          <h2>Join a room</h2>
          <p class="hint">Enter the code you were sent.</p>
          <form class="tg-join" data-tg-form="join">
            <input class="input tg-code-input" name="code" maxlength="8" placeholder="ABC123" autocomplete="off" required>
            <button class="btn btn-secondary" type="submit">Join</button>
          </form>
        </section>
      </div>
      <p class="tg-error" id="tg-message" role="alert"></p>
      <p class="hint">Sync server: <code>${esc(T.serverUrl || 'not set')}</code> · change it in <a href="#/settings">Settings</a></p>
    </div>`;
}

function drawRoom() {
  root.innerHTML = `
    <div class="tg-room">
      <div class="tg-bar">
        <div class="tg-codebox">
          <span class="tg-label">Room code</span>
          <b class="tg-code">${esc(T.room.code)}</b>
          <button class="btn btn-secondary tg-small" data-tg="copy">Copy</button>
        </div>
        <div class="tg-conn" id="tg-conn"></div>
        <button class="btn btn-outline tg-small" data-tg="leave">Leave room</button>
      </div>
      <div class="tg-people" id="tg-people"></div>
      <p class="tg-error" id="tg-message" role="alert"></p>
      <div class="tg-local" id="tg-local"></div>
      <div class="tg-status" id="tg-status"></div>
    </div>`;
  drawConnection();
  drawLocal();
}

function drawPeople() {
  const box = $('#tg-people');
  if (!box || !T.room) return;
  const me = T.room.members.find((m) => m.id === T.room.you);
  const others = T.room.members.filter((m) => m.id !== T.room.you);
  const person = (m, label) => `<span class="tg-person ${m.online ? 'online' : ''}"><i></i>${esc(label)}${m.online ? '' : ' (offline)'}</span>`;
  box.innerHTML = (me ? person(me, `${me.name} (you)`) : '')
    + others.map((m) => person(m, m.name)).join('')
    + (others.length ? '' : '<span class="tg-person waiting">Waiting for others to join…</span>');
}

function drawConnection() {
  const box = $('#tg-conn');
  if (!box) return;
  const label = { online: 'Connected', connecting: 'Connecting…', reconnecting: 'Reconnecting…', offline: 'Offline' }[T.connection];
  box.className = `tg-conn ${T.connection}`;
  box.innerHTML = `<i></i>${label}`;
}

const clock = (seconds) => {
  const s = Math.max(0, Math.floor(seconds));
  const h = Math.floor(s / 3600), m = Math.floor(s / 60) % 60;
  return `${h ? `${h}:${String(m).padStart(2, '0')}` : m}:${String(s % 60).padStart(2, '0')}`;
};
const megabytes = (bytes) => (bytes >= 1024 ** 3 ? `${(bytes / 1024 ** 3).toFixed(1)} GB` : `${Math.round(bytes / 1024 ** 2)} MB`);

/** What's happening with the room's video on this computer. */
function drawLocal() {
  const box = $('#tg-local');
  if (!box) return;
  const v = T.view;
  const title = esc(T.state?.mediaTitle || 'A video');
  let html;
  switch (v.phase) {
    case 'playing':
      html = `<h2>${title}</h2><p>Playing in VLC on this computer${v.file ? `: <code>${esc(v.file.name)}</code>` : ''}.
        Play, pause and seek in VLC apply to everyone in the room.</p>`;
      break;
    case 'looking':
      html = `<h2>${title}</h2><p>Looking for it in your library…</p>`;
      break;
    case 'missing': {
      const e = v.expected;
      const expected = e ? `${megabytes(e.size)}${e.duration ? `, ${clock(e.duration)} long` : ''}` : '';
      html = `<h2>${title}</h2>` + (v.alternatives?.length
        ? `<p>You don't have the exact file the room is playing (${expected}). These are the same length, so they
            look like other versions of it. You can play one, but it may be a little out of step:</p>
          <ul class="tg-files">${v.alternatives.map((f) => `
            <li><span><code>${esc(f.name)}</code> · ${megabytes(f.size)}</span>
              <button class="btn btn-secondary tg-small" data-tg="use-file" data-file="${esc(f.file_id)}">Play this one</button></li>`).join('')}</ul>`
        : `<p>That file isn't in your library (${expected}). Everyone needs the same file. If you have it, add its
            folder in <a href="#/settings">Settings</a>; Synaxis finds it by its contents, whatever it's called.</p>`);
      break;
    }
    case 'closed':
      html = `<h2>${title}</h2><p>VLC was closed on this computer. The others can keep watching.</p>
        <button class="btn btn-secondary tg-small" data-tg="reopen">Open it again</button>`;
      break;
    case 'finished':
      html = `<h2>${title}</h2><p>Finished. Pick something else from your library and press Play to start it for everyone.</p>`;
      break;
    case 'youtube':
      html = `<h2>${title === 'A video' ? 'A YouTube video' : title}</h2><p>The room is playing a YouTube video. Synaxis
        only plays videos from your library; the others can keep watching.</p>`;
      break;
    case 'error':
      html = `<h2>${title}</h2><p class="tg-error">${esc(v.message)}</p>`;
      break;
    default:
      html = `<div class="tg-empty">Nothing playing yet. Pick a movie or an episode from your library and press
        <b>Play</b>: it starts for everyone in the room.</div>`;
  }
  if (box.dataset.html !== html) {
    box.dataset.html = html;
    box.innerHTML = html;
  }
}

function drawStatus() {
  const box = $('#tg-status');
  if (!box || !T.room) return;
  const state = T.state;
  let text = '';
  if (state?.mediaId) {
    const now = serverNow();
    const at = clock(expectedPosition(state, now));
    if (state.waitingFor.length) {
      text = `Waiting for ${state.waitingFor.map(nameOf).join(', ')} to load…`;
    } else if (state.isPlaying && state.updatedAt > now + 300) {
      text = `Starting in ${Math.ceil((state.updatedAt - now) / 1000)}`;
    } else if (isRolling(state, now)) {
      const drift = T.openMediaId ? T.controller?.drift() : null;
      const sync = drift == null ? '' : Math.abs(drift) < 0.25
        ? ' · in step' : ` · ${Math.abs(drift).toFixed(1)} s ${drift > 0 ? 'ahead' : 'behind'}, catching up`;
      text = `Playing · ${at}${sync}`;
    } else {
      text = `Paused at ${at}`;
    }
  }
  if (box.textContent !== text) box.textContent = text;
}

function layout() {
  drawLocal();
  root.hidden = !T.onPage;
  document.body.classList.toggle('together-open', T.onPage);
}

// ---------- Events ----------

root.addEventListener('click', (event) => {
  const button = event.target.closest('[data-tg]');
  if (!button) return;
  switch (button.dataset.tg) {
    case 'create': return void enterRoom('room:create', {});
    case 'leave': return leaveRoom();
    case 'use-file': {
      const state = T.state;
      if (!state?.mediaId || state.mediaType !== 'local') return undefined;
      const file = T.view.alternatives?.find((f) => f.file_id === button.dataset.file);
      if (!file) return undefined;
      T.accepted.set(state.mediaId, file);
      T.dismissed = null;
      return void open(file, state);
    }
    case 'reopen':
      T.dismissed = null;
      if (T.state?.mediaId) vlcPlayer.load(T.state);
      return undefined;
    case 'copy':
      return void navigator.clipboard?.writeText(T.room.code)
        .then(() => notify('Room code copied'), () => notify('Copy failed; select the code instead', 'error'));
    default: return undefined;
  }
});

root.addEventListener('submit', (event) => {
  event.preventDefault();
  const form = event.target;
  if (form.dataset.tgForm === 'join') {
    showError('');
    enterRoom('room:join', { code: form.code.value.trim().toUpperCase() });
  }
});

// ---------- The interface for app.js ----------

window.Together = {
  supported: SUPPORTED,

  /** For debugging from the console: what the sync logic currently sees. */
  inspect() {
    const c = T.controller;
    return c && {
      serverNow: serverNow(), state: c.state, snapshot: c.snapshot, drift: c.drift(),
      guardFor: c.guardUntil - serverNow(), nudging: c.nudging, clockOffset: T.clockOffset,
      openMediaId: T.openMediaId, view: T.view,
    };
  },

  /** Whether you're in a room right now (so the library's Play button plays for everyone). */
  inRoom() {
    return Boolean(T.room);
  },

  /** How many people are in the room, you included. */
  roomSize() {
    return T.room?.members.length ?? 0;
  },

  /** Start a title for the whole room. `body` is what /api/play takes. */
  play: playForRoom,

  /** Called on every navigation with the current path, profile and settings. */
  onRoute(path, profile, settings) {
    if (!SUPPORTED) return void (root.hidden = true);
    T.onPage = path === '/together' && Boolean(profile);
    if (profile) {
      T.name = profile.name;
      T.profileId = profile.id;
    }
    if (settings && 'sync_server' in settings) T.serverUrl = (settings.sync_server || '').replace(/\/+$/, '');
    if (!profile) return void (root.hidden = true);

    if (!T.room && !root.querySelector('.tg-lobby')) drawLobby();
    // Reloading the page keeps you in your room.
    const saved = remember.get('synaxis.together.room');
    if (!T.room && saved && !T.socket && T.serverUrl) enterRoom('room:join', { code: saved }, true);
    layout();
  },
};
