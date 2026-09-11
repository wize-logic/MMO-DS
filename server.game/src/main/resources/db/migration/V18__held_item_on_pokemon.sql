-- What a monster is carrying.

-- The party menu's GIVE and TAKE move an item between the bag and a monster, and the bag half was
-- already recorded while the monster half had nowhere to go: an item handed over was gone from the
-- bag and on nothing, and the next join handed back a monster carrying air.

-- Half of the game's own mechanics read this column. An Everstone stops a monster evolving, a
-- Razor Claw or an Electirizer is the entire trigger of one, Leftovers heal a little every turn
-- and an Exp.

-- The value is a wire item id, the same numbering the bag and the shop use, and 0 is nothing held.
-- Every existing row starts at 0, which is what they have all really been holding.

ALTER TABLE pokemon
  ADD COLUMN held_item_id INT NOT NULL DEFAULT 0;

-- The escrow keeps a monster column for column while a listing is up, and it rebuilds the record
-- from those columns when the listing is bought or taken back.

ALTER TABLE gtl_listings
  ADD COLUMN mon_held_item_id INT;
