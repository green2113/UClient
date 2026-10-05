ALTER TABLE discord_links ADD COLUMN game_session TEXT NOT NULL DEFAULT '';
ALTER TABLE discord_links ADD COLUMN game_session_started_at INTEGER NOT NULL DEFAULT 0;
ALTER TABLE discord_links ADD COLUMN game_session_seen_at INTEGER NOT NULL DEFAULT 0;
