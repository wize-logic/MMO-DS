-- Which forme a monster is wearing.

-- The record has carried a form all along and nothing has ever written it down.

-- It is not only a picture.

-- 0 is the ordinary forme, which is what every existing row really is: nothing has ever been able
-- to become anything else for longer than a session.

ALTER TABLE pokemon
  ADD COLUMN form SMALLINT NOT NULL DEFAULT 0;

-- The shelf keeps a listed monster column for column and rebuilds the record from those columns
-- when the listing sells or is taken back, so a column it does not have is a field the monster
-- comes back without.

ALTER TABLE gtl_listings
  ADD COLUMN mon_form SMALLINT;
