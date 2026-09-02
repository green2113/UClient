CREATE TABLE launcher_notices (
	id TEXT PRIMARY KEY,
	title TEXT NOT NULL,
	body TEXT NOT NULL,
	severity TEXT NOT NULL DEFAULT 'warning' CHECK(severity IN ('info', 'warning', 'critical')),
	blocks_play INTEGER NOT NULL DEFAULT 0,
	starts_at INTEGER,
	ends_at INTEGER,
	sort_order INTEGER NOT NULL DEFAULT 0,
	enabled INTEGER NOT NULL DEFAULT 1,
	created_at INTEGER NOT NULL,
	updated_at INTEGER NOT NULL
);
CREATE INDEX launcher_notices_active_idx ON launcher_notices(enabled, starts_at, ends_at, sort_order);
