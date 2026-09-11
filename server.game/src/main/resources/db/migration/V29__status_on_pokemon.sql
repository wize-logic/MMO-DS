-- What a monster is suffering from.

-- Poison, a burn, paralysis, sleep and a freeze are all the client engine's: its battle sets them,
-- its Centers and its Full Heals clear them, and a poisoned party pays a hit point every four
-- steps of the field's own step chain.

-- The engine's own condition word, kept as the number it is.

-- 0 is a healthy monster, which is what every existing row really is: nothing has been able to
-- carry a condition for longer than a session.

ALTER TABLE pokemon
  ADD COLUMN status SMALLINT NOT NULL DEFAULT 0;

-- The shelf keeps a listed monster column for column and rebuilds the record from those columns
-- when the listing sells or is taken back, so a column it does not have is a field the monster
-- comes back without.

ALTER TABLE gtl_listings
  ADD COLUMN mon_status SMALLINT;
