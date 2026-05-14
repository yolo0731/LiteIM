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

// 把收到的 Packet 按消息类型分发给对应的业务 handler，并统一回包 / 报错
// handler在对应的业务层实现，MessageRouter 只负责分发，不关心 handler 里具体的业务逻辑，比如 AuthService 里 handler 会实现注册和登录的业务
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
        TlvMap fields;  // Router 统一解析后的 TLV map
    };
    // handler 函数类型，一个request输入，一个response输出
    using Handler = std::function<Status(const RouterRequest&, Packet&)>;

    explicit MessageRouter(ThreadPool& business_pool);

    // 注册某个 request 类型的 handler
    Status registerHandler(MessageType type, Handler handler, DispatchMode mode);
    // 收到 Packet 后的入口函数,解析 TLV body,根据 msg_type 找 handler,根据 DispatchMode 决定直接执行还是投递到业务线程池
    void route(Session::Ptr session, Packet packet);

private:
    struct HandlerEntry {
        Handler handler;  // 某种消息的处理函数
        DispatchMode mode{DispatchMode::Inline};
    };

    static std::uint16_t handlerKey(MessageType type) noexcept;
    static void executeHandler(Handler handler, RouterRequest request);
    static void sendError(const Session::Ptr& session, std::uint64_t seq_id, const Status& status);
    static void sendResponse(const Session::Ptr& session, std::uint64_t seq_id, Packet response);

    ThreadPool& business_pool_;
    std::mutex mutex_;
    std::unordered_map<std::uint16_t, HandlerEntry> handlers_;
    // msg_type -> { handler函数, 执行模式 }
};

}  // namespace liteim
