-- v6 (compatible with v5+): Save profile avatar path
ALTER TABLE signalmeow_contacts ADD COLUMN profile_avatar_path TEXT NOT NULL DEFAULT '';
