-- The save blocks the client owns the working copy of and this server records.

CREATE TABLE character_save_block (
  character_id BIGINT   NOT NULL REFERENCES characters (id) ON DELETE CASCADE,
  block_id     SMALLINT NOT NULL,
  data         BYTEA    NOT NULL,
  PRIMARY KEY (character_id, block_id)
);
