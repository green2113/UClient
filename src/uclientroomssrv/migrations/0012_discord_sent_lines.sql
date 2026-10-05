CREATE TABLE discord_sent_lines (
	install_id TEXT NOT NULL,
	body TEXT NOT NULL,
	created_at INTEGER NOT NULL,
	PRIMARY KEY (install_id, body)
);
