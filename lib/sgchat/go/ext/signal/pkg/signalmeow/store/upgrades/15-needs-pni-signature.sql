-- v15 (compatible with v13+): Store flag for recipients who need a PNI signature
ALTER TABLE signalmeow_recipients ADD COLUMN needs_pni_signature boolean DEFAULT false;
