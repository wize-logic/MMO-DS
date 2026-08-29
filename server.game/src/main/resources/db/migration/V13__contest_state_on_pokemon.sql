-- The contest half of a monster.

ALTER TABLE pokemon
  ADD COLUMN cond_cool             SMALLINT NOT NULL DEFAULT 0,
  ADD COLUMN cond_beauty           SMALLINT NOT NULL DEFAULT 0,
  ADD COLUMN cond_cute             SMALLINT NOT NULL DEFAULT 0,
  ADD COLUMN cond_smart            SMALLINT NOT NULL DEFAULT 0,
  ADD COLUMN cond_tough            SMALLINT NOT NULL DEFAULT 0,
  ADD COLUMN sheen                 SMALLINT NOT NULL DEFAULT 0,
  ADD COLUMN super_contest_ribbons BIGINT   NOT NULL DEFAULT 0;
