-- Where a monster was caught.

ALTER TABLE pokemon
  ADD COLUMN caught_region_id SMALLINT NOT NULL DEFAULT -1,
  ADD COLUMN caught_bank_id   SMALLINT NOT NULL DEFAULT -1,
  ADD COLUMN caught_map_id    SMALLINT NOT NULL DEFAULT -1;
