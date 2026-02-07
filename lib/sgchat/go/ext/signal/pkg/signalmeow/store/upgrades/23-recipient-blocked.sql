-- v23 (compatible with v13+): Store block status for recipients
ALTER TABLE signalmeow_recipients ADD COLUMN blocked BOOLEAN NOT NULL DEFAULT false;
