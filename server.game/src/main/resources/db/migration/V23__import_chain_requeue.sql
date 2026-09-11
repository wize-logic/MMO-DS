-- A settled chain put back in the queue by a moderator, once.

-- The worker never retries on its own: a divergence is the engine's word that these inputs did not
-- produce that save, and a crash or a queue that ran out of week says nothing about the player and
-- comes back on its own the next time they ask.

ALTER TABLE import_chains
  ADD COLUMN requeued_at TIMESTAMP,
  ADD COLUMN requeued_by VARCHAR(64);
