PRAGMA foreign_keys = OFF;

CREATE TABLE room_members_new (
	room_id TEXT NOT NULL REFERENCES rooms(id) ON DELETE CASCADE,
	install_id TEXT NOT NULL REFERENCES accounts(install_id) ON DELETE CASCADE,
	member_id TEXT NOT NULL UNIQUE,
	display_name TEXT NOT NULL,
	role TEXT NOT NULL CHECK(role IN ('owner', 'admin', 'member')),
	joined_at INTEGER NOT NULL,
	PRIMARY KEY(room_id, install_id)
);

INSERT INTO room_members_new(room_id, install_id, member_id, display_name, role, joined_at)
SELECT room_id, install_id, member_id, display_name, role, joined_at
FROM room_members;

DROP TABLE room_members;
ALTER TABLE room_members_new RENAME TO room_members;

CREATE INDEX room_members_install_id_idx ON room_members(install_id);

PRAGMA foreign_keys = ON;
