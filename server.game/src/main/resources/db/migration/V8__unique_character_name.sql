-- One name, one player. A name is how the world addresses a character, chat, whispers, the trade
-- window, the friend list, so two characters answering to it makes every one of those ambiguous.
-- [jooq ignore start]
DO $$
DECLARE
  dup       RECORD;
  candidate TEXT;
  n         INT;
BEGIN
  FOR dup IN
    SELECT id, name
    FROM (
      SELECT id,
             name,
             row_number() OVER (PARTITION BY lower(name) ORDER BY created_at, id) AS seniority
      FROM characters
    ) ranked
    WHERE seniority > 1
    ORDER BY id
  LOOP
    n := 2;
    LOOP
      candidate := left(dup.name, 32 - length(n::text) - 1) || '_' || n;
      EXIT WHEN NOT EXISTS (SELECT 1 FROM characters WHERE lower(name) = lower(candidate));
      n := n + 1;
    END LOOP;
    UPDATE characters SET name = candidate WHERE id = dup.id;
    RAISE NOTICE 'character % renamed to % to free the duplicate name %', dup.id, candidate, dup.name;
  END LOOP;
END $$;

DROP INDEX idx_characters_name;
CREATE UNIQUE INDEX idx_characters_name ON characters (lower(name));
-- [jooq ignore stop]
