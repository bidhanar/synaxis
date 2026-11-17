// Tandem's sync logic. No page, network or timers in here: the caller supplies
// a player, a clock and a send function, so everything can be tested with fakes.
//
// The player is anything with:
//   load(state, position)   start loading state.mediaId, to play from `position`
//   play()  pause()  seek(seconds)  setRate(rate)
// and the caller reports what the player is doing with onSnapshot():
//   { mediaId, status, position, rate }
//   status: 'playing' | 'paused' | 'buffering' | 'idle' (nothing loaded yet, or ended)

import { SYNC_CONFIG } from './sync-config.js';

/** Whether the room's media is actually moving at server time `now`. */
export function isRolling(state, now) {
  return state.isPlaying && state.waitingFor.length === 0 && now >= state.updatedAt;
}

/** Where playback should be at server time `now`, in seconds. */
export function expectedPosition(state, now) {
  if (!isRolling(state, now)) return state.position;
  return state.position + ((now - state.updatedAt) / 1000) * state.playbackRate;
}

/**
 * What a client sends when rejoining, so the server can rebuild the room if
 * it restarted and lost it: the media, where playback has got to by now, and
 * the queue. Harmless when the room still exists (the server ignores it).
 */
export function restoreInfo(state, queue, now) {
  const info = { queue: queue ?? [] };
  if (!state?.mediaId) return info;
  return {
    ...info,
    mediaType: state.mediaType,
    mediaId: state.mediaId,
    mediaTitle: state.mediaTitle,
    isPlaying: state.isPlaying,
    position: expectedPosition(state, now),
  };
}

/**
 * Clock offset from ping samples [{ t0, t1, serverTime }], where t0/t1 are
 * the local send and receive times. Uses the sample with the shortest round
 * trip, since it has the least room for error. serverNow = localNow + offset.
 */
export function bestOffset(samples) {
  let best = null;
  for (const s of samples) {
    const rtt = s.t1 - s.t0;
    if (!best || rtt < best.rtt) best = { rtt, offset: s.serverTime - (s.t0 + rtt / 2) };
  }
  return best ? best.offset : 0;
}

const clamp = (value, low, high) => Math.min(high, Math.max(low, value));
const steady = (status) => status === 'playing' || status === 'paused';

export class SyncController {
  /**
   * @param options.player    see the top of this file
   * @param options.clientId  this person's id in the room
   * @param options.mode      'youtube' (correct drift by seeking) or 'local' (by speed)
   * @param options.now       () => server time in ms
   * @param options.send      (event, payload) => void, to the sync server
   * @param options.setTimer  (fn, ms) => void; defaults to setTimeout. (Wrapped: Chrome refuses
   *                          to run setTimeout called as this.setTimer, with `this` not the window.)
   */
  constructor({ player, clientId, mode, now, send, config = SYNC_CONFIG, setTimer = (fn, ms) => setTimeout(fn, ms) }) {
    this.player = player;
    this.clientId = clientId;
    this.mode = mode;
    this.now = now;
    this.send = send;
    this.config = config;
    this.setTimer = setTimer;

    this.state = null; // latest room state
    this.snapshot = null; // latest player reading, with `at` (server time)
    this.lastSteady = null; // latest reading that was playing or paused (not buffering)
    this.guardUntil = 0; // until then, the player's events are our own doing
    this.commandsAfter = 0; // no new commands to the player before this
    this.loadingId = null; // media we have asked the player to load
    this.told = null; // what the player was last put into, by us or by the person here: 'play' or 'pause'
    this.readySent = false; // told the server we finished loading/buffering
    this.bufferingSince = null;
    this.stallSent = false;
    this.lastDriftCheck = 0;
    this.lastDriftSeek = -Infinity;
    this.measureSeek = false; // check how the last drift seek landed
    this.seekAhead = config.youtube.seekAheadStart;
    this.nudging = false; // playing slightly fast or slow to catch up
  }

  /**
   * Whether the room is moving, as far as commanding the player goes. A
   * start time a few milliseconds ahead of our clock (clocks never agree
   * exactly) counts as now; without that, every remote "play" would first
   * pause the player.
   */
  rolling(state, now) {
    return isRolling(state, now + this.config.startSlackMs);
  }

  /** How far the player is ahead (+) or behind (-) the room, or null if not comparable. */
  drift() {
    const { state, snapshot } = this;
    if (!state || !snapshot || snapshot.mediaId !== state.mediaId || !steady(snapshot.status)) return null;
    const now = this.now();
    return this.positionAt(snapshot, now) - expectedPosition(state, now);
  }

  /** Where the player is now, given a reading that may be a little old. */
  positionAt(snapshot, now) {
    if (snapshot.status !== 'playing') return snapshot.position;
    return snapshot.position + ((now - snapshot.at) / 1000) * snapshot.rate;
  }

  /** The server sent the room's state. */
  onRemoteState(state) {
    if (this.state && state.version <= this.state.version) return; // old news
    this.state = state;
    if (!state.waitingFor.includes(this.clientId)) this.readySent = false;
    this.commandsAfter = 0; // a real change is applied at once
    this.reconcile(true);

    // If playback starts at a moment still to come (a countdown), wake up then.
    const wait = state.updatedAt - this.now();
    if (wait > 0) this.setTimer(() => this.reconcile(false), wait + 5);
  }

  /** The player reported what it is doing (several times a second). */
  onSnapshot(reading) {
    const now = this.now();
    // Seeks are spotted by comparing with the last reading that wasn't
    // buffering: a seek during playback buffers for a moment in between.
    const previous = this.lastSteady;
    const snapshot = { ...reading, at: now };
    this.snapshot = snapshot;
    if (steady(snapshot.status)) this.lastSteady = snapshot;

    const state = this.state;
    if (!state || state.mediaId === null) return;
    if (snapshot.mediaId !== state.mediaId) return this.reconcile(false);

    if (state.waitingFor.includes(this.clientId)) {
      // The room is waiting for us. Playing means we're ready.
      if (snapshot.status === 'playing' && !this.readySent) {
        this.readySent = true;
        this.send('buffer:end');
      }
      return this.reconcile(false);
    }

    if (now >= this.guardUntil) {
      this.detectUserAction(state, snapshot, previous, now);
      // A player already doing what the room wants is in that state, whoever
      // put it there (the person may press play just as a countdown ends).
      // Otherwise their next pause would look like one of ours.
      if (snapshot.status === 'playing' && this.rolling(this.state, now)) this.told = 'play';
      if (snapshot.status === 'paused' && !this.state.isPlaying) this.told = 'pause';
    }
    this.detectStall(this.state, snapshot, now);
    this.reconcile(false);
  }

  /**
   * Outside the guard window, a player that disagrees with the room means
   * the person here pressed something, unless it is only doing what we last
   * told it (a player we paused to wait for a countdown is not a user pause).
   * Each action is sent once: we assume the server will accept it, so the
   * next reading already agrees.
   */
  detectUserAction(state, snapshot, previous, now) {
    const rolling = this.rolling(state, now);

    if (snapshot.status === 'paused' && rolling && this.told !== 'pause') {
      return this.request(state, { action: 'pause', position: snapshot.position }, { isPlaying: false }, now);
    }
    if (snapshot.status === 'playing' && !state.isPlaying && this.told !== 'play') {
      return this.request(state, { action: 'play', position: snapshot.position }, { isPlaying: true }, now);
    }

    // A seek shows as the position moving more than playback explains.
    if (!previous || previous.mediaId !== snapshot.mediaId || !steady(snapshot.status)) return;
    const elapsed = (snapshot.at - previous.at) / 1000;
    const moved = snapshot.position - previous.position;
    const most = (previous.status === 'playing' || snapshot.status === 'playing' ? elapsed * snapshot.rate : 0)
      + this.config.seekJump;
    if (moved < -this.config.seekJump || moved > most) {
      this.request(state, { action: 'seek', position: snapshot.position }, {}, now);
    }
  }

  /**
   * The person here switched to another video (clicked one, or YouTube moved
   * on by itself): the room follows. The room's state is updated here at
   * once, as the server will, so the player isn't sent back to the old video
   * while the answer is on its way.
   */
  requestLoad(mediaType, mediaId, position = 0) {
    const now = this.now();
    this.send('state:request', { action: 'load', mediaType, mediaId, position });
    this.state = {
      ...(this.state ?? { version: 0, updatedBy: this.clientId, mediaTitle: null }),
      mediaType, mediaId, mediaTitle: null, isPlaying: true, position, playbackRate: 1,
      updatedAt: now, waitingFor: [this.clientId],
    };
    this.readySent = false;
    this.told = 'play';
    this.lastSteady = null;
    if (this.nudging) this.endNudge();
    this.commanded(now);
  }

  /**
   * The player started over by itself, without us asking: the page around it
   * changed (YouTube's site reloads its player when you come back to a
   * video). It starts playing on its own, which is not the person pressing play.
   */
  playerRestarted() {
    this.snapshot = null;
    this.lastSteady = null;
    this.loadingId = null;
    this.told = 'play';
    if (this.nudging) this.endNudge();
    this.commanded(this.now());
  }

  request(state, request, change, now) {
    this.send('state:request', request);
    this.state = { ...state, ...change, position: request.position, updatedAt: now };
    // The player is now paused or playing by the person's choice; the next
    // change the other way is a new action of theirs, not an echo of ours.
    if (request.action !== 'seek') this.told = request.action;
  }

  /** Tell the room when this player has been stuck buffering for a while. */
  detectStall(state, snapshot, now) {
    if (snapshot.status !== 'buffering' || !this.rolling(state, now)) {
      this.bufferingSince = null;
      this.stallSent = false;
      return;
    }
    this.bufferingSince ??= now;
    if (!this.stallSent && now - this.bufferingSince >= this.config.bufferDebounceMs) {
      this.stallSent = true;
      this.send('buffer:start');
    }
  }

  /** Our own seek. The jump it causes must not be mistaken for the person seeking. */
  seekPlayer(seconds) {
    this.lastSteady = null;
    this.player.seek(seconds);
  }

  /** Marks the player's coming events as caused by us, and paces our commands. */
  commanded(now) {
    this.guardUntil = now + this.config.guardMs;
    this.commandsAfter = now + this.config.commandCooldownMs;
  }

  /**
   * Make the player match the room. `fresh` is true when the room state
   * just changed, which is when a position mismatch should be fixed by
   * seeking at once instead of being left to drift correction.
   */
  reconcile(fresh) {
    const { state, snapshot, player, config } = this;
    const now = this.now();
    if (!state || state.mediaId === null) return;

    if (!snapshot || snapshot.mediaId !== state.mediaId) {
      if (this.loadingId !== state.mediaId) {
        this.loadingId = state.mediaId;
        player.load(state, expectedPosition(state, now)); // a load starts playing by itself
        this.lastSteady = null;
        this.told = 'play';
        this.commanded(now);
      }
      return;
    }
    this.loadingId = null;
    if (snapshot.status === 'idle' || now < this.commandsAfter) return;

    if (state.waitingFor.includes(this.clientId)) {
      // Let the player run until it really plays; that's how we know it's ready.
      if (snapshot.status === 'paused') {
        player.play();
        this.told = 'play';
        this.commanded(now);
      }
      return;
    }

    const target = expectedPosition(state, now);
    const off = snapshot.position - target;

    if (!this.rolling(state, now)) {
      let acted = false;
      // Only a player that is really playing is paused. One that is still
      // buffering may be the person here pressing play (YouTube buffers for
      // a moment first); if it starts playing, the next reading settles it.
      if (snapshot.status === 'playing') {
        player.pause();
        this.told = 'pause';
        acted = true;
      }
      // Nor is one that is buffering moved: its position jumps about, and
      // seeking it would swallow that press of play.
      if (steady(snapshot.status) && Math.abs(off) > config.pausedTolerance) {
        this.seekPlayer(target);
        acted = true;
      }
      if (this.nudging) this.endNudge();
      if (acted) this.commanded(now);
      return;
    }

    let acted = false;
    if (fresh && steady(snapshot.status) && Math.abs(off) > config.applySeekThreshold) {
      this.seekPlayer(target);
      acted = true;
    }
    if (snapshot.status === 'paused' && (this.told === 'pause' || now < this.guardUntil || fresh)) {
      // Paused by us (or not yet caught up with a fresh change): start it.
      // A pause the person here just pressed is left for detectUserAction.
      player.play();
      this.told = 'play';
      acted = true;
    }
    if (acted) this.commanded(now);
  }

  /** Drift correction. Call every config.tickMs. */
  tick() {
    const { state, snapshot, config } = this;
    const now = this.now();
    if (!state || !snapshot || state.mediaId === null || snapshot.mediaId !== state.mediaId) return;
    if (!this.rolling(state, now) || snapshot.status !== 'playing' || now < this.guardUntil) return;
    if (state.waitingFor.includes(this.clientId)) return;

    const drift = this.positionAt(snapshot, now) - expectedPosition(state, now);

    if (this.nudging) return this.continueNudge(drift, now);
    if (now - this.lastDriftCheck < config.driftCheckMs) return;
    this.lastDriftCheck = now;

    if (this.mode === 'youtube') this.correctBySeeking(drift, now);
    else this.correctBySpeed(drift, now);
  }

  /** YouTube only has a few fixed speeds, so drift is fixed by seeking. */
  correctBySeeking(drift, now) {
    const y = this.config.youtube;
    if (this.measureSeek) {
      // Learn from how the last seek landed: behind means aim further ahead.
      this.measureSeek = false;
      this.seekAhead = clamp(this.seekAhead - drift * 0.5, 0, y.seekAheadMax);
    }
    if (Math.abs(drift) <= y.seekThreshold || now - this.lastDriftSeek < y.seekCooldownMs) return;
    this.seekPlayer(expectedPosition(this.state, now) + this.seekAhead);
    this.lastDriftSeek = now;
    this.measureSeek = true;
    this.commanded(now);
  }

  /** Local files: play slightly fast or slow until caught up; seek only when far off. */
  correctBySpeed(drift, now) {
    const l = this.config.local;
    const size = Math.abs(drift);
    if (size < l.ignoreBelow) return;
    if (size > l.seekAbove) {
      this.seekPlayer(expectedPosition(this.state, now));
      this.commanded(now);
      return;
    }
    const step = size < l.gentleBelow ? l.gentleRate : l.strongRate;
    this.nudging = true;
    this.nudgeSign = Math.sign(drift);
    this.player.setRate(drift > 0 ? 1 - step : 1 + step); // ahead: slow down; behind: speed up
  }

  continueNudge(drift, now) {
    const l = this.config.local;
    const caughtUp = Math.abs(drift) < l.doneBelow || Math.sign(drift) !== this.nudgeSign;
    if (caughtUp) return this.endNudge();
    if (Math.abs(drift) > l.seekAbove) {
      this.endNudge();
      this.seekPlayer(expectedPosition(this.state, now));
      this.commanded(now);
    }
  }

  endNudge() {
    this.nudging = false;
    this.player.setRate(1);
  }
}
