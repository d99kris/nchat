-- v28 (compatible with v13+): Store auth credential salt for phonenumberless accounts
ALTER TABLE signalmeow_device ADD COLUMN auth_credential_salt bytea;
