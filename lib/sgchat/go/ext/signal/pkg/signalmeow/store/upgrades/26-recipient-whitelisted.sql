-- v26 (compatible with v13+): Store whitelisted status for recipients
ALTER TABLE signalmeow_recipients ADD COLUMN whitelisted BOOLEAN;
