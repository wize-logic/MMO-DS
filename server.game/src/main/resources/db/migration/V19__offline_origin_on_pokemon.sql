-- Where a monster came from, for the one question the market and the ladder have to ask.

-- A save file is bytes on the player's disk. Any key the client could sign one with ships inside
-- the client, and a save edited to obey every rule the game itself obeys reads exactly like an
-- honest one.

-- The mark is not an accusation and it costs the player nothing they had offline: a marked monster
-- battles, evolves, holds an item and sits in the party like any other.

-- Every row that exists was earned on this server's dice, so they all start false.

ALTER TABLE pokemon
  ADD COLUMN offline_origin BOOLEAN NOT NULL DEFAULT FALSE;

-- The shelf keeps a listed monster column for column and rebuilds the record from those columns
-- when the listing sells or is taken back, so a column it does not have is a field the monster
-- comes back without.

ALTER TABLE gtl_listings
  ADD COLUMN mon_offline_origin BOOLEAN;
