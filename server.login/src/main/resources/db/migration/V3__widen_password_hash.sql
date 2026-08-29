-- The column held an unsalted SHA-1, which is exactly what the client sends, so the row was the
-- credential: reading the table was logging in as everybody. It now holds PBKDF2 over what the
-- client sends, salted per row and carrying its own parameters, which needs the room.
--
-- Widening only. Rows written before this are still accepted; the server rewrites each one the
-- first time it is used and sweeps the rest at startup.
ALTER TABLE users
  ALTER COLUMN password_hash TYPE VARCHAR(255);
