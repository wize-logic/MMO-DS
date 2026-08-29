-- The dev seed no longer creates accounts.
DELETE FROM users
WHERE (username = 'admin' AND password_hash = 'd033e22ae348aeb5660fc2140aec35850c4da997')
   OR (username = 'test' AND password_hash = 'a94a8fe5ccb19ba61c4c0873d391e987982fbbd3');
