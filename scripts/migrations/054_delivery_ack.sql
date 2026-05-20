USE liteim;

CREATE TABLE IF NOT EXISTS message_deliveries (
    message_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    status TINYINT UNSIGNED NOT NULL DEFAULT 0,
    pushed_at_ms BIGINT NULL,
    delivered_at_ms BIGINT NULL,
    read_at_ms BIGINT NULL,
    PRIMARY KEY (message_id, user_id),
    KEY idx_message_deliveries_user_status (user_id, status, message_id),
    CONSTRAINT fk_message_deliveries_message
        FOREIGN KEY (message_id) REFERENCES messages (message_id)
        ON DELETE CASCADE,
    CONSTRAINT fk_message_deliveries_user
        FOREIGN KEY (user_id) REFERENCES users (user_id)
        ON DELETE CASCADE,
    CONSTRAINT chk_message_deliveries_status
        CHECK (status IN (0, 1, 2, 3))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
