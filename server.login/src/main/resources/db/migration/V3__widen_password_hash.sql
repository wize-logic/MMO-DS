-- The column held an unsalted SHA-1 hex, which is exactly what the client sends, so the row was
-- the credential: reading the table was logging in as everybody.
ALTER TABLE users
  ALTER COLUMN password_hash TYPE VARCHAR(255);
