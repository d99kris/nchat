-- v12: Drop their_device_id column in signalmeow_identity_keys table
DELETE FROM signalmeow_identity_keys WHERE their_device_id<>1;
ALTER TABLE signalmeow_identity_keys DROP CONSTRAINT signalmeow_identity_keys_pkey;
ALTER TABLE signalmeow_identity_keys DROP COLUMN their_device_id;
ALTER TABLE signalmeow_identity_keys ADD PRIMARY KEY (account_id, their_service_id);
