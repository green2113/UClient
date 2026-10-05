ALTER TABLE discord_links ADD COLUMN launcher_seen_at INTEGER NOT NULL DEFAULT 0;
ALTER TABLE discord_links ADD COLUMN game_seen_at INTEGER NOT NULL DEFAULT 0;

CREATE TABLE discord_control_commands (
	id INTEGER PRIMARY KEY AUTOINCREMENT,
	install_id TEXT NOT NULL,
	target TEXT NOT NULL,
	kind TEXT NOT NULL,
	address TEXT NOT NULL DEFAULT '',
	password TEXT NOT NULL DEFAULT '',
	had_password INTEGER NOT NULL DEFAULT 0,
	application_id TEXT NOT NULL DEFAULT '',
	interaction_token TEXT NOT NULL DEFAULT '',
	created_at INTEGER NOT NULL,
	status TEXT NOT NULL DEFAULT 'pending'
);

CREATE INDEX discord_control_commands_install_idx
	ON discord_control_commands(install_id, status, id);
