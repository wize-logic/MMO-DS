-- The sessions behind an import, and how far this server has replayed them itself.

-- An import is believed the moment it lands and marked while it is.

-- The frontier is the whole reason this is a table and not a column.

CREATE TABLE import_chains (
  id             BIGINT      NOT NULL PRIMARY KEY,
  character_id   BIGINT      NOT NULL REFERENCES characters (id) ON DELETE CASCADE,
  -- The import this chain is the evidence for. The verdict lands on that row; this one holds the
  -- work behind it.
  import_id      BIGINT      REFERENCES character_imports (id) ON DELETE SET NULL,
  uploaded_at    TIMESTAMP   NOT NULL,
  -- What the chain claims to start from: the hash of an export this server itself wrote, or NULL,
  -- which is a character played from a New Game and is a root with nothing in it to check.
  anchor_sha256  VARCHAR(64),
  link_count     INT         NOT NULL,
  -- The sum of the links' end frames. The caps are read in frames because that is what a replay
  -- costs; hours are only how the operator writes them down.
  frame_total    BIGINT      NOT NULL,
  -- The last ordinal the worker replayed and agreed with, or -1 for nothing yet, together with the
  -- image it ended holding. That image is the only boot state any later link is allowed.
  verified_link  INT         NOT NULL,
  frontier_image BYTEA,
  -- PENDING until the worker answers; then VERIFIED, DIVERGED, INCONCLUSIVE or UNVERIFIABLE.
  verdict        VARCHAR(16) NOT NULL,
  verdict_reason VARCHAR(256),
  diverged_link  INT,
  diverged_frame BIGINT,
  -- What the player paid for the replay, and what is left to give back. A verdict that says nothing
  -- about them, a crash here, a queue that ran out of week, returns it; a divergence does not.
  fee_paid       INT         NOT NULL,
  fee_refunded   INT         NOT NULL,
  settled_at     TIMESTAMP
);

CREATE INDEX import_chains_by_character ON import_chains (character_id, uploaded_at);

-- One offline session. The recording is the session: a few bytes a button, a couple of hundred
-- kilobytes an hour of play, and the thing that is replayed.

CREATE TABLE import_chain_links (
  chain_id         BIGINT      NOT NULL REFERENCES import_chains (id) ON DELETE CASCADE,
  ordinal          INT         NOT NULL,
  -- Which build played it.
  revision         INT,
  rtc              TIMESTAMP   NOT NULL,
  boot_sha256      VARCHAR(64),
  recording_sha256 VARCHAR(64),
  quit_sha256      VARCHAR(64),
  -- Absent when the session ended by crash.
  end_frame        BIGINT,
  recording        BYTEA       NOT NULL,
  PRIMARY KEY (chain_id, ordinal)
);
