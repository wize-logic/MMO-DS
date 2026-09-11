-- Every PC a character has stood at, and where: the tile they used it from, so that a later trip
-- back lands them in front of the same machine.

CREATE TABLE character_pcs (
  character_id BIGINT    NOT NULL REFERENCES characters (id) ON DELETE CASCADE,
  region       INT       NOT NULL,
  bank         INT       NOT NULL,
  map          INT       NOT NULL,
  x            INT       NOT NULL,
  y            INT       NOT NULL,
  elevation    INT       NOT NULL DEFAULT 0,
  last_used    TIMESTAMP NOT NULL,
  PRIMARY KEY (character_id, region, bank, map)
);
