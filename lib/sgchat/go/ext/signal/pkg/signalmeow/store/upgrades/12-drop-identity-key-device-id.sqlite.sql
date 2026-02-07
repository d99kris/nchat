-- v12: Drop their_device_id column in signalmeow_identity_keys table
CREATE TABLE new_signalmeow_identity_keys (
    account_id       TEXT    NOT NULL,
    their_service_id TEXT    NOT NULL,
    key              bytea   NOT NULL,
    trust_level      TEXT    NOT NULL,

    PRIMARY KEY (account_id, their_service_id),
    FOREIGN KEY (account_id) REFERENCES signalmeow_device (aci_uuid) ON DELETE CASCADE ON UPDATE CASCADE
);

INSERT INTO new_signalmeow_identity_keys (account_id, their_service_id, key, trust_level)
SELECT account_id, their_service_id, key, trust_level
FROM signalmeow_identity_keys
WHERE their_device_id=1;

DROP TABLE signalmeow_identity_keys;

ALTER TABLE new_signalmeow_identity_keys RENAME TO signalmeow_identity_keys;
