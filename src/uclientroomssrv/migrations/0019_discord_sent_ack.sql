ALTER TABLE discord_outbound_messages ADD COLUMN discord_channel_id TEXT NOT NULL DEFAULT '';
ALTER TABLE discord_outbound_messages ADD COLUMN discord_message_id TEXT NOT NULL DEFAULT '';

CREATE TABLE discord_pending_deletes (
	install_id TEXT NOT NULL,
	channel_id TEXT NOT NULL,
	message_id TEXT NOT NULL,
	created_at INTEGER NOT NULL,
	PRIMARY KEY (install_id, message_id)
);
