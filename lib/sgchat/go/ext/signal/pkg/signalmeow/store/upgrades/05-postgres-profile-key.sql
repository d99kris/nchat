-- v5: Fix profile key column type on postgres
-- only: postgres
ALTER TABLE signalmeow_contacts ALTER COLUMN profile_key TYPE bytea USING profile_key::bytea;
UPDATE signalmeow_contacts SET profile_key=key FROM signalmeow_profile_keys WHERE signalmeow_contacts.aci_uuid=signalmeow_profile_keys.their_aci_uuid;
