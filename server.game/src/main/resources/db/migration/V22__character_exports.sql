-- The image a session wrote out for offline play, kept so the play after it can be checked from a
-- point this server trusts.

-- "Continue Offline" is the client running the engine's own save out to a file, so until now this
-- server had never held a cartridge image, and the only anchor a chain of offline sessions could
-- start from was a New Game.

CREATE TABLE character_exports (
  id             BIGINT      NOT NULL PRIMARY KEY,
  character_id   BIGINT      NOT NULL REFERENCES characters (id) ON DELETE CASCADE,
  exported_at    TIMESTAMP   NOT NULL,
  -- Of the image as it arrived. The client's own session records name their anchor by the hash of
  -- the file the front door adopted, and the two only ever meet when they are the same bytes.
  sha256         VARCHAR(64) NOT NULL,
  -- The whole backup chip, half a megabyte, because the first session after this export is
  -- replayed from it. A character keeps its newest few; older ones go as new ones arrive.
  image          BYTEA       NOT NULL,
  -- PENDING until it has been booted; then CHECKED, MISMATCH or UNCHECKED. Only CHECKED anchors.
  verdict        VARCHAR(16) NOT NULL,
  verdict_reason VARCHAR(256),
  checked_at     TIMESTAMP
);

CREATE INDEX character_exports_by_character ON character_exports (character_id, exported_at);
