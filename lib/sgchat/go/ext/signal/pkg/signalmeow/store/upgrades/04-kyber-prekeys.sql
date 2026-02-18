-- v4: Add kyber prekeys table
CREATE TABLE signalmeow_kyber_pre_keys (
    aci_uuid       TEXT    NOT NULL,
    key_id         INTEGER NOT NULL,
    uuid_kind      TEXT    NOT NULL,
    key_pair       bytea   NOT NULL,
    is_last_resort BOOLEAN NOT NULL,

    PRIMARY KEY (aci_uuid, uuid_kind, key_id),
    FOREIGN KEY (aci_uuid) REFERENCES signalmeow_device (aci_uuid) ON DELETE CASCADE ON UPDATE CASCADE
);
