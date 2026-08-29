-- Where a white out sends the character back to, set by entering a map that carries a respawn
-- (the decomp setrespawn). All null means the character has never set one.

ALTER TABLE characters
  ADD COLUMN last_heal_region SMALLINT,
  ADD COLUMN last_heal_bank   SMALLINT,
  ADD COLUMN last_heal_map    SMALLINT,
  ADD COLUMN last_heal_x      SMALLINT,
  ADD COLUMN last_heal_y      SMALLINT;
