// Every sync threshold in one place. Times are seconds unless the name says Ms.

export const SYNC_CONFIG = {
  // ---- Clock sync ----
  clockSamples: 8, // pings on connect; the one with the shortest round trip wins
  clockResyncMs: 60_000,

  // ---- Applying what the room says ----
  /** After commanding the player, ignore its own events for this long. */
  guardMs: 1500,
  /** Don't repeat commands to the player more often than this. */
  commandCooldownMs: 1000,
  /** A start time this close ahead of our clock counts as "now". */
  startSlackMs: 150,
  /** When a new room state arrives, seek if the player is further off than this. */
  applySeekThreshold: 0.5,
  /** While paused, how far from the room's position the player may sit. */
  pausedTolerance: 0.3,

  // ---- Telling the player's own jumps (someone seeking) from normal playback ----
  /** Between two readings, a move this much beyond normal playback is a seek. */
  seekJump: 1.0,

  // ---- Buffering ----
  /** Report a stall only after buffering this long (every seek buffers briefly). */
  bufferDebounceMs: 700,

  // ---- Drift correction ----
  driftCheckMs: 2000,
  /** How often the page calls tick(); faster than driftCheckMs so a speed nudge ends promptly. */
  tickMs: 500,

  youtube: {
    /** Drift below this is left alone; above it, seek. */
    seekThreshold: 0.5,
    /** Wait this long between drift seeks. */
    seekCooldownMs: 5000,
    /** A seek takes a moment to rebuffer, so aim a little ahead. Adjusted as we learn. */
    seekAheadStart: 0.3,
    seekAheadMax: 2.0,
  },

  local: {
    /** Drift below this is left alone. */
    ignoreBelow: 0.15,
    /** Up to here, play 5% faster or slower until caught up. */
    gentleBelow: 0.5,
    gentleRate: 0.05,
    /** Up to here, 10%. Beyond it, seek. */
    seekAbove: 2.0,
    strongRate: 0.10,
    /** A speed nudge ends once drift is back inside this. */
    doneBelow: 0.05,
  },
};
