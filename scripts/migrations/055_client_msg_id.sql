USE liteim;

SET @column_exists = (
    SELECT COUNT(*)
    FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'messages'
      AND COLUMN_NAME = 'client_msg_id'
);

SET @sql = IF(
    @column_exists = 0,
    'ALTER TABLE messages ADD COLUMN client_msg_id VARCHAR(64) NULL AFTER created_at_ms',
    'SELECT 1'
);
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @index_exists = (
    SELECT COUNT(*)
    FROM INFORMATION_SCHEMA.STATISTICS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'messages'
      AND INDEX_NAME = 'uk_messages_sender_client_msg'
);

SET @sql = IF(
    @index_exists = 0,
    'ALTER TABLE messages ADD UNIQUE KEY uk_messages_sender_client_msg (sender_id, client_msg_id)',
    'SELECT 1'
);
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
