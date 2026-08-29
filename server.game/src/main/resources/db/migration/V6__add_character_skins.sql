-- The appearance picked in the character creator. One row per occupied slot, keyed by the SkinSlot
-- enum name.

CREATE TABLE character_skins (
  character_id BIGINT      NOT NULL REFERENCES characters (id) ON DELETE CASCADE,
  slot         VARCHAR(16) NOT NULL,
  skin_type    SMALLINT,
  skin_color   SMALLINT,
  PRIMARY KEY (character_id, slot)
);

ALTER TABLE characters
  ADD COLUMN skin_region_selection_index SMALLINT NOT NULL DEFAULT 0;
