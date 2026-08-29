-- Remember me used to be a stateless HMAC signed with the same secret and shape as the short lived
-- ticket the game server verifies, so one verified as the other, the game server's key could mint
-- thirty day credentials, and nothing could withdraw one.
--
-- A row instead of a signature. The token is random, the server keeps only its hash, and revoking
-- one is deleting it.
CREATE TABLE remember_me_tokens (
  id         INT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  user_id    INT       NOT NULL REFERENCES users (id) ON DELETE CASCADE,
  -- SHA-256 hex of the bytes the client holds. The token is 256 bits of randomness rather than a
  -- password, so it needs no stretching; what matters is that the column is not the credential.
  token_hash CHAR(64)  NOT NULL,
  issued_at  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  expires_at TIMESTAMP NOT NULL,
  CONSTRAINT uq_remember_me_token_hash UNIQUE (token_hash)
);

CREATE INDEX idx_remember_me_tokens_user ON remember_me_tokens (user_id);
