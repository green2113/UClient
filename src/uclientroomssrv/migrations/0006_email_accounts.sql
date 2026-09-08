ALTER TABLE accounts ADD COLUMN email_normalized TEXT;
ALTER TABLE accounts ADD COLUMN password_hash TEXT;

CREATE UNIQUE INDEX accounts_email_normalized_unique_idx
	ON accounts(email_normalized)
	WHERE email_normalized IS NOT NULL;

CREATE TABLE account_device_credentials (
	id INTEGER PRIMARY KEY AUTOINCREMENT,
	account_install_id TEXT NOT NULL REFERENCES accounts(install_id) ON DELETE CASCADE,
	secret_hash TEXT NOT NULL,
	created_at INTEGER NOT NULL,
	last_used_at INTEGER NOT NULL,
	UNIQUE(account_install_id, secret_hash)
);
CREATE INDEX account_device_credentials_account_idx
	ON account_device_credentials(account_install_id);

INSERT INTO account_device_credentials(account_install_id, secret_hash, created_at, last_used_at)
SELECT install_id, secret_hash, created_at, last_seen_at
FROM accounts;

CREATE TABLE auth_attempts (
	id INTEGER PRIMARY KEY AUTOINCREMENT,
	kind TEXT NOT NULL,
	rate_key TEXT NOT NULL,
	succeeded INTEGER NOT NULL DEFAULT 0,
	created_at INTEGER NOT NULL
);
CREATE INDEX auth_attempts_key_created_idx
	ON auth_attempts(kind, rate_key, created_at);
