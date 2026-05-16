#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/storage/StorageTypes.hpp"

#include <cstdint>
#include <string>

namespace liteim {

struct BotOptions {
    std::uint64_t user_id{9001};
    std::string username{"mira_bot"};
    std::string mention{"@mira_bot"};
};

struct BotReply {
    std::string text;
};

class BotGateway {
public:
    virtual ~BotGateway() = default;

    virtual Status onPrivateMessage(const MessageRecord& message, BotReply& reply) = 0;
    virtual Status onGroupMention(const MessageRecord& message, BotReply& reply) = 0;
};

class EchoBotGateway final : public BotGateway {
public:
    explicit EchoBotGateway(std::string prefix = "Echo: ");

    Status onPrivateMessage(const MessageRecord& message, BotReply& reply) override;
    Status onGroupMention(const MessageRecord& message, BotReply& reply) override;

private:
    std::string prefix_;
};

}  // namespace liteim
