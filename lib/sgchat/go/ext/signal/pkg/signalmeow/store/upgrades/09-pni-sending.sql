-- v9: Add support for sending to PNIs
ALTER TABLE signalmeow_sessions RENAME COLUMN their_aci_uuid TO their_service_id;
ALTER TABLE signalmeow_identity_keys RENAME COLUMN their_aci_uuid TO their_service_id;
