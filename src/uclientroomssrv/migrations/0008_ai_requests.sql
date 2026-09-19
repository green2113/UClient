CREATE TABLE ai_requests (
	install_id TEXT NOT NULL,
	created_at INTEGER NOT NULL
);
CREATE INDEX ai_requests_install_created_idx ON ai_requests(install_id, created_at);
