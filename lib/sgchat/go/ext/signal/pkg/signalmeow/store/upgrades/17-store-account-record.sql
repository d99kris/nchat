-- v17 (compatible with v13+): Store account config
ALTER TABLE signalmeow_device ADD COLUMN account_record bytea;
