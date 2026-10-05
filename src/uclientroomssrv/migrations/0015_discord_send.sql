ALTER TABLE discord_outbound_messages ADD COLUMN mode TEXT NOT NULL DEFAULT 'all';
ALTER TABLE discord_outbound_messages ADD COLUMN room_id TEXT NOT NULL DEFAULT '';
