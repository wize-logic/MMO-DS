-- The engine's own location label for where a monster was caught.

-- Not a second copy of the region/bank/map above it.

-- 0 is the label the game itself draws as "Mystery Zone", so it is both the honest default for a
-- row that predates this column and the value that changes nothing about what those rows show.

ALTER TABLE pokemon
  ADD COLUMN caught_location_label SMALLINT NOT NULL DEFAULT 0;
