-- v21 (compatible with v13+): Add event buffer
CREATE TABLE signalmeow_event_buffer (
    account_id       TEXT   NOT NULL,
    ciphertext_hash  bytea  NOT NULL,
    plaintext        bytea,
    server_timestamp BIGINT NOT NULL,
    insert_timestamp BIGINT NOT NULL,

    PRIMARY KEY (account_id, ciphertext_hash),
    FOREIGN KEY (account_id) REFERENCES signalmeow_device (aci_uuid) ON DELETE CASCADE ON UPDATE CASCADE
);
