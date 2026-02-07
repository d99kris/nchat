-- v14 (compatible with v13+): Save storage master key for devices
ALTER TABLE signalmeow_device ADD COLUMN master_key bytea;
