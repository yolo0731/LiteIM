USE liteim;

SET NAMES utf8mb4;
SET @now_ms = CAST(UNIX_TIMESTAMP(CURRENT_TIMESTAMP(3)) * 1000 AS SIGNED);

INSERT INTO users (
    user_id,
    username,
    password_hash,
    password_salt,
    nickname,
    created_at_ms,
    updated_at_ms
) VALUES
    (1001, 'alice', 'dev_hash_alice', 'dev_salt_alice', 'Alice', @now_ms, @now_ms),
    (1002, 'bob', 'dev_hash_bob', 'dev_salt_bob', 'Bob', @now_ms, @now_ms),
    (9001, 'mira_bot', 'dev_hash_mira_bot', 'dev_salt_mira_bot', 'Mira Bot', @now_ms, @now_ms)
ON DUPLICATE KEY UPDATE
    password_hash = VALUES(password_hash),
    password_salt = VALUES(password_salt),
    nickname = VALUES(nickname),
    updated_at_ms = VALUES(updated_at_ms);

INSERT INTO friendships (user_id, friend_id, created_at_ms) VALUES
    (1001, 1002, @now_ms),
    (1002, 1001, @now_ms),
    (1001, 9001, @now_ms),
    (9001, 1001, @now_ms)
ON DUPLICATE KEY UPDATE
    created_at_ms = VALUES(created_at_ms);

INSERT INTO chat_groups (
    group_id,
    owner_id,
    group_name,
    created_at_ms,
    updated_at_ms
) VALUES
    (2001, 1001, 'dev_group', @now_ms, @now_ms)
ON DUPLICATE KEY UPDATE
    owner_id = VALUES(owner_id),
    group_name = VALUES(group_name),
    updated_at_ms = VALUES(updated_at_ms);

INSERT INTO group_members (group_id, user_id, joined_at_ms) VALUES
    (2001, 1001, @now_ms),
    (2001, 1002, @now_ms),
    (2001, 9001, @now_ms)
ON DUPLICATE KEY UPDATE
    joined_at_ms = VALUES(joined_at_ms);

INSERT INTO messages (
    message_id,
    conversation_type,
    conversation_id,
    sender_id,
    receiver_id,
    message_text,
    created_at_ms
) VALUES
    (5001, 1, 10011002, 1001, 1002, 'hello bob', @now_ms),
    (5002, 2, 2001, 1001, 2001, 'welcome to dev_group', @now_ms),
    (5003, 1, 10019001, 9001, 1001, 'I am Mira Bot.', @now_ms)
ON DUPLICATE KEY UPDATE
    conversation_type = VALUES(conversation_type),
    conversation_id = VALUES(conversation_id),
    sender_id = VALUES(sender_id),
    receiver_id = VALUES(receiver_id),
    message_text = VALUES(message_text),
    created_at_ms = VALUES(created_at_ms);

INSERT INTO offline_messages (
    user_id,
    message_id,
    delivered,
    created_at_ms,
    delivered_at_ms
) VALUES
    (1002, 5001, 0, @now_ms, NULL),
    (1001, 5003, 0, @now_ms, NULL)
ON DUPLICATE KEY UPDATE
    delivered = VALUES(delivered),
    created_at_ms = VALUES(created_at_ms),
    delivered_at_ms = VALUES(delivered_at_ms);

ALTER TABLE users AUTO_INCREMENT = 10000;
ALTER TABLE chat_groups AUTO_INCREMENT = 10000;
ALTER TABLE messages AUTO_INCREMENT = 10000;
ALTER TABLE offline_messages AUTO_INCREMENT = 10000;
