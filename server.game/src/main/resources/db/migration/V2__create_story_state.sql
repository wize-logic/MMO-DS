-- Per character story progression. A flag is a boolean (a row means the flag is set) and a var is
-- a named integer.

CREATE TABLE character_flags (
  character_id BIGINT       NOT NULL REFERENCES characters (id) ON DELETE CASCADE,
  flag_key     VARCHAR(128) NOT NULL,
  PRIMARY KEY (character_id, flag_key)
);

CREATE TABLE character_vars (
  character_id BIGINT       NOT NULL REFERENCES characters (id) ON DELETE CASCADE,
  var_key      VARCHAR(128) NOT NULL,
  var_value    INT          NOT NULL,
  PRIMARY KEY (character_id, var_key)
);
