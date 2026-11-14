-- V3 feedback persistence is a PostgreSQL runtime feature. This parity table
-- keeps the portable schema inventory aligned; publication worker support is
-- intentionally tied to the PostgreSQL V3 deployment backend.
CREATE TABLE IF NOT EXISTS player_feedback_reports (
    report_id CHAR(36) NOT NULL PRIMARY KEY,
    player_id BIGINT NOT NULL,
    idempotency_key VARCHAR(128) NOT NULL,
    report_type VARCHAR(16) NOT NULL,
    subject VARCHAR(160) NOT NULL,
    description TEXT NOT NULL,
    expected_behavior TEXT NOT NULL,
    actual_behavior TEXT NOT NULL,
    reproduction_steps TEXT NOT NULL,
    diagnostics JSON NOT NULL,
    client_name VARCHAR(32) NOT NULL,
    client_version VARCHAR(64) NOT NULL,
    build_id VARCHAR(80) NOT NULL,
    protocol_version VARCHAR(32) NOT NULL,
    public_issue_title TEXT NOT NULL,
    public_issue_body LONGTEXT NOT NULL,
    publication_state VARCHAR(16) NOT NULL DEFAULT 'pending',
    github_issue_number BIGINT NULL,
    github_issue_url TEXT NULL,
    attempt_count INT NOT NULL DEFAULT 0,
    last_publication_error VARCHAR(2048) NOT NULL DEFAULT '',
    next_retry_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY player_feedback_idempotency (player_id, idempotency_key),
    KEY player_feedback_outbox_ready (publication_state, next_retry_at, created_at),
    KEY player_feedback_player_created (player_id, created_at),
    CONSTRAINT player_feedback_player_fk FOREIGN KEY (player_id)
        REFERENCES players(player_id) ON DELETE CASCADE
);
