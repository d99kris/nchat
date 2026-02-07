-- v11: Rename our_aci_uuid columns to account_id
ALTER TABLE signalmeow_identity_keys RENAME COLUMN our_aci_uuid TO account_id;
ALTER TABLE signalmeow_profile_keys RENAME COLUMN our_aci_uuid TO account_id;
ALTER TABLE signalmeow_sender_keys RENAME COLUMN our_aci_uuid TO account_id;
ALTER TABLE signalmeow_groups RENAME COLUMN our_aci_uuid TO account_id;
ALTER TABLE signalmeow_contacts RENAME COLUMN our_aci_uuid TO account_id;
