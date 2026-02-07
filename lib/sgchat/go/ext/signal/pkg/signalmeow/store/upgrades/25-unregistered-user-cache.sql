-- v25 (compatible with v13+): Cache unregistered users
CREATE TABLE signalmeow_unregistered_users (
    aci_uuid uuid PRIMARY KEY
);
