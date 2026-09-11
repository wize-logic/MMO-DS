-- A request to have offline play checked: one monster, or the whole chain, at a price.

-- The chain is the evidence and the frontier is how far this server has replayed it itself.

CREATE TABLE verify_requests (
  id               BIGINT      NOT NULL PRIMARY KEY,
  chain_id         BIGINT      NOT NULL REFERENCES import_chains (id) ON DELETE CASCADE,
  character_id     BIGINT      NOT NULL REFERENCES characters (id) ON DELETE CASCADE,
  -- The personality of the one monster asked about, or NULL for the whole chain.
  monster_pid      INT,
  requested_at     TIMESTAMP   NOT NULL,
  -- What the walk could cost at most: every frame between the frontier and the end of the chain at
  -- the moment of asking.
  frame_budget     BIGINT      NOT NULL,
  -- The free allowance the character had left when asking, so the fee for what was actually run is
  -- reckoned against the same line the fee taken was.
  free_frames_left BIGINT      NOT NULL,
  fee_paid         INT         NOT NULL,
  fee_refunded     INT         NOT NULL,
  frames_run       BIGINT      NOT NULL,
  -- PENDING until a lane answers; then VERIFIED, DIVERGED, INCONCLUSIVE or UNVERIFIABLE.
  verdict          VARCHAR(16) NOT NULL,
  verdict_reason   VARCHAR(256),
  -- The session the walk stopped on: the birth, the end, or where it broke.
  stopped_link     INT,
  settled_at       TIMESTAMP,
  -- A settled request a moderator put back, once, and who.
  requeued_at      TIMESTAMP,
  requeued_by      VARCHAR(64)
);

CREATE INDEX verify_requests_by_character ON verify_requests (character_id, requested_at);

-- The chain keeps its state (HELD, VERIFIED or DIVERGED) and where it diverged; everything about
-- one ask leaves it.
ALTER TABLE import_chains DROP COLUMN fee_paid;
ALTER TABLE import_chains DROP COLUMN fee_refunded;
ALTER TABLE import_chains DROP COLUMN settled_at;
ALTER TABLE import_chains DROP COLUMN requeued_at;
ALTER TABLE import_chains DROP COLUMN requeued_by;
