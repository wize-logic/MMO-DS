-- What a save file did to a character, kept so it can be read back and undone.

-- An import replaces a whole character at once: party, boxes, bag, money, story and the engine
-- blocks.

-- The rest of the row is what a person needs to read a pattern without opening the blob.

CREATE TABLE character_imports (
  id                BIGINT       NOT NULL PRIMARY KEY,
  character_id      BIGINT       NOT NULL REFERENCES characters (id) ON DELETE CASCADE,
  imported_at       TIMESTAMP    NOT NULL,
  -- The save's own play-time counter. A signal, never a gate: the offline clock is the player's.
  play_time_seconds INT          NOT NULL,
  -- The client's hash of the file it read. It is the client's word and nothing is decided on it;
  -- what it buys is recognising the same file arriving twice.
  save_sha256       VARCHAR(64)  NOT NULL,
  client_revision   INT          NOT NULL,
  -- The save's own trainer id pair.
  trainer_id        INT          NOT NULL,
  party_count       INT          NOT NULL,
  box_count         INT          NOT NULL,
  species_count     INT          NOT NULL,
  level_total       INT          NOT NULL,
  level_max         INT          NOT NULL,
  money_before      INT          NOT NULL,
  money_after       INT          NOT NULL,
  badges_before     INT          NOT NULL,
  badges_after      INT          NOT NULL,
  -- Every clamp, drop and allow-and-log the door made, one to a line. The same sentences the
  -- player was shown, so the two never disagree.
  verdicts          TEXT         NOT NULL,
  -- Filled by a replayed session, which is the only thing that can clear the provenance mark.
  replay_verdict    VARCHAR(16),
  replay_frame      BIGINT,
  -- The character as it was, immediately before the replacement.
  snapshot          BYTEA        NOT NULL,
  snapshot_version  INT          NOT NULL,
  rolled_back_at    TIMESTAMP,
  rolled_back_by    VARCHAR(64),
  -- Set the moment something leaves the character: a trade settling, a monster going onto the
  -- shelf.
  sealed_at         TIMESTAMP,
  sealed_reason     VARCHAR(64)
);

CREATE INDEX character_imports_by_character ON character_imports (character_id, imported_at);

-- The refusals, on disk.

-- The counters this mirrors live in memory and turn over in ten minutes, which is the right window
-- for "is this session doing it right now" and the wrong one for the only question anybody asks
-- later: has this account done it before.

CREATE TABLE violations (
  character_id BIGINT       NOT NULL REFERENCES characters (id) ON DELETE CASCADE,
  kind         VARCHAR(32)  NOT NULL,
  total        BIGINT       NOT NULL,
  first_at     TIMESTAMP    NOT NULL,
  last_at      TIMESTAMP    NOT NULL,
  last_detail  VARCHAR(256) NOT NULL,
  PRIMARY KEY (character_id, kind)
);
