-- v27 (compatible with v13+): Add missing foreign key for outbound sender key info and groups
DELETE FROM signalmeow_outbound_sender_key_info
WHERE NOT EXISTS(SELECT 1 FROM signalmeow_device WHERE aci_uuid=account_id);
ALTER TABLE signalmeow_outbound_sender_key_info ADD CONSTRAINT signalmeow_outbound_sender_key_info_device_fkey
    FOREIGN KEY (account_id) REFERENCES signalmeow_device (aci_uuid) ON DELETE CASCADE ON UPDATE CASCADE;

DELETE FROM signalmeow_groups
WHERE NOT EXISTS(SELECT 1 FROM signalmeow_device WHERE aci_uuid=account_id);
ALTER TABLE signalmeow_groups ADD CONSTRAINT signalmeow_groups_device_fkey
    FOREIGN KEY (account_id) REFERENCES signalmeow_device (aci_uuid) ON DELETE CASCADE ON UPDATE CASCADE;
