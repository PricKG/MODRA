ALTER TABLE tasks ADD COLUMN assignee_name TEXT
    CHECK(assignee_name IS NULL OR length(trim(assignee_name)) > 0);
