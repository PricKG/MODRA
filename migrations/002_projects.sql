CREATE TABLE projects (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL CHECK(length(trim(name)) > 0),
    alias TEXT NOT NULL UNIQUE CHECK(length(alias) > 0),
    description TEXT,
    status TEXT NOT NULL DEFAULT 'planned'
        CHECK(status IN ('planned', 'active', 'paused', 'completed', 'archived')),
    start_date TEXT,
    target_date TEXT,
    local_path TEXT,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    archived_at TEXT
);

CREATE INDEX idx_projects_status ON projects(status);
CREATE INDEX idx_projects_archived_at ON projects(archived_at);
