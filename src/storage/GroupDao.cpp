#include "liteim/storage/GroupDao.hpp"

#include "liteim/base/Timestamp.hpp"
#include "liteim/storage/MySqlConnection.hpp"
#include "liteim/storage/MySqlPool.hpp"

#include <stdexcept>
#include <utility>

namespace liteim {
namespace {

Status malformedGroupRowStatus() {
    return Status::error(ErrorCode::InternalError, "groups query returned a malformed row");
}

Status requiredValue(const MySqlRow& row, std::size_t index, const std::string*& value) {
    if (index >= row.values.size() || !row.values[index].has_value()) {
        return malformedGroupRowStatus();
    }
    value = &(*row.values[index]);
    return Status::ok();
}

Status parseUint64(const std::string& text, std::uint64_t& value) {
    try {
        std::size_t parsed = 0;
        const auto result = std::stoull(text, &parsed, 10);
        if (parsed != text.size()) {
            return malformedGroupRowStatus();
        }
        value = static_cast<std::uint64_t>(result);
        return Status::ok();
    } catch (const std::exception&) {
        return malformedGroupRowStatus();
    }
}

Status parseInt64(const std::string& text, std::int64_t& value) {
    try {
        std::size_t parsed = 0;
        const auto result = std::stoll(text, &parsed, 10);
        if (parsed != text.size()) {
            return malformedGroupRowStatus();
        }
        value = static_cast<std::int64_t>(result);
        return Status::ok();
    } catch (const std::exception&) {
        return malformedGroupRowStatus();
    }
}

Status validateId(std::uint64_t id, const std::string& field_name) {
    if (id == 0U) {
        return Status::error(ErrorCode::InvalidArgument, field_name + " must not be zero");
    }
    return Status::ok();
}

Status bindId(PreparedStatement& statement,
              std::size_t index,
              std::uint64_t id,
              const std::string& field_name) {
    const auto validate_status = validateId(id, field_name);
    if (!validate_status.isOk()) {
        return validate_status;
    }
    return statement.bindUInt64(index, id);
}

void rollbackSilently(MySqlConnection& connection) noexcept {
    (void)connection.executeSimple("ROLLBACK");
}

Status rowToGroupRecord(const MySqlRow& row, GroupRecord& group) {
    if (row.values.size() != 4U) {
        return malformedGroupRowStatus();
    }

    const std::string* group_id = nullptr;
    const std::string* owner_id = nullptr;
    const std::string* group_name = nullptr;
    const std::string* created_at_ms = nullptr;

    const auto group_status = requiredValue(row, 0, group_id);
    if (!group_status.isOk()) {
        return group_status;
    }
    const auto owner_status = requiredValue(row, 1, owner_id);
    if (!owner_status.isOk()) {
        return owner_status;
    }
    const auto name_status = requiredValue(row, 2, group_name);
    if (!name_status.isOk()) {
        return name_status;
    }
    const auto created_status = requiredValue(row, 3, created_at_ms);
    if (!created_status.isOk()) {
        return created_status;
    }

    GroupRecord parsed_group;
    const auto group_parse_status = parseUint64(*group_id, parsed_group.group_id);
    if (!group_parse_status.isOk()) {
        return group_parse_status;
    }
    const auto owner_parse_status = parseUint64(*owner_id, parsed_group.owner_id);
    if (!owner_parse_status.isOk()) {
        return owner_parse_status;
    }
    const auto created_parse_status = parseInt64(*created_at_ms, parsed_group.created_at_ms);
    if (!created_parse_status.isOk()) {
        return created_parse_status;
    }
    parsed_group.group_name = *group_name;

    group = std::move(parsed_group);
    return Status::ok();
}

Status rowToGroupMemberRecord(const MySqlRow& row, GroupMemberRecord& member) {
    if (row.values.size() != 4U) {
        return malformedGroupRowStatus();
    }

    const std::string* user_id = nullptr;
    const std::string* username = nullptr;
    const std::string* nickname = nullptr;
    const std::string* joined_at_ms = nullptr;

    const auto user_status = requiredValue(row, 0, user_id);
    if (!user_status.isOk()) {
        return user_status;
    }
    const auto username_status = requiredValue(row, 1, username);
    if (!username_status.isOk()) {
        return username_status;
    }
    const auto nickname_status = requiredValue(row, 2, nickname);
    if (!nickname_status.isOk()) {
        return nickname_status;
    }
    const auto joined_status = requiredValue(row, 3, joined_at_ms);
    if (!joined_status.isOk()) {
        return joined_status;
    }

    GroupMemberRecord parsed_member;
    const auto user_parse_status = parseUint64(*user_id, parsed_member.user_id);
    if (!user_parse_status.isOk()) {
        return user_parse_status;
    }
    const auto joined_parse_status = parseInt64(*joined_at_ms, parsed_member.joined_at_ms);
    if (!joined_parse_status.isOk()) {
        return joined_parse_status;
    }
    parsed_member.username = *username;
    parsed_member.nickname = *nickname;

    member = std::move(parsed_member);
    return Status::ok();
}

Status querySingleGroup(PreparedStatement& statement, GroupRecord& group) {
    MySqlQueryResult result;
    const auto query_status = statement.executeQuery(result);
    if (!query_status.isOk()) {
        return query_status;
    }
    if (result.rows().empty()) {
        return Status::error(ErrorCode::NotFound, "group was not found");
    }
    if (result.rows().size() != 1U) {
        return Status::error(ErrorCode::InternalError, "groups query returned multiple rows");
    }
    return rowToGroupRecord(result.rows().front(), group);
}

Status queryLastInsertedGroup(MySqlConnection& connection, GroupRecord& group) {
    PreparedStatement query(connection);
    const auto prepare_status =
        query.prepare("SELECT group_id, owner_id, group_name, created_at_ms "
                      "FROM chat_groups WHERE group_id = LAST_INSERT_ID() LIMIT 1");
    if (!prepare_status.isOk()) {
        return prepare_status;
    }
    return querySingleGroup(query, group);
}

Status queryGroupOwner(MySqlConnection& connection, std::uint64_t group_id, std::uint64_t& owner_id) {
    PreparedStatement query(connection);
    const auto prepare_status = query.prepare("SELECT owner_id FROM chat_groups WHERE group_id = ? LIMIT 1");
    if (!prepare_status.isOk()) {
        return prepare_status;
    }
    const auto bind_status = bindId(query, 0, group_id, "group_id");
    if (!bind_status.isOk()) {
        return bind_status;
    }

    MySqlQueryResult result;
    const auto query_status = query.executeQuery(result);
    if (!query_status.isOk()) {
        return query_status;
    }
    if (result.rows().empty()) {
        return Status::error(ErrorCode::NotFound, "group was not found");
    }
    if (result.rows().size() != 1U || result.rows().front().values.size() != 1U) {
        return malformedGroupRowStatus();
    }

    const std::string* raw_owner_id = nullptr;
    const auto owner_status = requiredValue(result.rows().front(), 0, raw_owner_id);
    if (!owner_status.isOk()) {
        return owner_status;
    }
    return parseUint64(*raw_owner_id, owner_id);
}

Status insertGroupMember(MySqlConnection& connection,
                         std::uint64_t group_id,
                         std::uint64_t user_id,
                         std::int64_t joined_at_ms) {
    PreparedStatement statement(connection);
    const auto prepare_status =
        statement.prepare("INSERT INTO group_members (group_id, user_id, joined_at_ms) "
                          "VALUES (?, ?, ?) "
                          "ON DUPLICATE KEY UPDATE joined_at_ms = group_members.joined_at_ms");
    if (!prepare_status.isOk()) {
        return prepare_status;
    }
    const auto group_status = bindId(statement, 0, group_id, "group_id");
    if (!group_status.isOk()) {
        return group_status;
    }
    const auto user_status = bindId(statement, 1, user_id, "user_id");
    if (!user_status.isOk()) {
        return user_status;
    }
    const auto joined_status = statement.bindInt64(2, joined_at_ms);
    if (!joined_status.isOk()) {
        return joined_status;
    }

    std::uint64_t affected_rows = 0;
    return statement.executeUpdate(affected_rows);
}

} // namespace

GroupDao::GroupDao(MySqlPool& pool, std::chrono::milliseconds acquire_timeout)
    : pool_(&pool), acquire_timeout_(acquire_timeout) {
}

Status GroupDao::createGroup(const CreateGroupRequest& request, GroupRecord& created_group) {
    const auto owner_status = validateId(request.owner_id, "owner_id");
    if (!owner_status.isOk()) {
        return owner_status;
    }
    if (request.group_name.empty()) {
        return Status::error(ErrorCode::InvalidArgument, "group_name must not be empty");
    }

    ConnectionGuard guard;
    const auto acquire_status = pool_->acquire(acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    const auto begin_status = guard->executeSimple("START TRANSACTION");
    if (!begin_status.isOk()) {
        return begin_status;
    }

    PreparedStatement statement(*guard);
    const auto prepare_status =
        statement.prepare("INSERT INTO chat_groups "
                          "(owner_id, group_name, created_at_ms, updated_at_ms) "
                          "VALUES (?, ?, ?, ?)");
    if (!prepare_status.isOk()) {
        rollbackSilently(*guard);
        return prepare_status;
    }

    const auto now_ms = Timestamp::now().millisecondsSinceEpoch();
    const auto owner_bind_status = bindId(statement, 0, request.owner_id, "owner_id");
    if (!owner_bind_status.isOk()) {
        rollbackSilently(*guard);
        return owner_bind_status;
    }
    const auto name_status = statement.bindString(1, request.group_name);
    if (!name_status.isOk()) {
        rollbackSilently(*guard);
        return name_status;
    }
    const auto created_status = statement.bindInt64(2, now_ms);
    if (!created_status.isOk()) {
        rollbackSilently(*guard);
        return created_status;
    }
    const auto updated_status = statement.bindInt64(3, now_ms);
    if (!updated_status.isOk()) {
        rollbackSilently(*guard);
        return updated_status;
    }

    std::uint64_t affected_rows = 0;
    const auto insert_status = statement.executeUpdate(affected_rows);
    if (!insert_status.isOk()) {
        rollbackSilently(*guard);
        return insert_status;
    }
    if (affected_rows != 1U) {
        rollbackSilently(*guard);
        return Status::error(ErrorCode::InternalError, "create group affected unexpected row count");
    }

    const auto query_status = queryLastInsertedGroup(*guard, created_group);
    if (!query_status.isOk()) {
        rollbackSilently(*guard);
        return query_status;
    }

    const auto member_status = insertGroupMember(*guard, created_group.group_id, request.owner_id, now_ms);
    if (!member_status.isOk()) {
        rollbackSilently(*guard);
        return member_status;
    }

    statement.close();
    return guard->executeSimple("COMMIT");
}

Status GroupDao::addGroupMember(std::uint64_t group_id, std::uint64_t user_id) {
    const auto group_status = validateId(group_id, "group_id");
    if (!group_status.isOk()) {
        return group_status;
    }
    const auto user_status = validateId(user_id, "user_id");
    if (!user_status.isOk()) {
        return user_status;
    }

    ConnectionGuard guard;
    const auto acquire_status = pool_->acquire(acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    return insertGroupMember(*guard, group_id, user_id, Timestamp::now().millisecondsSinceEpoch());
}

Status GroupDao::removeGroupMember(std::uint64_t group_id, std::uint64_t user_id) {
    const auto group_status = validateId(group_id, "group_id");
    if (!group_status.isOk()) {
        return group_status;
    }
    const auto user_status = validateId(user_id, "user_id");
    if (!user_status.isOk()) {
        return user_status;
    }

    ConnectionGuard guard;
    const auto acquire_status = pool_->acquire(acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    std::uint64_t owner_id = 0;
    const auto owner_status = queryGroupOwner(*guard, group_id, owner_id);
    if (!owner_status.isOk()) {
        return owner_status;
    }
    if (owner_id == user_id) {
        return Status::error(ErrorCode::InvalidArgument, "group owner cannot be removed from members");
    }

    PreparedStatement statement(*guard);
    const auto prepare_status =
        statement.prepare("DELETE FROM group_members WHERE group_id = ? AND user_id = ?");
    if (!prepare_status.isOk()) {
        return prepare_status;
    }
    const auto group_bind_status = bindId(statement, 0, group_id, "group_id");
    if (!group_bind_status.isOk()) {
        return group_bind_status;
    }
    const auto user_bind_status = bindId(statement, 1, user_id, "user_id");
    if (!user_bind_status.isOk()) {
        return user_bind_status;
    }

    std::uint64_t affected_rows = 0;
    const auto delete_status = statement.executeUpdate(affected_rows);
    if (!delete_status.isOk()) {
        return delete_status;
    }
    if (affected_rows == 0U) {
        return Status::error(ErrorCode::NotFound, "group member was not found");
    }
    return Status::ok();
}

Status GroupDao::getGroupMembers(std::uint64_t group_id, std::vector<GroupMemberRecord>& members) {
    members.clear();

    const auto group_status = validateId(group_id, "group_id");
    if (!group_status.isOk()) {
        return group_status;
    }

    ConnectionGuard guard;
    const auto acquire_status = pool_->acquire(acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    PreparedStatement statement(*guard);
    const auto prepare_status =
        statement.prepare("SELECT u.user_id, u.username, u.nickname, gm.joined_at_ms "
                          "FROM group_members gm "
                          "JOIN users u ON u.user_id = gm.user_id "
                          "WHERE gm.group_id = ? "
                          "ORDER BY gm.joined_at_ms ASC, u.user_id ASC");
    if (!prepare_status.isOk()) {
        return prepare_status;
    }
    const auto bind_status = bindId(statement, 0, group_id, "group_id");
    if (!bind_status.isOk()) {
        return bind_status;
    }

    MySqlQueryResult result;
    const auto query_status = statement.executeQuery(result);
    if (!query_status.isOk()) {
        return query_status;
    }

    std::vector<GroupMemberRecord> parsed_members;
    parsed_members.reserve(result.rows().size());
    for (const auto& row : result.rows()) {
        GroupMemberRecord member;
        const auto row_status = rowToGroupMemberRecord(row, member);
        if (!row_status.isOk()) {
            return row_status;
        }
        parsed_members.push_back(std::move(member));
    }

    members = std::move(parsed_members);
    return Status::ok();
}

Status GroupDao::findGroupById(std::uint64_t group_id, GroupRecord& group) {
    const auto group_status = validateId(group_id, "group_id");
    if (!group_status.isOk()) {
        return group_status;
    }

    ConnectionGuard guard;
    const auto acquire_status = pool_->acquire(acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    PreparedStatement statement(*guard);
    const auto prepare_status =
        statement.prepare("SELECT group_id, owner_id, group_name, created_at_ms "
                          "FROM chat_groups WHERE group_id = ? LIMIT 1");
    if (!prepare_status.isOk()) {
        return prepare_status;
    }
    const auto bind_status = bindId(statement, 0, group_id, "group_id");
    if (!bind_status.isOk()) {
        return bind_status;
    }

    return querySingleGroup(statement, group);
}

} // namespace liteim
