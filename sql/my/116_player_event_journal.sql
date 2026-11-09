-- MySQL schema equivalent of PostgreSQL migration 116.
-- UUIDs use compact BINARY(16); the repository must generate/bind UUID bytes.
-- JSON is native MySQL JSON and temporal columns use native DATETIME(6).

CREATE TABLE IF NOT EXISTS player_event_streams (
    player_id BIGINT NOT NULL,
    stream_id BINARY(16) NOT NULL,
    last_sequence BIGINT NOT NULL DEFAULT 0,
    pruned_through_sequence BIGINT NOT NULL DEFAULT 0,
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    PRIMARY KEY (player_id),
    UNIQUE KEY ux_player_event_stream_identity (player_id, stream_id),
    CONSTRAINT player_event_stream_player_fk FOREIGN KEY (player_id) REFERENCES players(player_id) ON DELETE CASCADE,
    CONSTRAINT player_event_stream_last_check CHECK (last_sequence >= 0),
    CONSTRAINT player_event_stream_pruned_check CHECK (pruned_through_sequence >= 0 AND pruned_through_sequence <= last_sequence)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS player_events (
    event_id BINARY(16) NOT NULL,
    player_id BIGINT NOT NULL,
    stream_id BINARY(16) NOT NULL,
    sequence BIGINT NOT NULL,
    event_type VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    topic VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NULL,
    payload JSON NOT NULL,
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    expires_at DATETIME(6) NOT NULL,
    PRIMARY KEY (event_id),
    UNIQUE KEY ux_player_events_sequence (player_id, sequence),
    KEY idx_player_events_expiry (expires_at, player_id, sequence),
    KEY idx_player_events_stream (player_id, stream_id),
    CONSTRAINT player_events_stream_fk FOREIGN KEY (player_id, stream_id)
        REFERENCES player_event_streams(player_id, stream_id) ON DELETE CASCADE,
    CONSTRAINT player_events_sequence_check CHECK (sequence > 0),
    CONSTRAINT player_events_type_check CHECK (CHAR_LENGTH(event_type) > 0),
    CONSTRAINT player_events_expiry_check CHECK (expires_at > created_at)
) ENGINE=InnoDB;
