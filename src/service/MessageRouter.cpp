#include "liteim/service/MessageRouter.hpp"

#include "liteim/base/ErrorCode.hpp"
#include "liteim/concurrency/ThreadPool.hpp"

#include <exception>
#include <memory>
#include <string>
#include <utility>

namespace liteim {

MessageRouter::MessageRouter(ThreadPool& business_pool) : business_pool_(business_pool) {
    (void)registerHandler(
        MessageType::HeartbeatRequest,
        [](const RouterRequest&, Packet& response) {
            response.header.msg_type = MessageType::HeartbeatResponse;
            return Status::ok();
        },
        DispatchMode::Inline);
}

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

void MessageRouter::route(Session::Ptr session, Packet packet) {
    if (session == nullptr || session->closed()) {
        return;
    }

    const auto type = packet.header.msg_type;
    if (type == MessageType::Unknown || !isRequestType(type)) {
        sendError(session, packet.header.seq_id,
                  Status::error(ErrorCode::InvalidArgument, "message type is not a request"));
        return;
    }

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

    if (entry.mode == DispatchMode::Inline) {
        executeHandler(std::move(entry.handler),
                       RouterRequest{std::move(session), std::move(packet), std::move(fields)});
        return;
    }

    std::weak_ptr<Session> weak_session(session);
    const auto seq_id = packet.header.seq_id;
    auto handler = std::move(entry.handler);
    auto submit_status = business_pool_.submit(
        [weak_session, handler = std::move(handler), packet = std::move(packet),
         fields = std::move(fields)]() mutable {
            auto locked = weak_session.lock();
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

void MessageRouter::executeHandler(Handler handler, RouterRequest request) {
    if (request.session == nullptr || request.session->closed()) {
        return;
    }

    Packet response;
    Status handler_status = Status::ok();
    try {
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
    const auto append_message_status =
        appendString(TlvType::ErrorMessage, message, response.body);
    if (!append_message_status.isOk()) {
        return;
    }

    sendResponse(session, seq_id, std::move(response));
}

void MessageRouter::sendResponse(const Session::Ptr& session, std::uint64_t seq_id,
                                 Packet response) {
    if (session == nullptr || session->closed()) {
        return;
    }

    response.header.magic = kPacketMagic;
    response.header.version = kPacketVersion;
    response.header.flags = kPacketFlagsNone;
    response.header.seq_id = seq_id;
    const auto send_status = session->sendPacket(response);
    (void)send_status;
}

}  // namespace liteim
