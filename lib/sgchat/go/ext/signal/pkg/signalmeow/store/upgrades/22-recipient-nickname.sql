-- v22 (compatible with v13+): Store Signal-specific nickname of contacts
ALTER TABLE signalmeow_recipients ADD COLUMN nickname TEXT NOT NULL DEFAULT '';
