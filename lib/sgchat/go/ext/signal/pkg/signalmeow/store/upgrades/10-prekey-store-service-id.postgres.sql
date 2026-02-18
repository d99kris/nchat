-- v10: Change prekey store to use service IDs instead of UUID kind column
ALTER TABLE signalmeow_pre_keys ADD COLUMN service_id TEXT;
UPDATE signalmeow_pre_keys SET service_id=aci_uuid WHERE uuid_kind='aci';
UPDATE signalmeow_pre_keys SET service_id='PNI:' || (
    SELECT pni_uuid FROM signalmeow_device WHERE signalmeow_device.aci_uuid=signalmeow_pre_keys.aci_uuid
) WHERE uuid_kind='pni';
ALTER TABLE signalmeow_pre_keys ALTER COLUMN service_id SET NOT NULL;
ALTER TABLE signalmeow_pre_keys DROP CONSTRAINT signalmeow_pre_keys_pkey;
ALTER TABLE signalmeow_pre_keys DROP COLUMN uuid_kind;
ALTER TABLE signalmeow_pre_keys RENAME COLUMN aci_uuid TO account_id;
ALTER TABLE signalmeow_pre_keys ADD PRIMARY KEY (account_id, service_id, is_signed, key_id);

ALTER TABLE signalmeow_pre_keys DROP COLUMN uploaded;

ALTER TABLE signalmeow_kyber_pre_keys ADD COLUMN service_id TEXT;
UPDATE signalmeow_kyber_pre_keys SET service_id=aci_uuid WHERE uuid_kind='aci';
UPDATE signalmeow_kyber_pre_keys SET service_id='PNI:' || (
    SELECT pni_uuid FROM signalmeow_device WHERE signalmeow_device.aci_uuid=signalmeow_kyber_pre_keys.aci_uuid
) WHERE uuid_kind='pni';
ALTER TABLE signalmeow_kyber_pre_keys ALTER COLUMN service_id SET NOT NULL;
ALTER TABLE signalmeow_kyber_pre_keys DROP CONSTRAINT signalmeow_kyber_pre_keys_pkey;
ALTER TABLE signalmeow_kyber_pre_keys DROP COLUMN uuid_kind;
ALTER TABLE signalmeow_kyber_pre_keys RENAME COLUMN aci_uuid TO account_id;
ALTER TABLE signalmeow_kyber_pre_keys ADD PRIMARY KEY (account_id, service_id, key_id);

ALTER TABLE signalmeow_sessions ADD COLUMN service_id TEXT;
UPDATE signalmeow_sessions SET service_id=our_aci_uuid; -- there are no PNI sessions yet
ALTER TABLE signalmeow_sessions ALTER COLUMN service_id SET NOT NULL;
ALTER TABLE signalmeow_sessions DROP CONSTRAINT signalmeow_sessions_pkey;
ALTER TABLE signalmeow_sessions RENAME COLUMN our_aci_uuid TO account_id;
ALTER TABLE signalmeow_sessions ADD PRIMARY KEY (account_id, service_id, their_service_id, their_device_id);
