#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/net/Session.hpp"
#include "liteim/protocol/MessageType.hpp"
#include "liteim/protocol/Packet.hpp"
#include "liteim/protocol/TlvCodec.hpp"

#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace liteim {

class ThreadPool;

class MessageRouter {
public:
    enum class DispatchMode {
        Inline,
        BusinessThread,
    };

    struct RouterRequest {
        Session::Ptr session;
        Packet packet;
        TlvMap fields;
    };

    using Handler = std::function<Status(const RouterRequest&, Packet&)>;

    explicit MessageRouter(ThreadPool& business_pool);

    Status registerHandler(MessageType type, Handler handler, DispatchMode mode);
    void route(Session::Ptr session, Packet packet);

private:
    struct HandlerEntry {
        Handler handler;
        DispatchMode mode{DispatchMode::Inline};
    };

    static std::uint16_t handlerKey(MessageType type) noexcept;
    static void executeHandler(Handler handler, RouterRequest request);
    static void sendError(const Session::Ptr& session, std::uint64_t seq_id, const Status& status);
    static void sendResponse(const Session::Ptr& session, std::uint64_t seq_id, Packet response);

    ThreadPool& business_pool_;
    std::mutex mutex_;
    std::unordered_map<std::uint16_t, HandlerEntry> handlers_;
};

}  // namespace liteim
