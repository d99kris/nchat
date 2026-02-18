-- v7 -> v8: Migration from https://github.com/mautrix/signal/pull/449 to match the new v8 upgrade
ALTER TABLE signalmeow_contacts DROP COLUMN profile_avatar_hash;
ALTER TABLE signalmeow_contacts RENAME COLUMN profile_fetch_ts TO profile_fetched_at;
ALTER TABLE signalmeow_contacts ALTER COLUMN profile_fetched_at DROP DEFAULT;
ALTER TABLE signalmeow_contacts ALTER COLUMN profile_fetched_at DROP NOT NULL;
UPDATE signalmeow_contacts SET profile_fetched_at = NULL WHERE profile_fetched_at <= 0;
ALTER TABLE signalmeow_contacts ALTER COLUMN e164_number SET NOT NULL;
ALTER TABLE signalmeow_contacts ALTER COLUMN contact_name SET NOT NULL;
ALTER TABLE signalmeow_contacts ALTER COLUMN contact_avatar_hash SET NOT NULL;
ALTER TABLE signalmeow_contacts ALTER COLUMN profile_name SET NOT NULL;
ALTER TABLE signalmeow_contacts ALTER COLUMN profile_about SET NOT NULL;
ALTER TABLE signalmeow_contacts ALTER COLUMN profile_about_emoji SET NOT NULL;
