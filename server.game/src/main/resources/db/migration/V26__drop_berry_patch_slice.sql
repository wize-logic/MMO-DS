-- The berry patches stop crossing under an id of the client's own.

-- They are a slice of the game's misc block, and they were carved out of it under 195 because
-- another writer on the client re-stated the rival's name, a field of the same block, on every
-- join, so a stored copy could never win.

-- These rows are what the slice leaves behind, and they cannot be turned into the new block: the
-- client refuses a block that is not the width its own build says, and the rest of a misc cannot
-- be invented, an all-zero rival name is not the empty one the engine writes, and an all-zero
-- extra save key names a storage area rather than saying there is none.

-- Leaving them is the thing that does not work.
DELETE FROM character_save_block WHERE block_id = 195;
