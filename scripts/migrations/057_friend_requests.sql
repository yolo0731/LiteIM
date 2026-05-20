USE liteim;

CREATE TABLE IF NOT EXISTS friend_requests (
    requester_id BIGINT UNSIGNED NOT NULL,
    target_user_id BIGINT UNSIGNED NOT NULL,
    status TINYINT UNSIGNED NOT NULL DEFAULT 0,
    created_at_ms BIGINT NOT NULL,
    updated_at_ms BIGINT NOT NULL,
    PRIMARY KEY (requester_id, target_user_id),
    KEY idx_friend_requests_target_status (target_user_id, status, requester_id),
    CONSTRAINT fk_friend_requests_requester
        FOREIGN KEY (requester_id) REFERENCES users (user_id)
        ON DELETE CASCADE,
    CONSTRAINT fk_friend_requests_target
        FOREIGN KEY (target_user_id) REFERENCES users (user_id)
        ON DELETE CASCADE,
    CONSTRAINT chk_friend_requests_distinct
        CHECK (requester_id <> target_user_id),
    CONSTRAINT chk_friend_requests_status
        CHECK (status IN (0, 1, 2))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
