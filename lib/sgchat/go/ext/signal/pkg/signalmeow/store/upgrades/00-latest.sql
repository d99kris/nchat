-- v0 -> v26 (compatible with v13+): Latest revision
CREATE TABLE signalmeow_device (
    aci_uuid              TEXT PRIMARY KEY,

    aci_identity_key_pair bytea   NOT NULL,
    registration_id       INTEGER NOT NULL CHECK ( registration_id >= 0 AND registration_id < 4294967296 ),

    pni_uuid              TEXT    NOT NULL,
    pni_identity_key_pair bytea   NOT NULL,
    pni_registration_id   INTEGER NOT NULL CHECK ( pni_registration_id >= 0 AND pni_registration_id < 4294967296 ),

    device_id             INTEGER NOT NULL,
    number                TEXT    NOT NULL DEFAULT '',
    password              TEXT    NOT NULL DEFAULT '',

    master_key            bytea,
    account_record        bytea,
    account_entropy_pool  TEXT,
    ephemeral_backup_key  bytea,
    media_root_backup_key bytea
);

CREATE TABLE signalmeow_pre_keys (
    account_id TEXT    NOT NULL,
    service_id TEXT    NOT NULL,
    key_id     INTEGER NOT NULL,
    is_signed  BOOLEAN NOT NULL,
    key_pair   bytea   NOT NULL,

    PRIMARY KEY (account_id, service_id, key_id, is_signed),
    FOREIGN KEY (account_id) REFERENCES signalmeow_device (aci_uuid) ON DELETE CASCADE ON UPDATE CASCADE
);

CREATE TABLE signalmeow_kyber_pre_keys (
    account_id     TEXT    NOT NULL,
    service_id     TEXT    NOT NULL,
    key_id         INTEGER NOT NULL,
    key_pair       bytea   NOT NULL,
    is_last_resort BOOLEAN NOT NULL,

    PRIMARY KEY (account_id, service_id, key_id),
    FOREIGN KEY (account_id) REFERENCES signalmeow_device (aci_uuid) ON DELETE CASCADE ON UPDATE CASCADE
);

CREATE TABLE signalmeow_identity_keys (
    account_id       TEXT  NOT NULL,
    their_service_id TEXT  NOT NULL,
    key              bytea NOT NULL,
    trust_level      TEXT  NOT NULL,

    PRIMARY KEY (account_id, their_service_id),
    FOREIGN KEY (account_id) REFERENCES signalmeow_device (aci_uuid) ON DELETE CASCADE ON UPDATE CASCADE
);

CREATE TABLE signalmeow_sessions (
    account_id       TEXT    NOT NULL,
    service_id       TEXT    NOT NULL,
    their_service_id TEXT    NOT NULL,
    their_device_id  INTEGER NOT NULL,
    record           bytea   NOT NULL,

    PRIMARY KEY (account_id, service_id, their_service_id, their_device_id),
    FOREIGN KEY (account_id) REFERENCES signalmeow_device (aci_uuid) ON DELETE CASCADE ON UPDATE CASCADE
);

CREATE TABLE signalmeow_event_buffer (
    account_id       TEXT   NOT NULL,
    ciphertext_hash  bytea  NOT NULL,
    plaintext        bytea,
    server_timestamp BIGINT NOT NULL,
    insert_timestamp BIGINT NOT NULL,

    PRIMARY KEY (account_id, ciphertext_hash),
    FOREIGN KEY (account_id) REFERENCES signalmeow_device (aci_uuid) ON DELETE CASCADE ON UPDATE CASCADE
);

CREATE TABLE signalmeow_profile_keys (
    account_id     TEXT  NOT NULL,
    their_aci_uuid TEXT  NOT NULL,
    key            bytea NOT NULL,

    PRIMARY KEY (account_id, their_aci_uuid),
    FOREIGN KEY (account_id) REFERENCES signalmeow_device (aci_uuid) ON DELETE CASCADE ON UPDATE CASCADE
);

CREATE TABLE signalmeow_sender_keys (
    account_id       TEXT    NOT NULL,
    sender_uuid      TEXT    NOT NULL, -- note: this may actually be a service id
    sender_device_id INTEGER NOT NULL,
    distribution_id  TEXT    NOT NULL,
    key_record       bytea   NOT NULL,

    PRIMARY KEY (account_id, sender_uuid, sender_device_id, distribution_id),
    FOREIGN KEY (account_id) REFERENCES signalmeow_device (aci_uuid) ON DELETE CASCADE ON UPDATE CASCADE
);

CREATE TABLE signalmeow_outbound_sender_key_info (
    account_id      TEXT  NOT NULL,
    group_id        TEXT  NOT NULL,
    distribution_id TEXT  NOT NULL,
    shared_with     jsonb NOT NULL,

    PRIMARY KEY (account_id, group_id)
);

CREATE TABLE signalmeow_groups (
    account_id       TEXT NOT NULL,
    group_identifier TEXT NOT NULL,
    master_key       TEXT NOT NULL,

    PRIMARY KEY (account_id, group_identifier)
);

CREATE TABLE signalmeow_recipients (
    account_id          TEXT    NOT NULL,
    aci_uuid            TEXT,
    pni_uuid            TEXT,
    e164_number         TEXT    NOT NULL DEFAULT '',
    contact_name        TEXT    NOT NULL DEFAULT '',
    contact_avatar_hash TEXT    NOT NULL DEFAULT '',
    nickname            TEXT    NOT NULL DEFAULT '',
    profile_key         bytea,
    profile_name        TEXT    NOT NULL DEFAULT '',
    profile_about       TEXT    NOT NULL DEFAULT '',
    profile_about_emoji TEXT    NOT NULL DEFAULT '',
    profile_avatar_path TEXT    NOT NULL DEFAULT '',
    profile_fetched_at  BIGINT,
    needs_pni_signature BOOLEAN NOT NULL DEFAULT false,
    blocked             BOOLEAN NOT NULL DEFAULT false,
    whitelisted         BOOLEAN,

    CONSTRAINT signalmeow_contacts_account_id_fkey FOREIGN KEY (account_id) REFERENCES signalmeow_device (aci_uuid)
        ON DELETE CASCADE ON UPDATE CASCADE,
    CONSTRAINT signalmeow_contacts_aci_unique UNIQUE (account_id, aci_uuid),
    CONSTRAINT signalmeow_contacts_pni_unique UNIQUE (account_id, pni_uuid)
);

CREATE TABLE signalmeow_unregistered_users (
    aci_uuid uuid PRIMARY KEY
);

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
    account_id          TEXT   NOT NULL,
    chat_id             BIGINT NOT NULL,
    recipient_id        BIGINT NOT NULL,
    data                bytea  NOT NULL,

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
