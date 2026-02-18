-- v6 -> v8: Add profile_fetched_at and make other columns not null
ALTER TABLE signalmeow_contacts DROP COLUMN profile_avatar_hash;
ALTER TABLE signalmeow_contacts ADD COLUMN profile_fetched_at BIGINT;
-- only: postgres until "end only"
ALTER TABLE signalmeow_contacts ALTER COLUMN e164_number SET NOT NULL;
ALTER TABLE signalmeow_contacts ALTER COLUMN contact_name SET NOT NULL;
ALTER TABLE signalmeow_contacts ALTER COLUMN contact_avatar_hash SET NOT NULL;
ALTER TABLE signalmeow_contacts ALTER COLUMN profile_name SET NOT NULL;
ALTER TABLE signalmeow_contacts ALTER COLUMN profile_about SET NOT NULL;
ALTER TABLE signalmeow_contacts ALTER COLUMN profile_about_emoji SET NOT NULL;
-- end only postgres
