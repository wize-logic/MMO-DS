-- Which of a character's items came out of a save file, so a mart will not turn them into money.

-- Money does not cross the import door at all: a wallet is earned in the world it is spent in, and
-- a coin from an edited file would otherwise buy a monster somebody else earned.

-- Dropping them at the door would have been simpler and worse: an item a player earned offline is
-- theirs, and a Life Orb, an Everstone or a Rare Candy is worth having for what it does.

-- Replaced whole on every import, because an import replaces the whole character; and read back
-- clamped to what the bag actually holds, so an item spent, tossed or used leaves the mark behind
-- with it rather than making a later stack unsellable.

CREATE TABLE character_offline_items (
  character_id BIGINT NOT NULL REFERENCES characters (id) ON DELETE CASCADE,
  item_id      INT    NOT NULL,
  quantity     INT    NOT NULL,
  PRIMARY KEY (character_id, item_id)
);
