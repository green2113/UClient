CREATE TABLE email_verifications (
	id INTEGER PRIMARY KEY AUTOINCREMENT,
	email_normalized TEXT NOT NULL,
	purpose TEXT NOT NULL,
	code_hash TEXT NOT NULL,
	password_hash TEXT NOT NULL,
	install_id TEXT,
	secret_hash TEXT,
	locale TEXT NOT NULL DEFAULT 'en',
	version TEXT,
	expires_at INTEGER NOT NULL,
	attempts INTEGER NOT NULL DEFAULT 0,
	created_at INTEGER NOT NULL
);
CREATE INDEX email_verifications_email_purpose_idx
	ON email_verifications(email_normalized, purpose, created_at DESC);
CREATE INDEX email_verifications_expires_idx
	ON email_verifications(expires_at);
