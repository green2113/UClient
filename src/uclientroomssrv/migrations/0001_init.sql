PRAGMA foreign_keys = ON;

CREATE TABLE accounts (
	install_id TEXT PRIMARY KEY,
	secret_hash TEXT NOT NULL,
	created_at INTEGER NOT NULL,
	last_seen_at INTEGER NOT NULL,
	last_player_name TEXT NOT NULL DEFAULT '',
	last_client_version TEXT NOT NULL DEFAULT '',
	created_ip TEXT NOT NULL DEFAULT '',
	last_ip TEXT NOT NULL DEFAULT ''
);

CREATE TABLE registration_attempts (
	ip TEXT NOT NULL,
	created_at INTEGER NOT NULL
);
CREATE INDEX registration_attempts_ip_created_idx
	ON registration_attempts(ip, created_at);

CREATE TABLE user_bans (
	id INTEGER PRIMARY KEY AUTOINCREMENT,
	install_id TEXT NOT NULL,
	reason TEXT NOT NULL,
	banned_at INTEGER NOT NULL,
	expires_at INTEGER,
	banned_by TEXT NOT NULL DEFAULT ''
);
CREATE INDEX user_bans_install_id_idx ON user_bans(install_id);

CREATE TABLE rooms (
	id TEXT PRIMARY KEY,
	name TEXT NOT NULL,
	owner_install_id TEXT NOT NULL REFERENCES accounts(install_id) ON DELETE CASCADE,
	invite_code TEXT NOT NULL UNIQUE,
	created_at INTEGER NOT NULL
);

CREATE TABLE room_members (
	room_id TEXT NOT NULL REFERENCES rooms(id) ON DELETE CASCADE,
	install_id TEXT NOT NULL REFERENCES accounts(install_id) ON DELETE CASCADE,
	member_id TEXT NOT NULL UNIQUE,
	display_name TEXT NOT NULL,
	role TEXT NOT NULL CHECK(role IN ('owner', 'member')),
	joined_at INTEGER NOT NULL,
	PRIMARY KEY(room_id, install_id)
);
CREATE INDEX room_members_install_id_idx ON room_members(install_id);

CREATE TABLE room_changes (
	sequence INTEGER PRIMARY KEY AUTOINCREMENT,
	room_id TEXT NOT NULL,
	changed_at INTEGER NOT NULL
);
CREATE INDEX room_changes_changed_at_idx ON room_changes(changed_at);
