-- v24 (compatible with v13+): Store outbound sender keys for groups
CREATE TABLE IF NOT EXISTS signalmeow_outbound_sender_key_info (
    account_id      TEXT  NOT NULL,
    group_id        TEXT  NOT NULL,
    distribution_id TEXT  NOT NULL,
    shared_with     jsonb NOT NULL,

    PRIMARY KEY (account_id, group_id)
);
