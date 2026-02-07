-- v18 (compatible with v13+): Store account entropy pool and ephemeral backup keys
ALTER TABLE signalmeow_device ADD COLUMN account_entropy_pool TEXT;
ALTER TABLE signalmeow_device ADD COLUMN ephemeral_backup_key bytea;
ALTER TABLE signalmeow_device ADD COLUMN media_root_backup_key bytea;
