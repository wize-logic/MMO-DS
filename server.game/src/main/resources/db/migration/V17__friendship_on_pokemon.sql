-- How much a monster likes its trainer.

-- The client engine runs the game's own friendship rules already: walking with a party raises it
-- every 128 steps, a level up raises it more, fainting and surviving poison lower it.

-- What it buys is worth the column.

-- Existing rows start where the great majority of species start rather than at 0.

ALTER TABLE pokemon
  ADD COLUMN friendship SMALLINT NOT NULL DEFAULT 70;
