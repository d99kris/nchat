-- v13: Add PNIs to recipient table and merge profile keys
CREATE TABLE signalmeow_recipients (
    account_id          TEXT NOT NULL,
    aci_uuid            TEXT,
    pni_uuid            TEXT,
    e164_number         TEXT NOT NULL DEFAULT '',
    contact_name        TEXT NOT NULL DEFAULT '',
    contact_avatar_hash TEXT NOT NULL DEFAULT '',
    profile_key         bytea,
    profile_name        TEXT NOT NULL DEFAULT '',
    profile_about       TEXT NOT NULL DEFAULT '',
    profile_about_emoji TEXT NOT NULL DEFAULT '',
    profile_avatar_path TEXT NOT NULL DEFAULT '',
    profile_fetched_at  BIGINT,

    CONSTRAINT signalmeow_contacts_account_id_fkey FOREIGN KEY (account_id) REFERENCES signalmeow_device (aci_uuid)
        ON DELETE CASCADE ON UPDATE CASCADE,
    CONSTRAINT signalmeow_contacts_aci_unique UNIQUE (account_id, aci_uuid),
    CONSTRAINT signalmeow_contacts_pni_unique UNIQUE (account_id, pni_uuid)
);

INSERT INTO signalmeow_recipients (
    account_id, aci_uuid, e164_number, contact_name, contact_avatar_hash, profile_key, profile_name,
    profile_about, profile_about_emoji, profile_avatar_path, profile_fetched_at
)
SELECT account_id, aci_uuid, e164_number, contact_name, contact_avatar_hash, profile_key, profile_name,
       profile_about, profile_about_emoji, profile_avatar_path, profile_fetched_at
FROM signalmeow_contacts;

INSERT INTO signalmeow_recipients (account_id, aci_uuid, profile_key)
SELECT account_id, their_aci_uuid, key
FROM signalmeow_profile_keys
WHERE true -- https://sqlite.org/lang_upsert.html#parsing_ambiguity
ON CONFLICT (account_id, aci_uuid) DO UPDATE SET profile_key=excluded.profile_key;

DROP TABLE signalmeow_contacts;
DROP TABLE signalmeow_profile_keys;
