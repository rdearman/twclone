BEGIN;

CREATE TABLE IF NOT EXISTS player_feedback_reports (
    report_id UUID PRIMARY KEY,
    player_id INTEGER NOT NULL REFERENCES players(player_id) ON DELETE CASCADE,
    idempotency_key VARCHAR(128) NOT NULL,
    report_type VARCHAR(16) NOT NULL CHECK (report_type IN ('bug', 'suggestion')),
    subject VARCHAR(160) NOT NULL,
    description TEXT NOT NULL,
    expected_behavior TEXT NOT NULL DEFAULT '',
    actual_behavior TEXT NOT NULL DEFAULT '',
    reproduction_steps TEXT NOT NULL DEFAULT '',
    diagnostics JSONB NOT NULL DEFAULT '{}'::jsonb,
    client_name VARCHAR(32) NOT NULL CHECK (client_name IN ('python', 'godot')),
    client_version VARCHAR(64) NOT NULL DEFAULT '',
    build_id VARCHAR(80) NOT NULL DEFAULT '',
    protocol_version VARCHAR(32) NOT NULL DEFAULT '',
    public_issue_title TEXT NOT NULL DEFAULT '',
    public_issue_body TEXT NOT NULL DEFAULT '',
    publication_state VARCHAR(16) NOT NULL DEFAULT 'pending'
        CHECK (publication_state IN ('pending', 'processing', 'published', 'retryable', 'failed')),
    github_issue_number BIGINT,
    github_issue_url TEXT,
    attempt_count INTEGER NOT NULL DEFAULT 0 CHECK (attempt_count >= 0),
    last_publication_error TEXT NOT NULL DEFAULT '',
    next_retry_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT player_feedback_idempotency UNIQUE (player_id, idempotency_key),
    CONSTRAINT player_feedback_issue_pair CHECK (
        (github_issue_number IS NULL AND github_issue_url IS NULL)
        OR (github_issue_number IS NOT NULL AND github_issue_url IS NOT NULL)
    )
);

CREATE INDEX IF NOT EXISTS player_feedback_outbox_ready
    ON player_feedback_reports (next_retry_at, created_at)
    WHERE publication_state IN ('pending', 'retryable');
CREATE INDEX IF NOT EXISTS player_feedback_player_created
    ON player_feedback_reports (player_id, created_at DESC);

COMMIT;
