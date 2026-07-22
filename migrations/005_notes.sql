CREATE TABLE notes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    title TEXT NOT NULL CHECK(length(trim(title)) > 0),
    type TEXT NOT NULL DEFAULT 'general'
        CHECK(type IN ('general', 'technical', 'solution', 'meeting', 'sql', 'procedure', 'configuration', 'reference')),
    content TEXT NOT NULL CHECK(length(trim(content)) > 0),
    project_id INTEGER REFERENCES projects(id) ON DELETE RESTRICT,
    task_id INTEGER REFERENCES tasks(id) ON DELETE RESTRICT,
    is_favorite INTEGER NOT NULL DEFAULT 0 CHECK(is_favorite IN (0, 1)),
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    archived_at TEXT
);

CREATE INDEX idx_notes_type ON notes(type);
CREATE INDEX idx_notes_project_id ON notes(project_id);
CREATE INDEX idx_notes_task_id ON notes(task_id);
CREATE INDEX idx_notes_is_favorite ON notes(is_favorite);
CREATE INDEX idx_notes_updated_at ON notes(updated_at);
CREATE INDEX idx_notes_archived_at ON notes(archived_at);
