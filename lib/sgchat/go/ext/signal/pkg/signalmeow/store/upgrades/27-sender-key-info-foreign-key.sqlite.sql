-- v27 (compatible with v13+): Add missing foreign key for outbound sender key info and groups

CREATE TABLE new_signalmeow_outbound_sender_key_info (
    account_id      TEXT  NOT NULL,
    group_id        TEXT  NOT NULL,
    distribution_id TEXT  NOT NULL,
    shared_with     jsonb NOT NULL,

    PRIMARY KEY (account_id, group_id),
    CONSTRAINT signalmeow_outbound_sender_key_info_device_fkey
        FOREIGN KEY (account_id) REFERENCES signalmeow_device (aci_uuid) ON DELETE CASCADE ON UPDATE CASCADE
);
INSERT INTO new_signalmeow_outbound_sender_key_info
SELECT * FROM signalmeow_outbound_sender_key_info
WHERE EXISTS(SELECT 1 FROM signalmeow_device WHERE aci_uuid=account_id);
DROP TABLE signalmeow_outbound_sender_key_info;
ALTER TABLE new_signalmeow_outbound_sender_key_info RENAME TO signalmeow_outbound_sender_key_info;

CREATE TABLE new_signalmeow_groups (
    account_id       TEXT NOT NULL,
    group_identifier TEXT NOT NULL,
    master_key       TEXT NOT NULL,

    PRIMARY KEY (account_id, group_identifier),
    CONSTRAINT signalmeow_groups_device_fkey
        FOREIGN KEY (account_id) REFERENCES signalmeow_device (aci_uuid) ON DELETE CASCADE ON UPDATE CASCADE
);
INSERT INTO new_signalmeow_groups
SELECT * FROM signalmeow_groups
WHERE EXISTS(SELECT 1 FROM signalmeow_device WHERE aci_uuid=account_id);
DROP TABLE signalmeow_groups;
ALTER TABLE new_signalmeow_groups RENAME TO signalmeow_groups;
