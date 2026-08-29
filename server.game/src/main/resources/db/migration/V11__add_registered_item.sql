-- The Y-registered key item. It lived only in client RAM, so registering the
-- fishing rod lasted exactly one session. 0 is ITEM_NONE.
ALTER TABLE characters ADD COLUMN registered_item SMALLINT NOT NULL DEFAULT 0;
