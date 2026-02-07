-- v2: Add group master key table
CREATE TABLE signalmeow_groups (
    our_aci_uuid     TEXT NOT NULL,
    group_identifier TEXT NOT NULL,
    master_key       TEXT NOT NULL,

    PRIMARY KEY (our_aci_uuid, group_identifier)
);
