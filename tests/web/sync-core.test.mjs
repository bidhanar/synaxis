import assert from 'node:assert/strict';
import { test } from 'node:test';

import { bestOffset, expectedPosition, isRolling, SyncController } from '../../web/sync-core.js';

const ME = 'me';
const PARTNER = 'partner';
const START = 1_000_000;

/** Behaves like a real player: commands take effect, playback advances with time. */
class FakePlayer {
  mediaId = null;
  status = 'idle';
  position = 0;
  rate = 1;
  commands = [];
  loadTime = 0.5; // seconds a load spends buffering before it plays
  loading = 0;

  load(state, position) {
    this.commands.push(['load', state.mediaId, position]);
    Object.assign(this, { mediaId: state.mediaId, position, status: 'buffering', loading: this.loadTime });
  }
  play() { this.commands.push(['play']); this.status = 'playing'; }
  pause() { this.commands.push(['pause']); this.status = 'paused'; }
  seek(seconds) { this.commands.push(['seek', seconds]); this.position = seconds; }
  setRate(rate) { this.commands.push(['rate', rate]); this.rate = rate; }

  advance(seconds) {
    if (this.loading > 0) {
      this.loading -= seconds;
      if (this.loading <= 0) this.status = 'playing';
    } else if (this.status === 'playing') {
      this.position += seconds * this.rate;
    }
  }
  read() {
    return { mediaId: this.mediaId, status: this.status, position: this.position, rate: this.rate };
  }
  count(name) { return this.commands.filter((c) => c[0] === name).length; }
}

function setup(mode = 'youtube') {
  const world = { time: START, sent: [], timers: [], player: new FakePlayer() };
  world.controller = new SyncController({
    player: world.player, clientId: ME, mode,
    now: () => world.time,
    send: (event, payload) => world.sent.push([event, payload]),
    setTimer: (fn, ms) => world.timers.push({ fn, at: world.time + ms }),
  });
  /** Lets time pass: the player advances and reports four times a second. */
  world.run = (ms) => {
    for (let passed = 0; passed < ms; passed += 250) {
      world.time += 250;
      world.player.advance(0.25);
      for (const timer of world.timers.splice(0)) {
        if (timer.at <= world.time) timer.fn(); else world.timers.push(timer);
      }
      world.controller.onSnapshot(world.player.read());
      if ((world.time - START) % 500 === 0) world.controller.tick();
    }
  };
  world.requests = () => world.sent.filter(([event]) => event === 'state:request').map(([, payload]) => payload);
  world.events = (name) => world.sent.filter(([event]) => event === name).length;
  return world;
}

let version = 0;
function roomState(changes = {}) {
  version += 1;
  return {
    mediaType: 'youtube', mediaId: 'vid', mediaTitle: null, isPlaying: true, position: 0,
    playbackRate: 1, updatedAt: START, version, updatedBy: PARTNER, waitingFor: [], ...changes,
  };
}

/** A world where the video is loaded and both people are playing in sync. */
function playingWorld(mode = 'youtube') {
  const world = setup(mode);
  world.controller.onRemoteState(roomState({ waitingFor: [ME] }));
  world.run(1000); // loads, plays, reports ready
  world.controller.onRemoteState(roomState({ position: world.player.position, updatedAt: world.time }));
  world.run(3000);
  world.sent.length = 0;
  world.player.commands.length = 0;
  return world;
}

// ---- Shared maths ----

test('expected position advances only while the room is rolling', () => {
  const playing = roomState({ position: 10, updatedAt: START });
  assert.equal(expectedPosition(playing, START + 4000), 14);
  assert.equal(expectedPosition({ ...playing, isPlaying: false }, START + 4000), 10);
  assert.equal(expectedPosition({ ...playing, waitingFor: [PARTNER] }, START + 4000), 10);
  assert.equal(isRolling({ ...playing, updatedAt: START + 3000 }, START + 1000), false); // countdown
  assert.equal(expectedPosition({ ...playing, updatedAt: START + 3000 }, START + 5000), 12);
});

test('clock offset comes from the fastest ping', () => {
  const offset = bestOffset([
    { t0: 100, t1: 400, serverTime: 5300 }, // slow, lopsided
    { t0: 500, t1: 520, serverTime: 5510 }, // fast: offset 5000
    { t0: 600, t1: 700, serverTime: 5640 },
  ]);
  assert.equal(offset, 5000);
  assert.equal(bestOffset([]), 0);
});

// ---- Loading and readiness ----

test('a remote load loads the video once and reports ready once it plays', () => {
  const world = setup();
  world.controller.onRemoteState(roomState({ waitingFor: [ME, PARTNER] }));
  world.run(3000);
  assert.equal(world.player.count('load'), 1);
  assert.equal(world.events('buffer:end'), 1);
  assert.deepEqual(world.requests(), []);
});

test('ready but waiting for the partner: the player is held, and nothing is sent', () => {
  const world = setup();
  world.controller.onRemoteState(roomState({ waitingFor: [ME, PARTNER] }));
  world.run(1000);
  world.controller.onRemoteState(roomState({ waitingFor: [PARTNER] }));
  world.run(4000);
  assert.equal(world.player.status, 'paused');
  assert.equal(world.player.position, 0);
  assert.deepEqual(world.requests(), []);
});

test('joining mid-video loads at the position playback has reached', () => {
  const world = setup();
  world.controller.onRemoteState(roomState({ position: 100, updatedAt: START - 20_000, waitingFor: [ME] }));
  assert.deepEqual(world.player.commands[0], ['load', 'vid', 100]);
});

// ---- Feedback-loop prevention ----

test('applying remote play, pause and seek sends nothing back', () => {
  const world = playingWorld();
  world.controller.onRemoteState(roomState({ isPlaying: false, position: 3, updatedAt: world.time }));
  world.run(4000);
  world.controller.onRemoteState(roomState({ isPlaying: true, position: 3, updatedAt: world.time }));
  world.run(4000);
  world.controller.onRemoteState(roomState({ isPlaying: true, position: 300, updatedAt: world.time }));
  world.run(4000);
  world.controller.onRemoteState(roomState({ isPlaying: false, position: 20, updatedAt: world.time }));
  world.run(4000);

  assert.deepEqual(world.sent, []);
  assert.equal(world.player.status, 'paused');
  assert.equal(world.player.position, 20);
});

test('one user pause causes exactly one request', () => {
  const world = playingWorld();
  world.player.status = 'paused'; // the person here pressed pause
  world.run(3000); // many readings before the server answers
  assert.equal(world.requests().length, 1);
  assert.equal(world.requests()[0].action, 'pause');

  // The server's answer arrives: still nothing more.
  world.controller.onRemoteState(roomState({
    isPlaying: false, position: world.requests()[0].position, updatedAt: world.time, updatedBy: ME,
  }));
  world.run(3000);
  assert.equal(world.requests().length, 1);
  assert.equal(world.player.count('seek'), 0);
});

test('one user play causes exactly one request', () => {
  const world = playingWorld();
  world.controller.onRemoteState(roomState({ isPlaying: false, position: 5, updatedAt: world.time }));
  world.run(3000);
  world.sent.length = 0;

  world.player.status = 'playing'; // the person here pressed play
  world.run(3000);
  assert.deepEqual(world.requests().map((r) => r.action), ['play']);
});

test('the same person can pause and then play again', () => {
  const world = playingWorld();
  world.player.status = 'paused';
  world.run(3000);
  world.player.status = 'buffering'; // YouTube buffers for a moment before it plays
  world.run(500);
  world.player.status = 'playing';
  world.run(3000);
  world.player.status = 'paused';
  world.run(3000);
  assert.deepEqual(world.requests().map((r) => r.action), ['pause', 'play', 'pause']);
  assert.deepEqual(world.player.commands, []); // we never overrode them
});

test('one user seek causes exactly one request, forwards or backwards', () => {
  const world = playingWorld();
  world.player.position = 200;
  world.run(3000);
  world.player.position = 50;
  world.run(3000);
  assert.deepEqual(world.requests().map((r) => r.action), ['seek', 'seek']);
  assert.equal(world.requests()[0].position, 200.25);
  assert.equal(world.player.count('seek'), 0); // we never fought the user's seek
});

test('a seek that buffers for a while is still seen as one seek', () => {
  const world = playingWorld();
  world.player.position = 300;
  world.player.status = 'buffering';
  world.run(2000);
  world.player.status = 'playing';
  world.run(4000);
  assert.deepEqual(world.requests().map((r) => r.action), ['seek']);
  assert.equal(world.player.count('seek'), 0);
});

test('our own drift seek is not mistaken for the person seeking, however long it buffers', () => {
  const world = playingWorld();
  world.player.position -= 0.9;
  world.controller.lastSteady = null;
  const seek = world.player.seek.bind(world.player);
  world.player.seek = (seconds) => { seek(seconds); world.player.status = 'buffering'; };
  world.run(2500);
  assert.equal(world.player.count('seek'), 1);
  world.run(2500); // still buffering, well past the guard window
  world.player.status = 'playing';
  world.run(3000);
  assert.deepEqual(world.requests(), []);
});

test('a seek while paused is sent too', () => {
  const world = playingWorld();
  world.controller.onRemoteState(roomState({ isPlaying: false, position: 5, updatedAt: world.time }));
  world.run(3000);
  world.sent.length = 0;
  world.player.position = 90;
  world.run(2000);
  assert.deepEqual(world.requests(), [{ action: 'seek', position: 90 }]);
});

test('an older state than the one already applied is ignored', () => {
  const world = playingWorld();
  const older = roomState({ isPlaying: false, position: 1, updatedAt: world.time });
  world.controller.onRemoteState(roomState({ isPlaying: true, position: 60, updatedAt: world.time }));
  world.controller.onRemoteState(older);
  world.run(2000);
  assert.equal(world.player.status, 'playing');
  assert.ok(world.player.position > 60);
});

test('a remote play stamped slightly ahead of our clock does not pause the player first', () => {
  const world = playingWorld();
  world.controller.onRemoteState(roomState({ isPlaying: false, position: 5, updatedAt: world.time }));
  world.run(3000);
  world.player.commands.length = 0;
  world.controller.onRemoteState(roomState({ isPlaying: true, position: 5, updatedAt: world.time + 40 }));
  world.run(2000);
  assert.deepEqual(world.player.commands, [['play']]);
});

// ---- Buffering ----

test('a short stall is not reported; a long one is reported once', () => {
  const world = playingWorld();
  world.player.status = 'buffering';
  world.run(500);
  world.player.status = 'playing';
  world.run(1000);
  assert.equal(world.events('buffer:start'), 0);

  world.player.status = 'buffering';
  world.run(3000);
  assert.equal(world.events('buffer:start'), 1);
});

test('after a stall, ready is reported when the player plays again', () => {
  const world = playingWorld();
  world.player.status = 'buffering';
  world.run(1000);
  world.controller.onRemoteState(roomState({ position: 4, updatedAt: world.time, waitingFor: [ME] }));
  world.run(1000);
  assert.equal(world.events('buffer:end'), 0);
  world.player.status = 'playing';
  world.run(500);
  assert.equal(world.events('buffer:end'), 1);
  assert.deepEqual(world.requests(), []);
});

test('the partner stalling pauses this player without sending a pause', () => {
  const world = playingWorld();
  world.controller.onRemoteState(roomState({ position: 4, updatedAt: world.time, waitingFor: [PARTNER] }));
  world.run(3000);
  assert.equal(world.player.status, 'paused');
  assert.deepEqual(world.sent, []);
});

test('a countdown holds the player, then starts it on time', () => {
  const world = playingWorld();
  world.controller.onRemoteState(roomState({ position: 4, updatedAt: world.time, waitingFor: [PARTNER] }));
  world.run(2000);
  world.controller.onRemoteState(roomState({ position: 4, updatedAt: world.time + 3000 }));
  world.run(2500);
  assert.equal(world.player.status, 'paused');
  world.run(1000);
  assert.equal(world.player.status, 'playing');
  assert.deepEqual(world.sent, []);
});

// ---- Drift correction ----

test('YouTube: small drift is left alone', () => {
  const world = playingWorld();
  world.player.position -= 0.3;
  world.controller.lastSteady = null; // forget the last reading so this isn't seen as a seek
  world.run(10_000);
  assert.deepEqual(world.player.commands, []);
  assert.deepEqual(world.sent, []);
});

test('YouTube: large drift is fixed with one seek, then a cooldown', () => {
  const world = playingWorld();
  world.player.position -= 0.9; // fell behind without a visible jump (a stall)
  world.controller.lastSteady = null;
  world.run(4000);
  assert.equal(world.player.count('seek'), 1);
  assert.ok(Math.abs(world.controller.drift()) < 0.5);
  assert.deepEqual(world.sent, []);
});

test('local: small drift ignored; medium drift fixed by speed; large by seeking', () => {
  const small = playingWorld('local');
  small.player.position -= 0.1;
  small.controller.lastSteady = null;
  small.run(6000);
  assert.deepEqual(small.player.commands, []);

  const behind = playingWorld('local');
  behind.player.position -= 0.3;
  behind.controller.lastSteady = null;
  behind.run(2500);
  assert.deepEqual(behind.player.commands, [['rate', 1.05]]);
  behind.run(8000);
  assert.deepEqual(behind.player.commands.at(-1), ['rate', 1]);
  assert.ok(Math.abs(behind.controller.drift()) < 0.15);
  assert.equal(behind.player.count('seek'), 0);

  const ahead = playingWorld('local');
  ahead.player.position += 1.0;
  ahead.controller.lastSteady = null;
  ahead.run(2500);
  assert.deepEqual(ahead.player.commands, [['rate', 0.9]]);

  const far = playingWorld('local');
  far.player.position -= 5;
  far.controller.lastSteady = null;
  far.run(2500);
  assert.equal(far.player.count('seek'), 1);
  assert.deepEqual(far.sent, []);
});

// ---- Following what the person here does on the page (browser extension) ----

test('switching video here asks the room to load it, without going back to the old one', () => {
  const world = playingWorld();
  world.player.mediaId = 'other'; // the person clicked another video; it starts by itself
  world.player.position = 0;
  world.controller.requestLoad('youtube', 'other', 0);
  world.run(2000);
  assert.deepEqual(world.requests(), [{ action: 'load', mediaType: 'youtube', mediaId: 'other', position: 0 }]);
  assert.equal(world.player.count('load'), 0); // not sent back to 'vid'
  assert.equal(world.events('buffer:end'), 1); // ready, since it plays

  // The server's answer (the partner still loading) holds us at the start, and nothing more is sent.
  world.controller.onRemoteState(roomState({ mediaId: 'other', updatedAt: world.time, updatedBy: ME, waitingFor: [PARTNER] }));
  world.run(3000);
  assert.equal(world.player.status, 'paused');
  assert.equal(world.requests().length, 1);
  assert.equal(world.events('buffer:end'), 1);
});

test('a player that restarts by itself and starts playing is not taken as the person pressing play', () => {
  const world = playingWorld();
  world.controller.onRemoteState(roomState({ isPlaying: false, position: 40, updatedAt: world.time }));
  world.run(3000);
  world.sent.length = 0;

  // The page was left and come back to: the player starts again from the top, playing.
  world.controller.playerRestarted();
  Object.assign(world.player, { status: 'playing', position: 0 });
  world.run(3000);
  assert.deepEqual(world.requests(), []);
  assert.equal(world.player.status, 'paused');
  assert.equal(world.player.position, 40);
});

test('pressing play on a player paused slightly off the room is not undone while it buffers', () => {
  const world = playingWorld();
  world.controller.onRemoteState(roomState({ isPlaying: false, position: 5, updatedAt: world.time }));
  world.run(3000);
  world.player.position = 5.25; // within the paused tolerance: left alone
  world.run(2000);
  world.sent.length = 0;
  world.player.commands.length = 0;

  // The person presses play. The player buffers for a moment, reading a little further on.
  Object.assign(world.player, { status: 'buffering', position: 5.45, loading: 0.5 });
  world.run(3000);
  assert.deepEqual(world.requests().map((r) => r.action), ['play']);
  assert.equal(world.player.count('seek'), 0);
  assert.equal(world.player.count('pause'), 0);
});

test('a pause after pressing play just as a countdown ends is still sent', () => {
  const world = playingWorld();
  // The partner stalled; we were held, then a countdown.
  world.controller.onRemoteState(roomState({ position: 4, updatedAt: world.time, waitingFor: [PARTNER] }));
  world.run(2000);
  world.controller.onRemoteState(roomState({ position: 4, updatedAt: world.time + 3000 }));
  world.run(2750);
  world.player.status = 'playing'; // the person here pressed play a moment before it started by itself
  world.run(3000);
  world.sent.length = 0;

  world.player.status = 'paused'; // and now pauses
  world.run(3000);
  assert.deepEqual(world.requests().map((r) => r.action), ['pause']);
  assert.equal(world.player.status, 'paused');
});
