-- What an account may do beyond playing, as AccountRole bits. 0 is an ordinary player.
-- Roles sit on the account rather than on a character, so starting a new character keeps them.
ALTER TABLE users ADD COLUMN roles INT NOT NULL DEFAULT 0;
