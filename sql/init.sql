PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT NOT NULL UNIQUE,
    nickname TEXT NOT NULL,
    password_salt TEXT NOT NULL,
    password_hash TEXT NOT NULL,
    user_type INTEGER NOT NULL DEFAULT 0,
    created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
    CHECK (username <> '')
);

CREATE TABLE IF NOT EXISTS friendships (
    user_id INTEGER NOT NULL,
    friend_id INTEGER NOT NULL,
    created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
    PRIMARY KEY (user_id, friend_id),
    CHECK (user_id <> friend_id),
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
    FOREIGN KEY (friend_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS groups (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    owner_id INTEGER NOT NULL,
    created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
    CHECK (name <> ''),
    FOREIGN KEY (owner_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS group_members (
    group_id INTEGER NOT NULL,
    user_id INTEGER NOT NULL,
    joined_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
    PRIMARY KEY (group_id, user_id),
    FOREIGN KEY (group_id) REFERENCES groups(id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    message_type INTEGER NOT NULL,
    sender_id INTEGER NOT NULL,
    receiver_id INTEGER,
    group_id INTEGER,
    body TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    delivered INTEGER NOT NULL DEFAULT 0,
    CHECK (
        (message_type = 0 AND receiver_id IS NOT NULL AND group_id IS NULL)
        OR
        (message_type = 1 AND receiver_id IS NULL AND group_id IS NOT NULL)
    ),
    FOREIGN KEY (sender_id) REFERENCES users(id) ON DELETE CASCADE,
    FOREIGN KEY (receiver_id) REFERENCES users(id) ON DELETE CASCADE,
    FOREIGN KEY (group_id) REFERENCES groups(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_friendships_user_id
    ON friendships(user_id);

CREATE INDEX IF NOT EXISTS idx_group_members_group_id
    ON group_members(group_id);

CREATE INDEX IF NOT EXISTS idx_messages_private_history
    ON messages(message_type, sender_id, receiver_id, created_at, id);

CREATE INDEX IF NOT EXISTS idx_messages_group_history
    ON messages(message_type, group_id, created_at, id);

CREATE INDEX IF NOT EXISTS idx_messages_offline
    ON messages(message_type, receiver_id, delivered, created_at, id);
