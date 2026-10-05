CREATE TABLE discord_links (
	discord_user_id TEXT PRIMARY KEY,
	install_id TEXT NOT NULL UNIQUE,
	linked_at INTEGER NOT NULL,
	game_online_at INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE discord_link_challenges (
	token TEXT PRIMARY KEY,
	discord_user_id TEXT NOT NULL,
	application_id TEXT NOT NULL,
	interaction_token TEXT NOT NULL,
	guild_id TEXT NOT NULL DEFAULT '',
	created_at INTEGER NOT NULL,
	expires_at INTEGER NOT NULL,
	consumed INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE discord_message_channels (
	install_id TEXT PRIMARY KEY,
	discord_user_id TEXT NOT NULL UNIQUE,
	guild_id TEXT NOT NULL,
	channel_id TEXT NOT NULL UNIQUE,
	created_at INTEGER NOT NULL
);

CREATE TABLE discord_outbound_messages (
	id INTEGER PRIMARY KEY AUTOINCREMENT,
	install_id TEXT NOT NULL,
	body TEXT NOT NULL,
	created_at INTEGER NOT NULL
);

CREATE INDEX discord_outbound_messages_install_idx
	ON discord_outbound_messages(install_id, id);
