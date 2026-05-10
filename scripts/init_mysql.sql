CREATE DATABASE IF NOT EXISTS liteim
  DEFAULT CHARACTER SET utf8mb4
  DEFAULT COLLATE utf8mb4_0900_ai_ci;

USE liteim;

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 1;

CREATE TABLE IF NOT EXISTS users (
    user_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    username VARCHAR(64) NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    password_salt VARCHAR(128) NOT NULL,
    nickname VARCHAR(64) NOT NULL,
    created_at_ms BIGINT NOT NULL,
    updated_at_ms BIGINT NOT NULL,
    PRIMARY KEY (user_id),
    UNIQUE KEY uk_users_username (username),
    KEY idx_users_created_at_ms (created_at_ms)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS friendships (
    user_id BIGINT UNSIGNED NOT NULL,
    friend_id BIGINT UNSIGNED NOT NULL,
    created_at_ms BIGINT NOT NULL,
    PRIMARY KEY (user_id, friend_id),
    KEY idx_friendships_friend_id (friend_id, user_id),
    CONSTRAINT fk_friendships_user
        FOREIGN KEY (user_id) REFERENCES users (user_id)
        ON DELETE CASCADE,
    CONSTRAINT fk_friendships_friend
        FOREIGN KEY (friend_id) REFERENCES users (user_id)
        ON DELETE CASCADE,
    CONSTRAINT chk_friendships_distinct
        CHECK (user_id <> friend_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS chat_groups (
    group_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    owner_id BIGINT UNSIGNED NOT NULL,
    group_name VARCHAR(128) NOT NULL,
    created_at_ms BIGINT NOT NULL,
    updated_at_ms BIGINT NOT NULL,
    PRIMARY KEY (group_id),
    KEY idx_chat_groups_owner_id (owner_id),
    CONSTRAINT fk_chat_groups_owner
        FOREIGN KEY (owner_id) REFERENCES users (user_id)
        ON DELETE RESTRICT
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS group_members (
    group_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    joined_at_ms BIGINT NOT NULL,
    PRIMARY KEY (group_id, user_id),
    KEY idx_group_members_user_id (user_id, group_id),
    CONSTRAINT fk_group_members_group
        FOREIGN KEY (group_id) REFERENCES chat_groups (group_id)
        ON DELETE CASCADE,
    CONSTRAINT fk_group_members_user
        FOREIGN KEY (user_id) REFERENCES users (user_id)
        ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS messages (
    message_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    conversation_type TINYINT UNSIGNED NOT NULL,
    conversation_id BIGINT UNSIGNED NOT NULL,
    sender_id BIGINT UNSIGNED NOT NULL,
    receiver_id BIGINT UNSIGNED NOT NULL DEFAULT 0,
    message_text TEXT NOT NULL,
    created_at_ms BIGINT NOT NULL,
    PRIMARY KEY (message_id),
    KEY idx_messages_history (conversation_type, conversation_id, message_id),
    KEY idx_messages_sender (sender_id, message_id),
    KEY idx_messages_receiver (receiver_id, message_id),
    CONSTRAINT fk_messages_sender
        FOREIGN KEY (sender_id) REFERENCES users (user_id)
        ON DELETE RESTRICT,
    CONSTRAINT chk_messages_conversation_type
        CHECK (conversation_type IN (1, 2))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS offline_messages (
    offline_message_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    user_id BIGINT UNSIGNED NOT NULL,
    message_id BIGINT UNSIGNED NOT NULL,
    delivered TINYINT(1) NOT NULL DEFAULT 0,
    created_at_ms BIGINT NOT NULL,
    delivered_at_ms BIGINT NULL,
    PRIMARY KEY (offline_message_id),
    UNIQUE KEY uk_offline_messages_user_message (user_id, message_id),
    KEY idx_offline_messages_user_pending (user_id, delivered, offline_message_id),
    KEY idx_offline_messages_message_id (message_id),
    CONSTRAINT fk_offline_messages_user
        FOREIGN KEY (user_id) REFERENCES users (user_id)
        ON DELETE CASCADE,
    CONSTRAINT fk_offline_messages_message
        FOREIGN KEY (message_id) REFERENCES messages (message_id)
        ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
