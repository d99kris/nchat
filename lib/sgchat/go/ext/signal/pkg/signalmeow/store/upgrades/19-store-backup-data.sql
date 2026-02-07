-- v19 (compatible with v13+): Add tables for caching parsed backup data
CREATE TABLE signalmeow_backup_recipient (
    account_id       TEXT   NOT NULL,
    recipient_id     BIGINT NOT NULL,

    aci_uuid         TEXT,
    pni_uuid         TEXT,

    group_master_key TEXT,

    data             bytea  NOT NULL,

    PRIMARY KEY (account_id, recipient_id),
    CONSTRAINT signalmeow_backup_recipient_device_fkey FOREIGN KEY (account_id)
        REFERENCES signalmeow_device (aci_uuid) ON DELETE CASCADE ON UPDATE CASCADE
);
CREATE INDEX signalmeow_backup_recipient_group_idx ON signalmeow_backup_recipient (account_id, group_master_key);
CREATE INDEX signalmeow_backup_recipient_aci_idx ON signalmeow_backup_recipient (account_id, aci_uuid);

CREATE TABLE signalmeow_backup_chat (
    account_id   TEXT   NOT NULL,
    chat_id      BIGINT NOT NULL,
    recipient_id BIGINT NOT NULL,
    data         bytea  NOT NULL,

    latest_message_id   BIGINT,
    total_message_count INTEGER,

    PRIMARY KEY (account_id, chat_id),
    CONSTRAINT signalmeow_backup_chat_device_fkey FOREIGN KEY (account_id)
        REFERENCES signalmeow_device (aci_uuid) ON DELETE CASCADE ON UPDATE CASCADE,
    CONSTRAINT signalmeow_backup_chat_recipient_fkey FOREIGN KEY (account_id, recipient_id)
        REFERENCES signalmeow_backup_recipient (account_id, recipient_id) ON DELETE CASCADE ON UPDATE CASCADE
);
CREATE INDEX signalmeow_backup_chat_recipient_id_idx ON signalmeow_backup_chat (account_id, recipient_id);

CREATE TABLE signalmeow_backup_message (
    account_id TEXT   NOT NULL,
    chat_id    BIGINT NOT NULL,
    sender_id  BIGINT NOT NULL,
    message_id BIGINT NOT NULL,
    data       bytea  NOT NULL,

    PRIMARY KEY (account_id, sender_id, message_id),
    CONSTRAINT signalmeow_backup_message_chat_fkey FOREIGN KEY (account_id, chat_id)
        REFERENCES signalmeow_backup_chat (account_id, chat_id) ON DELETE CASCADE ON UPDATE CASCADE,
    CONSTRAINT signalmeow_backup_message_sender_fkey FOREIGN KEY (account_id, sender_id)
        REFERENCES signalmeow_backup_recipient (account_id, recipient_id) ON DELETE CASCADE ON UPDATE CASCADE,
    CONSTRAINT signalmeow_backup_message_device_fkey FOREIGN KEY (account_id)
        REFERENCES signalmeow_device (aci_uuid) ON DELETE CASCADE ON UPDATE CASCADE
);
CREATE INDEX signalmeow_backup_message_chat_id_idx ON signalmeow_backup_message (account_id, chat_id);
