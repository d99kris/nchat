-- v3: Add contacts table
CREATE TABLE signalmeow_contacts (
    our_aci_uuid        TEXT NOT NULL,
    aci_uuid            TEXT NOT NULL,
    e164_number         TEXT,
    contact_name        TEXT,
    contact_avatar_hash TEXT,
    profile_key         TEXT,
    profile_name        TEXT,
    profile_about       TEXT,
    profile_about_emoji TEXT,
    profile_avatar_hash TEXT,

    PRIMARY KEY (our_aci_uuid, aci_uuid),
    FOREIGN KEY (our_aci_uuid) REFERENCES signalmeow_device (aci_uuid) ON DELETE CASCADE ON UPDATE CASCADE
);
