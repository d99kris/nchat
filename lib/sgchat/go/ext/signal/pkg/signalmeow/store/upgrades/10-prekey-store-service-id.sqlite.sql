-- v10: Change prekey store to use service IDs instead of UUID kind column
CREATE TABLE new_signalmeow_pre_keys (
    account_id TEXT    NOT NULL,
    service_id TEXT    NOT NULL,
    key_id     INTEGER NOT NULL,
    is_signed  BOOLEAN NOT NULL,
    key_pair   bytea   NOT NULL,

    PRIMARY KEY (account_id, service_id, is_signed, key_id),
    FOREIGN KEY (account_id) REFERENCES signalmeow_device (aci_uuid) ON DELETE CASCADE ON UPDATE CASCADE
);

INSERT INTO new_signalmeow_pre_keys (account_id, service_id, key_id, is_signed, key_pair)
SELECT aci_uuid, CASE WHEN uuid_kind='pni' THEN 'PNI:'||(
    SELECT pni_uuid FROM signalmeow_device WHERE signalmeow_device.aci_uuid=signalmeow_pre_keys.aci_uuid
) ELSE aci_uuid END, key_id, is_signed, key_pair
FROM signalmeow_pre_keys;

DROP TABLE signalmeow_pre_keys;
ALTER TABLE new_signalmeow_pre_keys RENAME TO signalmeow_pre_keys;


CREATE TABLE new_signalmeow_kyber_pre_keys (
    account_id     TEXT    NOT NULL,
    service_id     TEXT    NOT NULL,
    key_id         INTEGER NOT NULL,
    key_pair       bytea   NOT NULL,
    is_last_resort BOOLEAN NOT NULL,

    PRIMARY KEY (account_id, service_id, key_id),
    FOREIGN KEY (account_id) REFERENCES signalmeow_device (aci_uuid) ON DELETE CASCADE ON UPDATE CASCADE
);

INSERT INTO new_signalmeow_kyber_pre_keys (account_id, service_id, key_id, key_pair, is_last_resort)
SELECT aci_uuid, CASE WHEN uuid_kind='pni' THEN 'PNI:'||(
    SELECT pni_uuid FROM signalmeow_device WHERE signalmeow_device.aci_uuid=signalmeow_kyber_pre_keys.aci_uuid
) ELSE aci_uuid END, key_id, key_pair, is_last_resort
FROM signalmeow_kyber_pre_keys;

DROP TABLE signalmeow_kyber_pre_keys;
ALTER TABLE new_signalmeow_kyber_pre_keys RENAME TO signalmeow_kyber_pre_keys;


CREATE TABLE new_signalmeow_sessions (
    account_id       TEXT    NOT NULL,
    service_id       TEXT    NOT NULL,
    their_service_id TEXT    NOT NULL,
    their_device_id  INTEGER NOT NULL,
    record           bytea   NOT NULL,

    PRIMARY KEY (account_id, service_id, their_service_id, their_device_id),
    FOREIGN KEY (account_id) REFERENCES signalmeow_device (aci_uuid) ON DELETE CASCADE ON UPDATE CASCADE
);
INSERT INTO new_signalmeow_sessions (account_id, service_id, their_service_id, their_device_id, record)
SELECT our_aci_uuid, our_aci_uuid, their_service_id, their_device_id, record
FROM signalmeow_sessions;

DROP TABLE signalmeow_sessions;
ALTER TABLE new_signalmeow_sessions RENAME TO signalmeow_sessions;
