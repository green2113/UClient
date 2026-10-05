ALTER TABLE discord_message_channels ADD COLUMN topic_desired TEXT NOT NULL DEFAULT '';
ALTER TABLE discord_message_channels ADD COLUMN topic_retry_at INTEGER NOT NULL DEFAULT 0;
UPDATE discord_message_channels SET topic_desired = topic WHERE topic_desired = '';
