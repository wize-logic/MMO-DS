-- Everything else a monster has been given since the shelf was built.

-- The escrow copies a listed monster out of the seller's rows and rebuilds the record from these
-- columns when the listing sells or is taken back, so a column the shelf does not have is a field
-- the monster comes back without.

-- What that cost is worth more than the columns.

-- The defaults match the columns these mirror on the pokemon table, so a listing standing on the
-- shelf when this lands reads back exactly as it does today rather than gaining values it never
-- had.

ALTER TABLE gtl_listings
  ADD COLUMN mon_cond_cool             SMALLINT,
  ADD COLUMN mon_cond_beauty           SMALLINT,
  ADD COLUMN mon_cond_cute             SMALLINT,
  ADD COLUMN mon_cond_smart            SMALLINT,
  ADD COLUMN mon_cond_tough            SMALLINT,
  ADD COLUMN mon_sheen                 SMALLINT,
  ADD COLUMN mon_super_contest_ribbons BIGINT,
  ADD COLUMN mon_caught_region_id      SMALLINT,
  ADD COLUMN mon_caught_bank_id        SMALLINT,
  ADD COLUMN mon_caught_map_id         SMALLINT,
  ADD COLUMN mon_caught_location_label SMALLINT,
  ADD COLUMN mon_friendship            SMALLINT;
