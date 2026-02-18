-- v13: Add PNIs to recipient table and merge profile keys
ALTER TABLE signalmeow_contacts DROP CONSTRAINT signalmeow_contacts_pkey;
ALTER TABLE signalmeow_contacts RENAME TO signalmeow_recipients;
ALTER TABLE signalmeow_recipients ADD COLUMN pni_uuid TEXT;
ALTER TABLE signalmeow_recipients ALTER COLUMN aci_uuid DROP NOT NULL;
ALTER TABLE signalmeow_recipients ADD CONSTRAINT signalmeow_contacts_aci_unique UNIQUE (account_id, aci_uuid);
ALTER TABLE signalmeow_recipients ADD CONSTRAINT signalmeow_contacts_pni_unique UNIQUE (account_id, pni_uuid);

ALTER TABLE signalmeow_recipients ALTER COLUMN e164_number SET DEFAULT '';
ALTER TABLE signalmeow_recipients ALTER COLUMN contact_name SET DEFAULT '';
ALTER TABLE signalmeow_recipients ALTER COLUMN contact_avatar_hash SET DEFAULT '';
ALTER TABLE signalmeow_recipients ALTER COLUMN profile_name SET DEFAULT '';
ALTER TABLE signalmeow_recipients ALTER COLUMN profile_about SET DEFAULT '';
ALTER TABLE signalmeow_recipients ALTER COLUMN profile_about_emoji SET DEFAULT '';
ALTER TABLE signalmeow_recipients ALTER COLUMN profile_avatar_path SET DEFAULT '';

INSERT INTO signalmeow_recipients (account_id, aci_uuid, profile_key)
SELECT account_id, their_aci_uuid, key
FROM signalmeow_profile_keys
ON CONFLICT (account_id, aci_uuid) DO UPDATE SET profile_key=excluded.profile_key;

DROP TABLE signalmeow_profile_keys;
