#include "liteim/service/MessageRouter.hpp"

#include "liteim/base/ErrorCode.hpp"
#include "liteim/concurrency/ThreadPool.hpp"

#include <exception>
#include <memory>
#include <string>
#include <utility>

namespace liteim {

// 请求消息类型和响应消息类型不要求一样，handler填相应response packet的msg_type 和 body，其余的消息头在sendResponse里统一设置,响应包会带回请求包的 seq_id

// 默认注册心跳处理handler，这里的心跳相应是简单的回复客户端的一个心跳响应包，这个心跳响应的 body 是空的
MessageRouter::MessageRouter(ThreadPool& business_pool) : business_pool_(business_pool) {
    (void)registerHandler(
        MessageType::HeartbeatRequest,
        [](const RouterRequest&, Packet& response) {
            response.header.msg_type = MessageType::HeartbeatResponse;
            return Status::ok();
        },
        DispatchMode::Inline);
}

// 注册某种消息的处理函数handler
Status MessageRouter::registerHandler(MessageType type, Handler handler, DispatchMode mode) {
    if (type == MessageType::Unknown || !isRequestType(type)) {
        return Status::error(ErrorCode::InvalidArgument, "handler message type must be a request");
    }
    if (!handler) {
        return Status::error(ErrorCode::InvalidArgument, "message handler must not be empty");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    handlers_[handlerKey(type)] = HandlerEntry{std::move(handler), mode};
    return Status::ok();
}

// 收到 Packet 后的入口函数，根据消息类型找到对应的 handler，并执行
void MessageRouter::route(Session::Ptr session, Packet packet) {
    if (session == nullptr || session->closed()) {
        return;
    }
    // 解析消息类型，这里的消息类型和TLV的type不同，消息类型在PacketHeader里，是用来路由到不同handler的，TLV的type是在Packet body里，是用来区分不同字段的
    const auto type = packet.header.msg_type;
    if (type == MessageType::Unknown || !isRequestType(type)) {
        sendError(session, packet.header.seq_id,
                  Status::error(ErrorCode::InvalidArgument, "message type is not a request"));
        return;
    }
    // 解析 TLV
    TlvMap fields;
    const auto parse_status = parseTlvMap(packet.body, fields);
    if (!parse_status.isOk()) {
        sendError(session, packet.header.seq_id, parse_status);
        return;
    }

    HandlerEntry entry;
    bool found_handler = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = handlers_.find(handlerKey(type));
        if (it != handlers_.end()) {
            entry = it->second;
            found_handler = true;
        }
    }
    if (!found_handler) {
        sendError(session, packet.header.seq_id,
                  Status::error(ErrorCode::NotFound, "no message handler registered"));
        return;
    }
    // 当前线程直接执行,例如心跳
    if (entry.mode == DispatchMode::Inline) {
        executeHandler(std::move(entry.handler),
                       RouterRequest{std::move(session), std::move(packet), std::move(fields)});
        return;
    }
    // 丢到业务线程池执行
    std::weak_ptr<Session> weak_session(session);
    const auto seq_id = packet.header.seq_id;
    auto handler = std::move(entry.handler);
    auto submit_status =
        business_pool_.submit([weak_session, handler = std::move(handler),
                               packet = std::move(packet), fields = std::move(fields)]() mutable {
            auto locked = weak_session.lock();  // 如果 session 还活着，拿到shared_ptr
            if (locked == nullptr || locked->closed()) {
                return;
            }

            executeHandler(std::move(handler),
                           RouterRequest{std::move(locked), std::move(packet), std::move(fields)});
        });
    if (!submit_status.isOk()) {
        sendError(session, seq_id, submit_status);
    }
}

std::uint16_t MessageRouter::handlerKey(MessageType type) noexcept {
    return static_cast<std::uint16_t>(type);
}

// 真正调用业务处理函数，route函数根据msg_type找到对应的handler来执行并处理消息
void MessageRouter::executeHandler(Handler handler, RouterRequest request) {
    if (request.session == nullptr || request.session->closed()) {
        return;
    }

    Packet response;
    Status handler_status = Status::ok();
    try {
        // 调用业务 handler 处理消息,得到 handlerStatus 和 response
        handler_status = handler(request, response);
    } catch (const std::exception& ex) {
        handler_status = Status::error(ErrorCode::InternalError, ex.what());
    } catch (...) {
        handler_status = Status::error(ErrorCode::InternalError, "message handler failed");
    }

    if (!handler_status.isOk()) {
        sendError(request.session, request.packet.header.seq_id, handler_status);
        return;
    }

    sendResponse(request.session, request.packet.header.seq_id, std::move(response));
}

// 统一错误响应,把错误码和错误信息写进 TLV body,构建一个Packet发回客户端
void MessageRouter::sendError(const Session::Ptr& session, std::uint64_t seq_id,
                              const Status& status) {
    Packet response;
    response.header.msg_type = MessageType::ErrorResponse;

    const auto code = static_cast<std::uint64_t>(status.code());
    const auto append_code_status = appendUint64(TlvType::ErrorCode, code, response.body);
    if (!append_code_status.isOk()) {
        return;
    }

    std::string message = status.message();
    if (message.empty()) {
        message = toString(status.code());
    }
    const auto append_message_status = appendString(TlvType::ErrorMessage, message, response.body);
    if (!append_message_status.isOk()) {
        return;
    }

    sendResponse(session, seq_id, std::move(response));
}

// 给Packet统一补响应头并发送,响应包会带回请求包的 seq_id
void MessageRouter::sendResponse(const Session::Ptr& session, std::uint64_t seq_id,
                                 Packet response) {
    if (session == nullptr || session->closed()) {
        return;
    }

    response.header.magic = kPacketMagic;
    response.header.version = kPacketVersion;
    response.header.flags = kPacketFlagsNone;
    response.header.seq_id = seq_id;  // 强制设置响应包的 seq_id 和请求包一致
    const auto send_status = session->sendPacket(response);
    (void)send_status;
}

}  // namespace liteim
