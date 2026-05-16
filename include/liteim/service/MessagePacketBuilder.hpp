#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/protocol/Packet.hpp"
#include "liteim/storage/StorageTypes.hpp"

namespace liteim {

Status appendMessageFields(const MessageRecord& message, Packet& packet);

}  // namespace liteim
