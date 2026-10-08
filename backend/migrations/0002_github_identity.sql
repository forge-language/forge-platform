ALTER TABLE packages ADD COLUMN owner_id text CHECK (owner_id ~ '^[1-9][0-9]{0,19}$');
