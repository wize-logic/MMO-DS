-- The dev seed no longer creates characters either.
DELETE FROM characters
WHERE user_id IN (1, 2)
  AND name IN ('Kanto', 'Hoenn', 'Sinnoh', 'Lauren', 'Nathan');
