CREATE TABLE cfg_backup_versions (
	id TEXT PRIMARY KEY,
	account_install_id TEXT NOT NULL REFERENCES accounts(install_id) ON DELETE CASCADE,
	relative_path TEXT NOT NULL,
	object_key TEXT NOT NULL UNIQUE,
	created_at INTEGER NOT NULL,
	size_bytes INTEGER NOT NULL CHECK(size_bytes >= 0),
	sha256 TEXT NOT NULL
);
CREATE INDEX cfg_backup_versions_account_created_idx
	ON cfg_backup_versions(account_install_id, created_at DESC);
