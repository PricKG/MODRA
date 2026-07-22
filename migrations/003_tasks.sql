CREATE TABLE tasks (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id INTEGER NOT NULL REFERENCES projects(id) ON DELETE RESTRICT,
    title TEXT NOT NULL CHECK(length(trim(title)) > 0),
    description TEXT,
    type TEXT NOT NULL DEFAULT 'technical'
        CHECK(type IN ('technical', 'administrative', 'management', 'research', 'documentation', 'follow_up')),
    status TEXT NOT NULL DEFAULT 'pending'
        CHECK(status IN ('pending', 'in_progress', 'blocked', 'in_review', 'completed', 'cancelled')),
    priority TEXT NOT NULL DEFAULT 'normal'
        CHECK(priority IN ('low', 'normal', 'high', 'critical')),
    due_date TEXT,
    completed_at TEXT,
    blocked_reason TEXT,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    archived_at TEXT,
    CHECK(status <> 'blocked' OR (blocked_reason IS NOT NULL AND length(trim(blocked_reason)) > 0)),
    CHECK(status <> 'completed' OR completed_at IS NOT NULL)
);

CREATE INDEX idx_tasks_project_id ON tasks(project_id);
CREATE INDEX idx_tasks_status ON tasks(status);
CREATE INDEX idx_tasks_due_date ON tasks(due_date);
CREATE INDEX idx_tasks_archived_at ON tasks(archived_at);
