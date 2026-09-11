-- Remember me is a row in remember_me_tokens now, not a signature, so revoking one is deleting it
-- (V4).
ALTER TABLE users
  DROP COLUMN token_epoch;
