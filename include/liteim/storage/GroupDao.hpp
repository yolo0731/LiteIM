#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/storage/StorageTypes.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

namespace liteim {

class MySqlPool;

class GroupDao {
public:
    explicit GroupDao(MySqlPool& pool,
                      std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds(500));

    Status createGroup(const CreateGroupRequest& request, GroupRecord& created_group);
    Status addGroupMember(std::uint64_t group_id, std::uint64_t user_id);
    Status removeGroupMember(std::uint64_t group_id, std::uint64_t user_id);
    Status getGroupMembers(std::uint64_t group_id, std::vector<GroupMemberRecord>& members);
    Status findGroupById(std::uint64_t group_id, GroupRecord& group);

private:
    MySqlPool* pool_;
    std::chrono::milliseconds acquire_timeout_;
};

} // namespace liteim
